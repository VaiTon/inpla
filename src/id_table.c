#include "id_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IdTableT IdTable[IDTABLE_SIZE];

static int NextAgentId, NextGnameId;

void IdTable_init() {

  int i;
  NextAgentId = START_ID_OF_USER_AGENT - 1;
  NextGnameId = START_ID_OF_GNAME - 1;
  // 0 : ID_INT(used in expressions)
  // 1 .. START_ID_OF_USER_AGENT-1 : built-in agents
  // START_ID_OF_USER_AGENT .. NUM_AGENTS-1: user defined agents
  // NUM_AGENTS(=ID_NAME) .. : names

  for (i = 0; i <= END_ID_OF_AGENT; i++) {
    IdTable[i].name = NULL;
    IdTable[i].aux.arity = -1; // means ERROR
  }
  for (i = START_ID_OF_GNAME; i < IDTABLE_SIZE; i++) {
    IdTable[i].name = NULL;
    IdTable[i].aux.heap = (VALUE)NULL;
  }

  // built-in agent
  IdTable[ID_TUPLE0].aux.arity = 0;
  IdTable[ID_TUPLE1].aux.arity = 1;
  IdTable[ID_TUPLE2].aux.arity = 2;
  IdTable[ID_TUPLE3].aux.arity = 3;
  IdTable[ID_TUPLE4].aux.arity = 4;
  IdTable[ID_TUPLE5].aux.arity = 5;
  IdTable[ID_NIL].aux.arity = 0;
  IdTable[ID_CONS].aux.arity = 2;
  IdTable[ID_INTAGENT].aux.arity = 1;

  IdTable[ID_APPEND].aux.arity = 2;
  IdTable[ID_ZIP].aux.arity = 2;
  IdTable[ID_ZIPC].aux.arity = 2;
  IdTable[ID_MERGER].aux.arity = 1;
  IdTable[ID_MERGER_P].aux.arity = 1;
  IdTable[ID_ADD].aux.arity = 2;
  IdTable[ID_ADD2].aux.arity = 2;
  IdTable[ID_SUB].aux.arity = 2;
  IdTable[ID_SUB2].aux.arity = 2;
  IdTable[ID_MUL].aux.arity = 2;
  IdTable[ID_MUL2].aux.arity = 2;
  IdTable[ID_DIV].aux.arity = 2;
  IdTable[ID_DIV2].aux.arity = 2;
  IdTable[ID_MOD].aux.arity = 2;
  IdTable[ID_MOD2].aux.arity = 2;
  IdTable[ID_PERCENT].aux.arity = 1;
  IdTable[ID_MAP].aux.arity = 2;

  IdTable[ID_ERASER].aux.arity = 0;
  IdTable[ID_DUP].aux.arity = 2;

  IdTable[ID_INT].name = "Int";
  IdTable[ID_TUPLE0].name = "Tuple0";
  IdTable[ID_TUPLE1].name = "Tuple1";
  IdTable[ID_TUPLE2].name = "Tuple2";
  IdTable[ID_TUPLE3].name = "Tuple3";
  IdTable[ID_TUPLE4].name = "Tuple4";
  IdTable[ID_TUPLE5].name = "Tuple5";
  IdTable[ID_NIL].name = "[]";
  IdTable[ID_CONS].name = "Cons";
  IdTable[ID_INTAGENT].name = "Int";
  IdTable[ID_WILDCARD].name = "Wildcard";

  IdTable[ID_APPEND].name = "Append";
  IdTable[ID_ZIP].name = "Zip";
  IdTable[ID_ZIPC].name = "ZipC";
  IdTable[ID_MERGER].name = "Merger";
  IdTable[ID_MERGER_P].name = "_MergerP";
  IdTable[ID_ADD].name = "Add";
  IdTable[ID_ADD2].name = "_Add";
  IdTable[ID_SUB].name = "Sub";
  IdTable[ID_SUB2].name = "_Sub";
  IdTable[ID_MUL].name = "Mul";
  IdTable[ID_MUL2].name = "_Mul";
  IdTable[ID_DIV].name = "Div";
  IdTable[ID_DIV2].name = "_Div";
  IdTable[ID_MOD].name = "Mod";
  IdTable[ID_MOD2].name = "_Mod";
  IdTable[ID_PERCENT].name = "%";
  IdTable[ID_MAP].name = "Map";

  IdTable[ID_ERASER].name = "Eraser";
  IdTable[ID_DUP].name = "Dup";
}

int IdTable_getid_builtin_funcAgent(Ast *agent) {
  // returns -1 if the agent is not built-in.

  //  puts("!!");
  int id = -1;

  if (agent->id != AST_AGENT && agent->id != AST_PERCENT) {
    return -1;
  }

  if (strcmp((char *)agent->left->sym, "Add") == 0) {
    id = ID_ADD;
  } else if (strcmp((char *)agent->left->sym, "Sub") == 0) {
    id = ID_SUB;
  } else if (strcmp((char *)agent->left->sym, "Mul") == 0) {
    id = ID_MUL;
  } else if (strcmp((char *)agent->left->sym, "Div") == 0) {
    id = ID_DIV;
  } else if (strcmp((char *)agent->left->sym, "Mod") == 0) {
    id = ID_MOD;
  } else if (strcmp((char *)agent->left->sym, "Append") == 0) {
    id = ID_APPEND;
  } else if (strcmp((char *)agent->left->sym, "Zip") == 0) {
    id = ID_ZIP;
  } else if (strcmp((char *)agent->left->sym, "Map") == 0) {
    id = ID_MAP;
  } else if (strcmp((char *)agent->left->sym, "Int") == 0) {
    id = ID_INTAGENT;
  } else if (strcmp((char *)agent->left->sym, "Merger") == 0) {
    id = ID_MERGER;
  } else if (strcmp((char *)agent->left->sym, "Eraser") == 0) {
    id = ID_ERASER;
  } else if (strcmp((char *)agent->left->sym, "Dup") == 0) {
    id = ID_DUP;
  }

  return id;
}

void IdTable_set_name(int id, char *symname) {
  if (id > IDTABLE_SIZE) {
    printf("Error: The given id %d was beyond of the size of IdTable (%d)\n",
           id, IDTABLE_SIZE);
    exit(-1);
  }
  IdTable[id].name = symname;
}

char *IdTable_get_name(int id) { return IdTable[id].name; }

void IdTable_set_arity(int id, int arity) {
  if (IdTable[id].aux.arity == -1 || IdTable[id].aux.arity == arity) {
    IdTable[id].aux.arity = arity;
  } else {
    printf("Warning: The agent `%s' has been previously defined of arity %d, "
           "but is now used of arity %d.\n",
           IdTable[id].name, IdTable[id].aux.arity, arity);
    IdTable[id].aux.arity = arity;
  }
}

int IdTable_get_arity(const int id) { return IdTable[id].aux.arity; }

void IdTable_set_heap(const unsigned long id, const VALUE heap) {
  if (id >= IDTABLE_SIZE) {
    fprintf(stderr,
            "Error: The given id %lu was beyond of the size of IdTable (%d)\n",
            id, IDTABLE_SIZE);
    exit(EXIT_FAILURE);
  }
  IdTable[id].aux.heap = heap;
}

VALUE IdTable_get_heap(unsigned long id) { return IdTable[id].aux.heap; }

int IdTable_new_agentid() {
  NextAgentId++;
  if (NextAgentId > END_ID_OF_USER_AGENT) {
    printf("ERROR: The number of agents exceeded the limitation size (%d).\n",
           END_ID_OF_USER_AGENT);
    exit(-1);
  }
  return NextAgentId;
}

int IdTable_new_gnameid() {
  NextGnameId++;
  if (NextGnameId < IDTABLE_SIZE) {
    return NextGnameId;
  } else {

    printf(
        "ERROR: The total number of names exceeded the size of IDTABLE (%d)\n",
        IDTABLE_SIZE);
    exit(-1);
  }
}
