#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

// Defines the type of the bytecode, e.g. add, subtract,
// variable lookup etc
typedef enum {
  OP_CONSTANT,
  // Special literals
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  // Negates a boolean
  OP_NOT,
  // Negates a numerical value
  OP_NEGATE,
  // Prints a value
  OP_PRINT,
  // Return from current function
  OP_RETURN,

  // Comparison operators
  OP_GREATER,
  OP_LESS,
  OP_DEFINE_GLOBAL,
  OP_GET_LOCAL,
  OP_GET_GLOBAL,
  OP_SET_LOCAL,
  OP_SET_GLOBAL,
  OP_EQUAL,

  // Arithmetic,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
} OpCode;

typedef struct {
  // Array of byte-sized instructions.
  //
  // uint8_t is the equivalent of an unsigned integer 
  // of length 8 bits, i.e. equivalent to a byte
  uint8_t* code;
  // Number of instructions stored
  int count;
  // Capacity allocated for this array of instructions
  int capacity;

  // An integer array that parallels each byte code to track its corresponding line number
  int* lines;
  ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);

// Returns the offset in which the value was written
// in the constants array
int addConstant(Chunk* chunk, Value value);

#endif

