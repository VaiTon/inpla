#include "imcode.h"

#include <stdio.h>
#include <stdlib.h>

static void IMCODE_OVERFLOW_CHECK() {
  if (IMCode_n > MAX_IMCODE_SEQUENCE) {
    fprintf(stderr, "IMCODE overflow: %d\n", IMCode_n);
    exit(EXIT_FAILURE);
  }
}

int IMCode_n;
struct IMCode_tag IMCode[MAX_IMCODE_SEQUENCE];

void IMCode_init(void) { IMCode_n = 0; }

void IMCode_genCode0(int opcode) {
  IMCode[IMCode_n++].opcode = opcode;
  IMCODE_OVERFLOW_CHECK();
}

void IMCode_genCode1(int opcode, long operand1) {
  IMCode[IMCode_n].operand1 = operand1;
  IMCode[IMCode_n++].opcode = opcode;
  IMCODE_OVERFLOW_CHECK();
}

void IMCode_genCode2(int opcode, long operand1, long operand2) {
  IMCode[IMCode_n].operand1 = operand1;
  IMCode[IMCode_n].operand2 = operand2;
  IMCode[IMCode_n++].opcode = opcode;
  IMCODE_OVERFLOW_CHECK();
}

void IMCode_genCode3(int opcode, long operand1, long operand2, long operand3) {
  IMCode[IMCode_n].operand1 = operand1;
  IMCode[IMCode_n].operand2 = operand2;
  IMCode[IMCode_n].operand3 = operand3;
  IMCode[IMCode_n++].opcode = opcode;
  IMCODE_OVERFLOW_CHECK();
}

void IMCode_genCode4(int opcode, long operand1, long operand2, long operand3,
                     long operand4) {
  IMCode[IMCode_n].operand1 = operand1;
  IMCode[IMCode_n].operand2 = operand2;
  IMCode[IMCode_n].operand3 = operand3;
  IMCode[IMCode_n].operand4 = operand4;
  IMCode[IMCode_n++].opcode = opcode;
  IMCODE_OVERFLOW_CHECK();
}

void IMCode_genCode5(int opcode, long operand1, long operand2, long operand3,
                     long operand4, long operand5) {
  IMCode[IMCode_n].operand1 = operand1;
  IMCode[IMCode_n].operand2 = operand2;
  IMCode[IMCode_n].operand3 = operand3;
  IMCode[IMCode_n].operand4 = operand4;
  IMCode[IMCode_n].operand5 = operand5;
  IMCode[IMCode_n++].opcode = opcode;
  IMCODE_OVERFLOW_CHECK();
}

void IMCode_genCode6(int opcode, long operand1, long operand2, long operand3,
                     long operand4, long operand5, long operand6) {
  IMCode[IMCode_n].operand1 = operand1;
  IMCode[IMCode_n].operand2 = operand2;
  IMCode[IMCode_n].operand3 = operand3;
  IMCode[IMCode_n].operand4 = operand4;
  IMCode[IMCode_n].operand5 = operand5;
  IMCode[IMCode_n].operand6 = operand6;
  IMCode[IMCode_n++].opcode = opcode;
  IMCODE_OVERFLOW_CHECK();
}

void IMCode_genCode7(int opcode, long operand1, long operand2, long operand3,
                     long operand4, long operand5, long operand6,
                     long operand7) {
  IMCode[IMCode_n].operand1 = operand1;
  IMCode[IMCode_n].operand2 = operand2;
  IMCode[IMCode_n].operand3 = operand3;
  IMCode[IMCode_n].operand4 = operand4;
  IMCode[IMCode_n].operand5 = operand5;
  IMCode[IMCode_n].operand6 = operand6;
  IMCode[IMCode_n].operand7 = operand7;
  IMCode[IMCode_n++].opcode = opcode;
  IMCODE_OVERFLOW_CHECK();
}