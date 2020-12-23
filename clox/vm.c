#include <stdio.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

// Defining a static VM as a global variable is not necessarily the best choice
// It does however save us the need to pass a pointer to a VM all the time.
VM vm;

static void resetStack() {
  vm.stackTop = vm.stack;
}

void initVM() {
  resetStack();
}

void freeVM() {

}

static InterpretResult run() {
  // Return a byte instruction and advance the instruction pointer
  #define READ_BYTE() (*vm.ip++)
  // Return the next instruction as a constant (and advance the IP)
  #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
  // Using a do while here allows here to execute multiple lines off
  // code while supporting the use of semicolons at the end
  #define BINARY_OP(op) \
    do { \
      double b = pop(); \
      double a = pop(); \
      push (a op b); \
    } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    // Print contents of the value stack before executing each instruction
    // starting from the bottom of the stack (i.e. the first value that was 
    // added will be printed first)
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");

    // By doing pointer arithmetic between the ptr to the next instruction
    // and the start of the instruction array, we get the offset of the 
    // next instruction to be executed
    disassembleInstruction(vm.chunk, (int) (vm.ip - vm.chunk->code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      // We consume the constant, increment the IP and push the value
      // to the value stack
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      // Arithmetic
      case OP_ADD: BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE: BINARY_OP(/); break;
      case OP_NEGATE: push(-pop()); break;
      case OP_RETURN: {
        // TODO: Complete this implementation, for now we will pop the top value
        // off the value stack and print it.
        printValue(pop());
        printf("\n");
        return INTERPRET_OK;
      }
    }
  }
  #undef READ_BYTE
  #undef READ_CONSTANT
  #undef BINARY_OP
}

InterpretResult interpret(const char* source) {
  compile(source);
  return INTERPRET_OK;
}

// Value stack operations

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

// pop does not explicitly remove the value, it simply decrements the pointer
Value pop(Value value) {
  vm.stackTop--;
  return *vm.stackTop;
}
