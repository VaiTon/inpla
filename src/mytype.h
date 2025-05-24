#ifndef _TYPE_
#define _TYPE_

#include "config.h"

typedef unsigned int IDTYPE;
typedef unsigned long VALUE;

typedef struct {
  IDTYPE id;
} Basic;

// #define MAX_PORT 5

typedef struct {
  Basic basic;

#ifndef THREAD
  VALUE port;
#else
  volatile VALUE port;
#endif
} Name;

typedef struct {
  Basic basic;

#ifndef THREAD
  VALUE port[MAX_PORT];
#else
  volatile VALUE port[MAX_PORT];
#endif
} Agent;

// Equation
typedef struct EQ_tag {
  VALUE l, r;
} EQ;

typedef struct EQList_tag {
  EQ eq;
  struct EQList_tag *next;
} EQList;

#define FIXNUM_FLAG  0x01
#define INT2FIX(i)   ((VALUE)(((long)(i) << 1) | FIXNUM_FLAG))
#define FIX2INT(i)   ((long)(i) >> 1)
#define IS_FIXNUM(i) ((VALUE)(i) & FIXNUM_FLAG)

#define AGENT(a) ((Agent *)(a))
#define BASIC(a) ((Basic *)(a))
#define NAME(a)  ((Name *)(a))

#endif
