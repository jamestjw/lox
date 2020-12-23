#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;
  // Instruction pointer: tracks the location of 
  // the instruction currently being executed (or
  // about to be executed).
  uint8_t* ip;

  Value stack[STACK_MAX];
  // stackTop points just past the last element in the array,
  // this way when the stack is empty stackTop would point
  // to the start of the array
  Value* stackTop;
} VM;

// Compiler reports static errors and VM detects runtime errors
typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

// Value stack operations
void push(Value value);
Value pop();

#endif

