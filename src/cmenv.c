#include "cmenv.h"

#include "imcode.h"
#include "mytype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int   yylineno;
extern void *CodeAddr[OP_NOP + 1];

// Init for CmEnv
CmEnvironment CmEnv = {
    .put_warning_for_cnct_property = 1, // with warning
    .put_compiled_codes = 0,            // without outputting codes
    .tco = 0,                           // without tail call opt.
};

int CmEnv_Optimise_VMCode_CopyPropagation(int target_imcode_addr) {

  struct IMCode_tag *imcode;
  int                load_from, load_to;

  load_from = IMCode[target_imcode_addr].operand1;
  load_to = IMCode[target_imcode_addr].operand2;

  // When the given line is: LOAD src1 src1
  if (load_from == load_to &&
      IMCode[target_imcode_addr].opcode == OP_LOAD) {
    IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
    return 1;
  }

  // ------------------------------------------------------------
  // This part was commented out at v0.10.5
  // Even if LOAD_META reg_n reg_n occurs, the varn could be preserved
  // to another reg_n during the execusion, so it must not be DEAD_CODE here.
  // ------------------------------------------------------------
  /*
  // When the given line is: LOAD_META src1 src1
  if ((load_from == load_to) &&
      (IMCode[target_imcode_addr].opcode == OP_LOAD_META)) {
    IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
    return 1;
  }
  */

  for (int line_num = target_imcode_addr + 1; line_num < IMCode_n; line_num++) {
    imcode = &IMCode[line_num];

    if (imcode->opcode == OP_BEGIN_BLOCK) {
      // This optimisation will last until OP_BEGIN_BLOCK
      return 0;
    }
    //    if ((CmEnv.is_in_jmpcnctBlock) &&
    //	(imcode->opcode == OP_BEGIN_JMPCNCT_BLOCK)) {
    //      // This optimisation will last until OP_BEGIN_JMPCNCT_BLOCK
    //      return 0;
    //    }

    switch (imcode->opcode) {

    case OP_MKNAME:
    case OP_MKGNAME:
    case OP_MKAGENT:
      break;

    case OP_LOADP:
      // LOADP src port dest
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_LOADP_L:
    case OP_LOADP_R:
      // LOADP_L src port
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_PUSH:
    case OP_MYPUSH:
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      if (imcode->operand2 == load_to) {
        imcode->operand2 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_JMPEQ0:
    case OP_JMPNEQ0:
    case OP_JMPCNCT_CONS:
    case OP_JMPCNCT:
    case OP_PUSHI:
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_LT:
    case OP_LE:
    case OP_EQ:
    case OP_NE:
    case OP_ADD:
    case OP_SUB:
      // op src1 src2 dest
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      if (imcode->operand2 == load_to) {
        imcode->operand2 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_ADDI:
    case OP_SUBI:
    case OP_EQI:
      // op src1 $2 dest
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_LT_R0:
    case OP_LE_R0:
    case OP_EQ_R0:
    case OP_NE_R0:
      // op src1 src2
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      if (imcode->operand2 == load_to) {
        imcode->operand2 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_EQI_R0:
      // op src1 $2
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_UNM:
    case OP_RAND:
    case OP_INC:
    case OP_DEC:
      // op src dest
      if (imcode->operand1 == load_to) {
        imcode->operand1 = load_from;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;
    }
  }

  return 0;
}

#define MAX_LABEL     50
#define MAX_BACKPATCH MAX_LABEL * 2
int CmEnv_generate_VMCode(void **code) {
  int                addr = 0;
  struct IMCode_tag *imcode;

  int label_table[MAX_LABEL];
  int backpatch_num = 0;
  int backpatch_table[MAX_BACKPATCH];

#ifdef OPTIMISE_IMCODE
  CmEnv_clear_register_assignment_table_all();
#endif

  for (int line_num = 0; line_num < IMCode_n; line_num++) {
    imcode = &IMCode[line_num];

    // DEBUG
    //    printf("%d\n", line_num);
    //    VMCode_puts(code, addr);

#ifdef OPTIMISE_IMCODE
    // optimisation
    // Copy Propagation for OP_LOAD reg1, reg2 --> [reg2/reg1].
    // When it is success, the code is applied Dead Code Elimination to.
    if (imcode->opcode == OP_LOAD) {
      if (CmEnv_Optimise_VMCode_CopyPropagation(line_num)) {
        continue;
      }
    }
    if (imcode->opcode == OP_LOAD_META) {
      if (CmEnv_Optimise_VMCode_CopyPropagation(line_num)) {
        continue;
      }
    }
    if (imcode->opcode == OP_LOADI) {
      if (CmEnv_Optimise_VMCode_CopyPropagation_LOADI(line_num)) {
        continue;
      }
    }
#endif

    switch (imcode->opcode) {
    case OP_MKNAME: {
      //      printf("OP_MKNAME var%d\n", imcode->operand1);
      long dest = imcode->operand1;

#ifdef OPTIMISE_IMCODE
      dest = CmEnv_get_newreg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)dest;
      break;
    }
    case OP_MKGNAME: {
      //      printf("OP_MKGNAME sym:%d var%d (as `%s')\n",
      //	     imcode->operand1, imcode->operand2,
      //	     CmEnv.bind[imcode->operand1].name);
      long dest = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      dest = CmEnv_get_newreg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)imcode->operand1;
      code[addr++] = (void *)(unsigned long)dest;

      break;
    }

    case OP_MKAGENT: {
      //      printf("OP_MKAGENT id var%d:%d\n",
      //	     imcode->operand1, imcode->operand2);
      long dest = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      dest = CmEnv_get_newreg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)imcode->operand1;
      code[addr++] = (void *)(unsigned long)dest;
      break;
    }

    case OP_LOAD: {
      // OP_LOAD src1 dest
      long src1 = imcode->operand1;
      long dest = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);

      dest = CmEnv_get_newreg(dest);

      if (src1 == dest) {
        imcode->opcode = OP_DEAD_CODE;
        break;
      }
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)dest;
      break;
    }

    case OP_LOAD_META: {
      // OP_LOAD_META src1 dest
      // the dest is used as it is
      long src1 = imcode->operand1;
      long dest = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);

      //      dest = CmEnv_get_newreg(dest);

#  ifdef OPTIMISE_TWO_ADDRESS
      CmEnv_assign_reg(dest, dest);
#  endif

      if (src1 == dest) {
        imcode->opcode = OP_DEAD_CODE;
        break;
      }

#endif

      code[addr++] = CodeAddr[OP_LOAD];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)dest;
      break;
    }

    case OP_LOADP: {
      // OP_LOADP src1 port dest
      long src1 = imcode->operand1;
      long port = imcode->operand2;
      long dest = imcode->operand3;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);

      dest = CmEnv_using_reg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)port;
      code[addr++] = (void *)(unsigned long)dest;
      break;
    }

    case OP_LOADP_L:
    case OP_LOADP_R: {
      // OP_LOADP_L src port
      long src1 = imcode->operand1;
      long port = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);
      //      dest = CmEnv_using_reg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)port;
      break;
    }

    case OP_CHID_L:
    case OP_CHID_R: {
      // OP_CHID_L id
      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)imcode->operand1;
      break;
    }

    case OP_LOADI: {
      // OP_LOADI int1 dest
      long dest = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      dest = CmEnv_get_newreg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)INT2FIX(imcode->operand1);
      code[addr++] = (void *)(unsigned long)dest;

      break;
    }

    case OP_LOADI_SHARED: {
      // OP_LOADI int1 dest
      long dest = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      //      dest = CmEnv_get_newreg(dest);
      {
        dest = CmEnv_using_reg_with_nothing_info(dest);
        if (dest == -1) {
          dest = CmEnv_get_newreg(imcode->operand2);
        }
      }
#endif

      code[addr++] = CodeAddr[OP_LOADI];
      code[addr++] = (void *)INT2FIX(imcode->operand1);
      code[addr++] = (void *)(unsigned long)dest;

      break;
    }

      // OP_LOADI_META を入れよう
      // META ではなく、KEEP_REG とか、OPT を遠ざける意味としよう。
      // TODO: and と or の LABEL分岐による結果導出 loadi $1 reg1, load $0 reg1
      // の reg1 を同じものに割り当てたい。だから、この KEEP_REG を使いたい

    case OP_PUSH:
    case OP_MYPUSH: {
      //      printf("OP_PUSH var%d var%d\n",
      //	     imcode->operand1, imcode->operand2);
      long src1 = imcode->operand1;
      long src2 = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1,
                                                   line_num) &&
          imcode->operand1 != imcode->operand2)
        CmEnv_free_reg(src1);

      src2 = CmEnv_using_reg(src2);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand2,
                                                   line_num) &&
          imcode->operand1 != imcode->operand2)
        CmEnv_free_reg(src2);

      /*
      src1 = CmEnv_using_reg(src1);
      if (imcode->operand1 != imcode->operand2)
        CmEnv_free_reg(src1);

      src2 = CmEnv_using_reg(src2);
      CmEnv_free_reg(src2);
      */

#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)src2;

      break;
    }
    case OP_JMPEQ0:
    case OP_JMPNEQ0:
    case OP_JMPCNCT_CONS: {
      // OP_JMPEQ0 reg label
      long src1 = imcode->operand1;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      // for label
      backpatch_table[backpatch_num++] = addr;
      code[addr++] = (void *)(unsigned long)imcode->operand2;
      break;
    }

    case OP_JMPEQ0_R0: {
      code[addr++] = CodeAddr[imcode->opcode];

      // for label
      backpatch_table[backpatch_num++] = addr;
      code[addr++] = (void *)(unsigned long)imcode->operand1;
      break;
    }

    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_LT:
    case OP_LE:
    case OP_EQ:
    case OP_NE: {
      // op src1 src2 dest
      long src1 = imcode->operand1;
      long src2 = imcode->operand2;
      long dest = imcode->operand3;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1,
                                                   line_num) &&
          imcode->operand1 != imcode->operand2)
        CmEnv_free_reg(src1);

      src2 = CmEnv_using_reg(src2);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand2, line_num))
        CmEnv_free_reg(src2);

      dest = CmEnv_get_newreg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)src2;
      code[addr++] = (void *)(unsigned long)dest;

      break;
    }

    case OP_ADDI:
    case OP_SUBI: {
      // op src1 $2 dest
      long src1 = imcode->operand1;
      long dest = imcode->operand3;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);

      dest = CmEnv_get_newreg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)imcode->operand2;
      code[addr++] = (void *)(unsigned long)dest;

      break;
    }

    case OP_EQI: {
      // op src1 int2fix($2) dest
      long src1 = imcode->operand1;
      long dest = imcode->operand3;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);

      dest = CmEnv_get_newreg(dest);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)INT2FIX(imcode->operand2);
      code[addr++] = (void *)(unsigned long)dest;

      break;
    }

    case OP_LT_R0:
    case OP_LE_R0:
    case OP_EQ_R0:
    case OP_NE_R0: {
      // op src1 src2
      long src1 = imcode->operand1;
      long src2 = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1,
                                                   line_num) &&
          imcode->operand1 != imcode->operand2)
        CmEnv_free_reg(src1);

      src2 = CmEnv_using_reg(src2);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand2, line_num))
        CmEnv_free_reg(src2);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)src2;
      break;
    }

    case OP_EQI_R0: {
      // op src1 int2

#ifdef OPTIMISE_IMCODE
      if (imcode->operand2 == 0 &&
          IMCode[line_num + 1].opcode == OP_JMPEQ0_R0) {
        // OP_EQI_Ro reg1 $0
        // OP_JMPEQ0_R0 pc
        // ==>
        // DEAD_CODE
        // OP_JMPNEQ reg1 pc
        IMCode[line_num + 1].opcode = OP_JMPNEQ0;
        IMCode[line_num + 1].operand2 = IMCode[line_num + 1].operand1;
        IMCode[line_num + 1].operand1 = imcode->operand1;
        imcode->opcode = OP_DEAD_CODE;
        break;
      }
#endif

      long src1 = imcode->operand1;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)INT2FIX(imcode->operand2);
      break;
    }

    case OP_UNM:
    case OP_INC:
    case OP_DEC:
    case OP_RAND: {
      // op src dest
      long src1 = imcode->operand1;
      long dest = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);

#  if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
      dest = CmEnv_get_newreg(dest);
#  else
      int new_reg = CmEnv_assign_reg(dest, src1);
      if (new_reg > 0) {
        code[addr++] = CodeAddr[OP_LOAD];
        code[addr++] = (void *)(unsigned long)src1;
        code[addr++] = (void *)(unsigned long)new_reg;
      }
#  endif

#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
#if !defined(OPTIMISE_TWO_ADDRESS) || !defined(OPTIMISE_TWO_ADDRESS_UNARY)
      code[addr++] = (void *)(unsigned long)dest;
#endif
      break;
    }

    case OP_PUSHI: {
      // OP src1 int2
      long src1 = imcode->operand1;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      // int
      code[addr++] = (void *)INT2FIX(imcode->operand2);

      break;
    }

    case OP_RET:
    case OP_RET_FREE_L:
    case OP_RET_FREE_R:
    case OP_RET_FREE_LR:
    case OP_LOOP:
    case OP_NOP:
      code[addr++] = CodeAddr[imcode->opcode];
      break;

    case OP_LOOP_RREC1:
    case OP_LOOP_RREC2:
    case OP_LOOP_RREC1_FREE_R:
    case OP_LOOP_RREC2_FREE_R: {
      //      printf("OP_LOOP_RREC1 var%d\n",
      //	     imcode->operand1);
      long src1 = imcode->operand1;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      break;
    }

    case OP_LOOP_RREC:
    case OP_LOOP_RREC_FREE_R: {
      //      printf("OP_LOOP_RREC1 var%d $%d\n",
      //	     imcode->operand1, imcode->operand2);
      long src1 = imcode->operand1;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)imcode->operand2;
      break;
    }

    case OP_JMPCNCT: {
      // OP_JMPCNCT var id label
      //      printf("OP_JMPCNCT var%d id%d $%d\n",
      //	     imcode->operand1, imcode->operand2, imcode->operand3);
      long src1 = imcode->operand1;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1, line_num))
        CmEnv_free_reg(src1);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      // int
      code[addr++] = (void *)(unsigned long)imcode->operand2;
      // for label
      backpatch_table[backpatch_num++] = addr;
      code[addr++] = (void *)(unsigned long)imcode->operand3;
      break;
    }

    case OP_JMP:
      //      printf("OP_JMP $%d\n",
      //	     imcode->operand1);
      code[addr++] = CodeAddr[imcode->opcode];

      backpatch_table[backpatch_num++] = addr;
      code[addr++] = (void *)(unsigned long)imcode->operand1;
      break;

    case OP_CNCTGN:
    case OP_SUBSTGN: {
      //      printf("OP_CNCTGN var%d $%d\n",
      //	     imcode->operand1, imcode->operand2);
      long src1 = imcode->operand1;
      long src2 = imcode->operand2;

#ifdef OPTIMISE_IMCODE
      src1 = CmEnv_using_reg(src1);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand1,
                                                   line_num) &&
          imcode->operand1 != imcode->operand2)
        CmEnv_free_reg(src1);

      src2 = CmEnv_using_reg(src2);
      if (!CmEnv_Optimise_check_occurence_in_block(imcode->operand2, line_num))
        CmEnv_free_reg(src2);
#endif

      code[addr++] = CodeAddr[imcode->opcode];
      code[addr++] = (void *)(unsigned long)src1;
      code[addr++] = (void *)(unsigned long)src2;
      break;
    }

    case OP_DEAD_CODE:
      break;

    case OP_BEGIN_BLOCK:
#ifdef OPTIMISE_IMCODE
      CmEnv_clear_register_assignment_table_all();
#endif
      break;

#ifdef OPTIMISE_TWO_ADDRESS
    case OP_BEGIN_JMPCNCT_BLOCK:
      //      CmEnv_clear_jmpcnctBlock_assignment_table_all();

      CmEnv_stack_assignment_table();
      CmEnv.is_in_jmpcnctBlock = 1;
      break;
#endif

    case OP_LABEL:
      if (imcode->operand1 > MAX_LABEL) {
        printf("Critical Error: Label number overfllow.");
        exit(-1);
      }
      label_table[imcode->operand1] = addr;
      break;

    default:
      printf("Error[CmEnv_generate_VMCode]: %d does not match any opcode\n",
             imcode->opcode);
      exit(-1);
    }
  }

  // two pass for label
  for (int i = 0; i < backpatch_num; i++) {
    int  hole_addr = backpatch_table[i];
    long jmp_label = (unsigned long)code[hole_addr];
    code[hole_addr] =
        (void *)(unsigned long)(label_table[jmp_label] - (hole_addr + 1));
  }

  return addr;
}

void CmEnv_clear_register_assignment_table_all(void) {
  for (int i = 0; i < VM_OFFSET_LOCALVAR; i++) {
    CmEnv.tmpRegState[i] = i; // occupied already
  }
  for (int i = VM_OFFSET_LOCALVAR; i < VM_REG_SIZE; i++) {
    CmEnv.tmpRegState[i] = -1; // free
  }

#ifdef OPTIMISE_TWO_ADDRESS
  CmEnv_clear_jmpcnctBlock_assignment_table_all();
#endif
}

void CmEnv_clear_localnamePtr(void) { CmEnv.localNamePtr = VM_OFFSET_LOCALVAR; }
void CmEnv_clear_bind(int preserve_idx) {
  // clear reference counter only, until preserve_idx
  for (int i = 0; i <= preserve_idx; i++) {
    CmEnv.bind[i].refnum = 0;
  }

  // after that, clear everything
  for (int i = preserve_idx + 1; i < MAX_NBIND; i++) {
    CmEnv.bind[i].name = NULL;
    CmEnv.bind[i].refnum = 0;
    CmEnv.bind[i].reg = 0;
  }

  // new name information will be stored from preserve_idx+1
  CmEnv.bindPtr = preserve_idx + 1;

  // Reset the number of compilation errors
  CmEnv.count_compilation_errors = 0;
}

#ifdef OPTIMISE_TWO_ADDRESS
void CmEnv_clear_jmpcnctBlock_assignment_table_all(void) {
  for (int i = 0; i < VM_REG_SIZE; i++) {
    CmEnv.jmpcnctBlockRegState[i] = -1;
  }
  CmEnv.is_in_jmpcnctBlock = 0;
}
void CmEnv_stack_assignment_table(void) {
  for (int i = 0; i < VM_REG_SIZE; i++) {
    CmEnv.jmpcnctBlockRegState[i] = CmEnv.tmpRegState[i];
  }
}
#endif

void CmEnv_clear_all(void) {
  // clear all the information of names.
  CmEnv_clear_bind(-1);

  // reset the annotation properties for rule agents.
  CmEnv.bindPtr_metanames = -1;
  CmEnv.annotateL = ANNOTATE_NOTHING;
  CmEnv.annotateR = ANNOTATE_NOTHING;

  // reset the beginning number for local vars.
  CmEnv_clear_localnamePtr();

  // reset the index of storage for imtermediate codes.
  IMCode_init();

  // reset the index of labels;
  CmEnv.label = 0;

  // reset the register assignment table
  CmEnv_clear_register_assignment_table_all();

  // reset the number of compilation erros
  CmEnv.count_compilation_errors = 0;
}
void CmEnv_clear_keeping_rule_properties(void) {
  // clear the information of names EXCEPT meta ones.
  CmEnv_clear_bind(CmEnv.bindPtr_metanames);

  // Except of  ANNOTATE_INT and ANNOTATE_WILDCARD
  if (CmEnv.annotateL != ANNOTATE_INT_MODIFIER &&
      CmEnv.annotateL != ANNOTATE_WILDCARD) {
    CmEnv.annotateL = ANNOTATE_NOTHING;
  }
  if (CmEnv.annotateR != ANNOTATE_INT_MODIFIER &&
      CmEnv.annotateR != ANNOTATE_WILDCARD) {
    CmEnv.annotateR = ANNOTATE_NOTHING;
  }

  // reset the beginning number for local vars.
  CmEnv_clear_localnamePtr();

  /*
  // reset the index of storage for imtermediate codes.
  IMCode_init();
  */
}
int CmEnv_get_newlabel(void) { return CmEnv.label++; }
int CmEnv_set_symbol_as_name(char *name) {
  // return: a regnum for the given name.

  int result;

  if (name != NULL) {
    CmEnv.bind[CmEnv.bindPtr].name = name;
    CmEnv.bind[CmEnv.bindPtr].reg = CmEnv.localNamePtr;
    CmEnv.bind[CmEnv.bindPtr].refnum = 0;
    CmEnv.bind[CmEnv.bindPtr].type = NB_NAME;

    CmEnv.bindPtr++;
    if (CmEnv.bindPtr > MAX_NBIND) {
      puts("SYSTEM ERROR: CmEnv.bindPtr exceeded MAX_NBIND.");
      exit(-1);
    }

    result = CmEnv.localNamePtr;
    CmEnv.localNamePtr++;
    /*
    if (CmEnv.localNamePtr > VM_REG_SIZE) {
      puts("SYSTEM ERROR: CmEnv.localNamePtr exceeded VM_REG_SIZE.");
      exit(-1);
    }
    */

    return result;
  }
  return -1;
}
void CmEnv_set_symbol_as_meta(char *name, int reg, NB_TYPE type) {

  if (name != NULL) {
    CmEnv.bind[CmEnv.bindPtr].name = name;
    CmEnv.bind[CmEnv.bindPtr].reg = reg;
    CmEnv.bind[CmEnv.bindPtr].refnum = 0;
    CmEnv.bind[CmEnv.bindPtr].type = type;

    // update the last index for metanames
    CmEnv.bindPtr_metanames = CmEnv.bindPtr;

    CmEnv.bindPtr++;
    if (CmEnv.bindPtr > MAX_NBIND) {
      puts("SYSTEM ERROR: CmEnv.bindPtr exceeded MAX_NBIND.");
      exit(-1);
    }
  }
}
int CmEnv_set_as_INTVAR(char *name) {
  int result;

  if (name != NULL) {
    CmEnv.bind[CmEnv.bindPtr].name = name;
    CmEnv.bind[CmEnv.bindPtr].reg = CmEnv.localNamePtr;
    CmEnv.bind[CmEnv.bindPtr].refnum = 0;
    CmEnv.bind[CmEnv.bindPtr].type = NB_INTVAR;
    result = CmEnv.localNamePtr;

    CmEnv.bindPtr++;
    if (CmEnv.bindPtr > MAX_NBIND) {
      puts("SYSTEM ERROR: CmEnv.bindPtr exceeded MAX_NBIND.");
      exit(-1);
    }

    CmEnv.localNamePtr++;
    /*
    if (CmEnv.localNamePtr > VM_REG_SIZE) {
      puts("SYSTEM ERROR: CmEnv.localNamePtr exceeded VM_REG_SIZE.");
      exit(-1);
    }
    */

    return result;
  }
  return -1;
}

int CmEnv_find_var(char *key) {
  int i;
  for (i = 0; i < CmEnv.bindPtr; i++) {
    if (strcmp(key, CmEnv.bind[i].name) == 0) {

      // #ifndef OPTIMISE_IMCODE_TCO
      if (!CmEnv.tco) {
        CmEnv.bind[i].refnum++;

      } else {
        // During compilation on ANNOTATE_TCO, ignore counting up.
        if (CmEnv.annotateL != ANNOTATE_TCO) {
          CmEnv.bind[i].refnum++;
        }
      }

      return CmEnv.bind[i].reg;
    }
  }
  return -1;
}
int CmEnv_gettype_forname(char *key, NB_TYPE *type) {
  int i;
  for (i = 0; i < CmEnv.bindPtr; i++) {
    if (strcmp(key, CmEnv.bind[i].name) == 0) {
      *type = CmEnv.bind[i].type;
      return 1;
    }
  }
  return 0;
}
int CmEnv_newvar(void) {

  int result;
  result = CmEnv.localNamePtr;
  CmEnv.localNamePtr++;
  /*
  if (CmEnv.localNamePtr > VM_REG_SIZE) {
    puts("SYSTEM ERROR: CmEnv.localNamePtr exceeded VM_REG_SIZE.");
    exit(-1);
  }
  */
  return result;
}
int CmEnv_check_linearity_in_rule(void) {
  int i;

  for (i = 0; i < CmEnv.bindPtr; i++) {

    if (CmEnv.bind[i].type == NB_META_L ||
        CmEnv.bind[i].type == NB_META_R ||
        CmEnv.bind[i].type == NB_WILDCARD) {
      if (CmEnv.bind[i].refnum != 1) { // Be just once!
        printf("%d:ERROR: `%s' is referred not once in the right-hand side ",
               yylineno, CmEnv.bind[i].name);
        return 0;
      }

    } else if (CmEnv.bind[i].type == NB_NAME) {
      if (CmEnv.bind[i].refnum != 1) {
        // It must occur twice.
        // The first occurrence is just recorded and not counted as referred.
        // So, be just once in the RHS.
        printf("%d:ERROR: `%s' is referred not twice in the right-hand side ",
               yylineno, CmEnv.bind[i].name);
        return 0;
      }
    }
  }

  return 1;
}
int CmEnv_check_name_reference_times(void) {
  int i;
  for (i = 0; i < CmEnv.bindPtr; i++) {
    if (CmEnv.bind[i].type == NB_NAME) {
      if (CmEnv.bind[i].refnum > 1) {
        printf("%d:ERROR: The name `%s' occurs more than twice.\n", yylineno,
               CmEnv.bind[i].name);
        return 0;
      }
    }
  }
  return 1;
}
void CmEnv_retrieve_MKGNAME(void) {
  int local_var;

  for (int i = 0; i < IMCode_n; i++) {
    if (IMCode[i].opcode != OP_MKNAME) {
      continue;
    }

    local_var = IMCode[i].operand1;

    for (int j = 0; j < CmEnv.bindPtr; j++) {
      if (CmEnv.bind[j].type == NB_NAME && CmEnv.bind[j].reg == local_var &&
          CmEnv.bind[j].refnum == 0) {

        // ==> OP_MKGNAME id dest
        char *sym = CmEnv.bind[j].name;
        int   sym_id = NameTable_get_id(sym);
        if (!IS_GNAMEID(sym_id)) {
          // new occurrence
          sym_id = IdTable_new_gnameid();
          NameTable_set_id(sym, sym_id);
          IdTable_set_name(sym_id, sym);
        }

        IMCode[i].opcode = OP_MKGNAME;
        IMCode[i].operand2 = IMCode[i].operand1;
        IMCode[i].operand1 = sym_id;
      }
    }
  }
}
int CmEnv_using_reg_with_nothing_info(int localvar) {
#ifndef OPTIMISE_TWO_ADDRESS
  for (int i = 0; i < VM_REG_SIZE; i++) {
    if (CmEnv.tmpRegState[i] == localvar) {
      return i;
    }
  }
  return -1; // nothing
#else

  // For two address

  if (CmEnv.is_in_jmpcnctBlock) {
    for (int i = 0; i < VM_REG_SIZE; i++) {
      if (CmEnv.jmpcnctBlockRegState[i] == localvar) {
        return i;
      }
    }
  } else {
    for (int i = 0; i < VM_REG_SIZE; i++) {
      if (CmEnv.tmpRegState[i] == localvar) {
        return i;
      }
    }
  }
  return -1; // nothing
#endif
}
int CmEnv_using_reg(int localvar) {
#ifndef OPTIMISE_TWO_ADDRESS
  int retval = CmEnv_using_reg_with_nothing_info(localvar);
  if (retval != -1) {
    return retval;
  }
  printf("ERROR[CmEnv_using_reg]: No register assigned to var%d\n", localvar);
  exit(1);
#else

  // For two address
  int retval = CmEnv_using_reg_with_nothing_info(localvar);
  if (retval != -1) {
    return retval;
  }
  printf("ERROR[CmEnv_using_reg]: No register assigned to var%d\n", localvar);
  printf("is_in_jmpcnctBlock:%d\n", CmEnv.is_in_jmpcnctBlock);
  exit(1);
#endif
}
int CmEnv_get_newreg(int localvar) {
#ifndef OPTIMISE_TWO_ADDRESS
  //  for (int i=VM_OFFSET_LOCALVAR; i<VM_REG_SIZE; i++) {
  for (int i = 1; i < VM_REG_SIZE; i++) {
    if (CmEnv.tmpRegState[i] == -1) {
      CmEnv.tmpRegState[i] = localvar;
      return i;
    }
  }
#else

  // for two address
  if (CmEnv.is_in_jmpcnctBlock) {
    for (int i = 0; i < VM_REG_SIZE; i++) {
      if (CmEnv.jmpcnctBlockRegState[i] == -1) {
        CmEnv.jmpcnctBlockRegState[i] = localvar;
        return i;
      }
    }
  } else {
    for (int i = 1; i < VM_REG_SIZE; i++) {
      if (CmEnv.tmpRegState[i] == -1) {
        CmEnv.tmpRegState[i] = localvar;
        return i;
      }
    }
  }

#endif

  printf("ERROR[CmEnv_get_newreg]: All registers run up.\n");
  exit(1);
}
void CmEnv_free_reg(int reg) {
#ifndef OPTIMISE_TWO_ADDRESS
  if (reg >= VM_OFFSET_LOCALVAR)
    CmEnv.tmpRegState[reg] = -1;
#else

  // for two address

  if (CmEnv.is_in_jmpcnctBlock) {
    CmEnv.jmpcnctBlockRegState[reg] = -1;
    return;
  } else {
    // if (reg >= VM_OFFSET_LOCALVAR)  // removed this comment out for v0.8.2-2
    //                            // so, regs for meta variables are not freed.
    if (reg >= VM_OFFSET_LOCALVAR)
      CmEnv.tmpRegState[reg] = -1;
  }
#endif
}
int CmEnv_Optimise_check_occurence_in_block(int localvar,
                                            int target_imcode_addr) {
  struct IMCode_tag *imcode;

  for (int i = target_imcode_addr + 1; i < IMCode_n; i++) {
    imcode = &IMCode[i];

    if (imcode->opcode == OP_BEGIN_BLOCK) {
      // This optimisation will last until OP_BEGIN_BLOCK occurs
      return 0;
    }

#ifdef OPTIMISE_TWO_ADDRESS
    if (CmEnv.is_in_jmpcnctBlock &&
        imcode->opcode == OP_BEGIN_JMPCNCT_BLOCK) {
      // This optimisation will last until OP_BEGIN_JMPCNCT_BLOCK occurs
      return 0;
    }
#endif

    switch (imcode->opcode) {
    case OP_LOAD_META:
      // LOAD_META src dest
      // If `localvar' occurs as `dest', it should not be freed.
      if (imcode->operand2 == localvar) {
        return 1;
      }

    case OP_LOAD:
    case OP_LOADI:
      // OP src1 dest
      if (imcode->operand1 == localvar) {
        return 1;
      }
      break;

    case OP_LOADP:
      // OP src1 port dest
      if (imcode->operand1 == localvar) {
        return 1;
      }
      break;

    case OP_LOADP_L:
    case OP_LOADP_R:
      // OP src1 port
      if (imcode->operand1 == localvar) {
        return 1;
      }
      break;

    case OP_MKNAME:
    case OP_MKGNAME:
    case OP_MKAGENT:
      break;

    case OP_PUSH:
    case OP_MYPUSH:
      if (imcode->operand1 == localvar) {
        return 1;
      }
      if (imcode->operand2 == localvar) {
        return 1;
      }
      break;

    case OP_JMPEQ0:
    case OP_JMPNEQ0:
    case OP_JMPCNCT_CONS:
    case OP_JMPCNCT:
    case OP_PUSHI:
      if (imcode->operand1 == localvar) {
        return 1;
      }
      break;

    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_LT:
    case OP_LE:
    case OP_EQ:
    case OP_NE:
    case OP_ADD:
    case OP_SUB:
      // OP src1 src2 dest
      if (imcode->operand1 == localvar) {
        return 1;
      }
      if (imcode->operand2 == localvar) {
        return 1;
      }
      break;

    case OP_ADDI:
    case OP_SUBI:
    case OP_EQI:
      // OP src1 $2 dest
      if (imcode->operand1 == localvar) {
        return 1;
      }
      break;

    case OP_LT_R0:
    case OP_LE_R0:
    case OP_EQ_R0:
    case OP_NE_R0:
      // OP src1 src2
      if (imcode->operand1 == localvar) {
        return 1;
      }
      if (imcode->operand2 == localvar) {
        return 1;
      }
      break;

    case OP_EQI_R0:
      // OP src1
      if (imcode->operand1 == localvar) {
        return 1;
      }
      break;
    }
  }

  return 0;
}
int CmEnv_Optimise_VMCode_CopyPropagation_LOADI(int target_imcode_addr) {

  struct IMCode_tag *imcode;
  long               load_i, load_to;

  load_i = IMCode[target_imcode_addr].operand1;
  load_to = IMCode[target_imcode_addr].operand2;

  for (int line_num = target_imcode_addr + 1; line_num < IMCode_n; line_num++) {
    imcode = &IMCode[line_num];

    if (imcode->opcode == OP_BEGIN_BLOCK) {
      // This optimisation will last until OP_BEGIN_BLOCK
      return 0;
    }
    if (imcode->opcode == OP_LABEL) {
      // This optimisation will last until OP_LABEL
      return 0;
    }
    //    if (imcode->opcode == OP_BEGIN_JMPCNCT_BLOCK) {
    //      // This optimisation will last until OP_BEGIN_BLOCK
    //      return 0;
    //    }

    switch (imcode->opcode) {

    case OP_PUSH:
      if (imcode->operand1 == load_to) {
        imcode->opcode = OP_PUSHI;
        imcode->operand1 = imcode->operand2;
        imcode->operand2 = load_i;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      if (imcode->operand2 == load_to) {
        imcode->opcode = OP_PUSHI;
        imcode->operand2 = load_i;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_ADD:
      // op src1 src2 dest
      if (imcode->operand1 == load_to) {
        if (load_i == 1) {
          // OP_INC src2 dest
          imcode->opcode = OP_INC;
          imcode->operand1 = imcode->operand2;
          imcode->operand2 = imcode->operand3;
        } else {
          imcode->opcode = OP_ADDI;
          imcode->operand1 = imcode->operand2;
          imcode->operand2 = load_i;
        }
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      if (imcode->operand2 == load_to) {
        if (load_i == 1) {
          // OP_INC src1 dest
          imcode->opcode = OP_INC;
          imcode->operand2 = imcode->operand3;
        } else {
          imcode->opcode = OP_ADDI;
          imcode->operand2 = load_i;
        }
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_SUB:
      // op src1 src2 dest
      if (imcode->operand2 == load_to) {
        if (load_i == 1) {
          // DEC src1 dest
          imcode->opcode = OP_DEC;
          imcode->operand2 = imcode->operand3;
        } else {
          imcode->opcode = OP_SUBI;
          imcode->operand2 = load_i;
        }
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;

    case OP_EQ:
    case OP_EQ_R0:
      // op src1 src2 dest
      if (imcode->operand1 == load_to) {
        if (imcode->opcode == OP_EQ) {
          imcode->opcode = OP_EQI;
        } else {
          imcode->opcode = OP_EQI_R0;
        }
        imcode->operand1 = imcode->operand2;
        imcode->operand2 = load_i;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      if (imcode->operand2 == load_to) {
        if (imcode->opcode == OP_EQ) {
          imcode->opcode = OP_EQI;
        } else {
          imcode->opcode = OP_EQI_R0;
        }
        imcode->operand2 = load_i;
        IMCode[target_imcode_addr].opcode = OP_DEAD_CODE;
        return 1;
      }
      break;
    }
  }

  return 0;
}

#ifdef OPTIMISE_TWO_ADDRESS
int CmEnv_assign_reg(int localvar, int reg) {
  // Enforce assignment of localvar to reg.
  // [Return values]
  //   When the reg has been already used,
  //     this function returns a new reg.
  //   Otherwise it returns -1.

  int *tmpRegState;

  if (CmEnv.is_in_jmpcnctBlock) {
    tmpRegState = CmEnv.jmpcnctBlockRegState;
  } else {
    tmpRegState = CmEnv.tmpRegState;
  }

  if (tmpRegState[reg] == localvar) {
    return -1;
  }

  if (tmpRegState[reg] < 0) {
    tmpRegState[reg] = localvar;
    return -1;
  }

  int orig_var = tmpRegState[reg];

  tmpRegState[reg] = localvar;

  int new_reg = CmEnv_get_newreg(orig_var);
  return new_reg;
}
#endif

void CmEnv_copy_VMCode(const int byte, void **source, void **target) {
  for (int i = 0; i < byte; i++) {
    target[i] = source[i];
  }
}
