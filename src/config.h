#ifndef IMPLA_CONFIG_H
#define IMPLA_CONFIG_H

// Configurations  ---------------------------------------------------
#ifndef CONFIG_H_
#define CONFIG_H_

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

// ------------------------------------------------
// AST Heap
// ------------------------------------------------
// MAX_AST_HEAP defines the heap size of AST.
// Increase this value if the parser runs out of heap space.
// Default: 100000
#define MAX_AST_HEAP 100000

// Optional counters (uncomment to enable)
// #define COUNT_CNCT    // count of execution of JMP_CNCT
// #define COUNT_MKAGENT // count of execution of mkagent

// ------------------------------------------------
// Heaps
// ------------------------------------------------
// MAX_VMCODE_SEQUENCE defines the maximum number of VM codes
// for each rule. This value limits the code size stored in the RuleTable.
// Default: 1024.
#define MAX_VMCODE_SEQUENCE 1024

// MAX_EXEC_VMCODE_SEQUENCE defines the maximum number of VM codes
// for the top-level execution.
// This buffer is dynamically allocated at the start of the top-level execution
// and de-allocated immediately after the compilation and execution finish.
// Default: 1000000.
#define MAX_EXEC_VMCODE_SEQUENCE 1000000

// ------------------------------------------------
// IMCode Sequence
// ------------------------------------------------
// MAX_IMCODE_SEQUENCE defines the maximum line number of intermediate codes.
// This buffer is statically allocated.
// Increase this value if the compiler runs out of heap space.
// Default: 1024.
#define MAX_IMCODE_SEQUENCE 1024

// ------------------------------------------------
// Enable Inpla Built-in Agent Operations
// ------------------------------------------------
// Comment out the below definition to run in Pure Interaction Nets mode.
#define INPLA_USE_BUILTINS

// ------------------------------------------------
// Heap Allocation Strategies
// ------------------------------------------------
// Select the memory management strategy for agent and name heaps:
//
//   - Fixed-size ring buffers
//       Heap size is strictly bounded.
//       The size is specified by the execution option -m
//
//   - Expandable ring buffers
//       New buffer chunks are automatically allocated and linked when full.
//
//   - Flexibly expandable ring buffer (DEFAULT)
//       Initial size and the expansion ratio are configurable
//       via the '-Xms' (initial) and '-Xmt' (multiplier) runtime options.
//
// Please uncomment exactly ONE of the three definitions below.

// #define FIXED_HEAP
// #define EXPANDABLE_HEAP
#define FLEX_EXPANDABLE_HEAP

// For the expandable ring buffer, the unit size HOOP_SIZE can be changed.
// HOOP_SIZE must be a power of two. Choose a smaller value for many small
// allocations (e.g. fib) and a larger value for few large allocations
// (e.g. msort-80000).
#ifdef EXPANDABLE_HEAP
// Example sizes:
// #define HOOP_SIZE (1 << 12)    // good for small heaps such as fib
// #define HOOP_SIZE (1 << 18)    // good for large heaps such as msort-80000
#  ifndef HOOP_SIZE
#    define HOOP_SIZE (1 << 18)
#  endif
#endif

#ifdef FLEX_EXPANDABLE_HEAP
// The maximum limitation for heap expansion.
// This helps prevent segmentation faults caused by out-of-memory.
// Adjust this value according to your environment.
#define MAX_HOOP_SIZE 50000000
#endif

// ------------------------------------------------
// Rule Table Implementation
// ------------------------------------------------
// There are two implementations available for the rule table:
//
//   - Hashed linear table (DEFAULT)
//   - Simple array table
//
// To use the default hashed table,
// leave the following RULETABLE_SIMPLE definition commented out.

// #define RULETABLE_SIMPLE

// ------------------------------------------------
// Optimisations
// ------------------------------------------------
// Comment out the definitions if not needed.
//
// Optimisation of the intermediate codes
//
//   - Minimise register assignments to improve cache locality.
//   - Perform copy propagation and dead code elimination for LOAD instructions.
//   - Use Reg0 as a special register to store comparison results.
//   - Apply peephole rewrites for specific instruction patterns.
//     For instance, `SUBI src $1 dest' becomes `DEC src dest'.
#define OPTIMISE_IMCODE

#ifdef OPTIMISE_IMCODE
// Further optimisations on virtual machine codes:
// the following can work when OPTIMISE_IMCODE is defined:
//
// Generate virtual machine codes with two-address notation
#  define OPTIMISE_TWO_ADDRESS

#  ifdef OPTIMISE_TWO_ADDRESS
// For Unary operator like INC, DEC (Unfinished)
// #define OPTIMISE_TWO_ADDRESS_UNARY
#  endif

#endif

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

#endif
#endif // IMPLA_CONFIG_H