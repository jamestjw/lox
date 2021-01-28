#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

// A callframe represents a single ongoing function call
typedef struct {
  // A pointer to the closure that contains the fn that is being called
  ObjClosure* closure;
  // Tracks the address that will be executed next, after returning
  // from a function call, the VM will jump to the ip of the caller's
  // CallFrame
  uint8_t* ip;
  // Points to the first slot in the VM's value stack that
  // this function can use
  Value *slots;
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  // Current height of the CallFrame stack, i.e. the number
  // of ongoing function calls
  int frameCount;

  Value stack[STACK_MAX];
  // stackTop points just past the last element in the array,
  // this way when the stack is empty stackTop would point
  // to the start of the array
  Value* stackTop;

  // Table of global variable names and values
  Table globals;

  // A hash table to keep track of all interned strings
  Table strings;
  
  // Interned string for the init keyword for classes
  ObjString* initString;

  // Head of the sorted linked list of open upvalues
  ObjUpvalue* openUpvalues;

  // Track amount of memory allocated
  size_t bytesAllocated;
  // Threshold of memory allocation before next GC is necessary
  size_t nextGC;

  // Head of a linked list of all objects allocated
  Obj* objects;

  // Gray stack is a list of objects marked for GC
  int grayCount;
  int grayCapacity;
  Obj** grayStack;
} VM;

// Compiler reports static errors and VM detects runtime errors
typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR,
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

// Value stack operations
void push(Value value);
Value pop();

#endif

