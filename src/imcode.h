#ifndef INPLA_IMCODE_H
#define INPLA_IMCODE_H

// http://www.hpcs.cs.tsukuba.ac.jp/~msato/lecture-note/comp-lecture/note10.html
#define MAX_IMCODE_SEQUENCE 1024

struct IMCode_tag {
  int opcode;
  long operand1, operand2, operand3, operand4, operand5, operand6, operand7;
};

extern struct IMCode_tag IMCode[MAX_IMCODE_SEQUENCE];
extern int IMCode_n;

void IMCode_init(void);

void IMCode_genCode0(int opcode);
void IMCode_genCode1(int opcode, long operand1);
void IMCode_genCode2(int opcode, long operand1, long operand2);
void IMCode_genCode3(int opcode, long operand1, long operand2, long operand3);
void IMCode_genCode4(int opcode, long operand1, long operand2, long operand3,
                     long operand4);
void IMCode_genCode5(int opcode, long operand1, long operand2, long operand3,
                     long operand4, long operand5);
void IMCode_genCode6(int opcode, long operand1, long operand2, long operand3,
                     long operand4, long operand5, long operand6);
void IMCode_genCode7(int opcode, long operand1, long operand2, long operand3,
                     long operand4, long operand5, long operand6,
                     long operand7);

#endif // INPLA_IMCODE_H
