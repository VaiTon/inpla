#ifndef INPLA_CMENV_H
#define INPLA_CMENV_H

#include "imcode.h"
#include "vm.h"

typedef enum {
  NB_NAME = 0,
  NB_META_L,
  NB_META_R,
  NB_INTVAR,
  NB_WILDCARD,
} NB_TYPE;

// NBIND 数は meta数(MAX_PORT*2) + 一つの rule（や eqlist） における
// 最大name出現数(100)
#define MAX_NBIND MAX_PORT * 2 + 100
typedef struct {
  char *name;
  int   reg;
  int   refnum; //  0: global name (thus created only),
                // >0: local name
                // (for the bind names: should be 1)
                // (for the int names: not paticular)
  NB_TYPE type; // NB_NAME, NB_META_L (or R), NB_INTVAR, NB_WILDCARD
} NameBind;

typedef struct {

  // Management table for local and global names
  NameBind bind[MAX_NBIND];
  int      bindPtr;           // its index
  int      bindPtr_metanames; // The max index that stores info of meta names
                              // (default: -1)

  // Index for local and global names in Regs
  int localNamePtr; // It starts from VM_OFFSET_LOCALVAR

  // For rule agents
  int idL, idR;               // ids of ruleAgentL and ruleAgentR
  int reg_agentL, reg_agentR; // Beginning reg nums for args of
                              // ruleAgentL, ruleAgentR
  int annotateL, annotateR;   // `Annotation properties' such as (*L) (*R) (int)

  // for compilation to VMCode
  int label;                    // labels
  int tmpRegState[VM_REG_SIZE]; // register assignments
                                // store localvar numbers. -1 is no assignment.

#ifdef OPTIMISE_TWO_ADDRESS
  int jmpcnctBlockRegState[VM_REG_SIZE];
  int is_in_jmpcnctBlock;
#endif

  // the amount of compilation error
  int count_compilation_errors;

  // warning output for x~y and x~i
  // where x and y are int modified names, and i is an integer such as 1
  int put_warning_for_cnct_property;

  // flag: output compiled codes
  int put_compiled_codes;

  // flag: tail-call-optimisation
  int tco;

} CmEnvironment;

// Annotation states
#define ANNOTATE_NOTHING      0 // The rule agent is not reused.
#define ANNOTATE_REUSE        1 // Annotation (*L), (*R) is specified.
#define ANNOTATE_INT_MODIFIER 2 // (int i), therefore it must not be freed.
#define ANNOTATE_WILDCARD     3 // Wildcard Agent, therefore it must not be freed.
#define ANNOTATE_TCO          4 // Annotated and reused as TCO.

extern CmEnvironment CmEnv;

void CmEnv_copy_VMCode(int byte, void **source, void **target);
int  CmEnv_generate_VMCode(void **code);

void CmEnv_clear_register_assignment_table_all(void);
void CmEnv_clear_localnamePtr(void);
void CmEnv_clear_bind(int preserve_idx);
void CmEnv_clear_all(void);
void CmEnv_clear_keeping_rule_properties(void);

#ifdef OPTIMISE_TWO_ADDRESS
void CmEnv_clear_jmpcnctBlock_assignment_table_all(void);
void CmEnv_stack_assignment_table(void);
#endif

int CmEnv_get_newlabel(void);
int CmEnv_get_newreg(int localvar);
int CmEnv_gettype_forname(char *key, NB_TYPE *type);

int  CmEnv_set_symbol_as_name(char *name);
void CmEnv_set_symbol_as_meta(char *name, int reg, NB_TYPE type);
int  CmEnv_set_as_INTVAR(char *name);

int CmEnv_find_var(char *key);
int CmEnv_newvar(void);

int CmEnv_check_linearity_in_rule(void);
int CmEnv_check_name_reference_times(void);

int CmEnv_using_reg_with_nothing_info(int localvar);
int CmEnv_using_reg(int localvar);

void CmEnv_retrieve_MKGNAME(void);

void CmEnv_free_reg(int reg);

#ifdef OPTIMISE_TWO_ADDRESS
int CmEnv_assign_reg(int localvar, int reg);
#endif

int CmEnv_Optimise_VMCode_CopyPropagation(int target_imcode_addr);
int CmEnv_Optimise_VMCode_CopyPropagation_LOADI(int target_imcode_addr);
int CmEnv_Optimise_check_occurence_in_block(int localvar,
                                            int target_imcode_addr);
int CmEnv_Optimise_VMCode_CopyPropagation_LOADI(int target_imcode_addr);

#endif // INPLA_CMENV_H
