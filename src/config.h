#ifndef IMPLA_CONFIG_H
#define IMPLA_CONFIG_H

// Configurations  ---------------------------------------------------

// ------------------------------------------------
// Number of Agent Ports
// ------------------------------------------------
// MAX_PORT defines a number of ports of agents.
// Default is 5 and should be 2 or more.

#define MAX_PORT 5

// ----------------------------------------------
// Tail Recursion Optimisation
// ------------------------------------------------
// For experiments of the tail recursion optimisation.

// #define COUNT_CNCT    // count of execution of JMP_CNCT
// #define COUNT_MKAGENT // count of execution fo mkagent

//
// For the expandable ring buffer, the unit size HOOP_SIZE can be changed.
// We note that HOOP_SIZE must be two to power.
//
#ifdef EXPANDABLE_HEAP
// #define HOOP_SIZE (1 << 12)    // good for small heaps such as fib
#  define HOOP_SIZE (1 << 18) // good for large heaps such as msort-80000
#endif

// ------------------------------------------------
// RuleTable
// ------------------------------------------------
// There are two implementation for the rule table:
//   - Hashed linear table (default)
//   - Simple array table
// To use the hashed one, comment out the following RULETABLE_SIMPLE definition.

// #define RULETABLE_SIMPLE

// ------------------------------------------------
// Optimisation
// ------------------------------------------------
// Comment out definitions if not needed.

//
// Optimisation of the intermediate codes
//
//
//   - Assign registers as little as possible with expecting cache works.
//   - Copy propagation and Dead code elimination for LOAD are performed.
//   - Reg0 is used as a special one that stores results of comparison.
//   - Some combinations are rewritten.
//     For instance, `SUBI src $1 dest' becomes `DEC src dest'.
//
#define OPTIMISE_IMCODE

#ifdef OPTIMISE_IMCODE
// Furthermore optimisations on virtual machine codes:
// the following can work when the OPTIMISE_IMCODE is defined:

//
// Generate virtual machine codes with two-address notation
//
#  define OPTIMISE_TWO_ADDRESS

#  ifdef OPTIMISE_TWO_ADDRESS
// For Unary operator like INC, DEC (Unfinished)
// #define OPTIMISE_TWO_ADDRESS_UNARY
#  endif

#endif
// -------------------------------------------------

// ------------------------------------------------
// For developers
// ------------------------------------------------

// Show the computation process.
// #define DEBUG

// Show compiled codes for rules.
// #define DEBUG_MKRULE

// Show compiled codes for nets.
// #define DEBUG_NETS

// Show AST of an expression comes with compile errors.
// #define DEBUG_EXPR_COMPILE_ERROR

// Put memory usage of agents and names.
// #define VERBOSE_NODE_USE

// Put messages when hoops are expanded.
// #define VERBOSE_HOOP_EXPANSION

// Put messages when Eqstacks are expanded.
// #define VERBOSE_EQSTACK_EXPANSION

// Put message when TCO is enabled.
// #define VERBOSE_TCO

// Count the amount of interactions.
#define COUNT_INTERACTION

#endif // IMPLA_CONFIG_H
