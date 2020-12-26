#include <stdarg.h>
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

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  // Doing a minus 1 is necessary since the IP points to
  // the next instruction to execute, i.e. at this point
  // we need to access the previous instruction
  size_t instruction = vm.ip - vm.chunk->code - 1;
  int line = vm.chunk->lines[instruction];
  fprintf(stderr, "[line %d] in script\n", line);

  resetStack();
}

void initVM() {
  resetStack();
}

void freeVM() {

}

// Value stack operations

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

// pop does not explicitly remove the value, it simply decrements the pointer
Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static Value peek(int distance) {
  return vm.stackTop[-1 -distance];
}

// nil and false are falsey, everything else is truthy
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run() {
  // Return a byte instruction and advance the instruction pointer
  #define READ_BYTE() (*vm.ip++)
  // Return the next instruction as a constant (and advance the IP)
  #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
  // Using a do while here allows here to execute multiple lines off
  // code while supporting the use of semicolons at the end
  #define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push (valueType(a op b)); \
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
      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
      // Arithmetic
      case OP_ADD: BINARY_OP(NUMBER_VAL, +); break;
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;
      case OP_NEGATE: 
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
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
  Chunk chunk;
  initChunk(&chunk);

  if(!compile(source, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_COMPILE_ERROR;
  }

  vm.chunk = &chunk;
  vm.ip = vm.chunk->code;

  InterpretResult result = run();

  freeChunk(&chunk);
  return INTERPRET_OK;
}
