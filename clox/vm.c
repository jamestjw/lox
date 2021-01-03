#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

// Defining a static VM as a global variable is not necessarily the best choice
// It does however save us the need to pass a pointer to a VM all the time.
VM vm;

// Elapsed time since the program started running
static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->function;

    // -1 since IP points to the next instruction to execute
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ",
            function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}

static void defineNative(const char* name, NativeFn function) {
  // We store things on the stack so that the GC knows that
  // we are not done with them
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

void initVM() {
  resetStack();
  vm.objects = NULL;
  initTable(&vm.globals);
  initTable(&vm.strings);

  defineNative("clock", clockNative);
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  freeObjects();
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

static bool call(ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    runtimeError("Expected %d arguments but got %d.", function->arity, argCount);
    return false;
  }
  
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  // Point the frame's ip to the beginning of the function's
  // bytecode
  frame->ip = function->chunk.code;

  // First slot is reserved for the function itself, which
  // is why we need a -1 here
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch(OBJ_TYPE(callee)) {
      case OBJ_FUNCTION:
        return call(AS_FUNCTION(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        Value result = native(argCount, vm.stackTop - argCount);
        // Note that the function object itself will be the first value
        // in the stack frame, which is why we need the +1 here
        vm.stackTop -= argCount + 1;
        push(result);
        return true;
      }
      default:
          // Non-callable object
          break;
    }
  }

  runtimeError("Can only call functions and classes.");
  return false;
}

// nil and false are falsey, everything else is truthy
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  push(OBJ_VAL(result));
}

static InterpretResult run() {
  // Storing the current frame in a local variable will encourage
  // the C compiler to store this pointer in a register
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

  // Return a byte instruction and advance the instruction pointer
  #define READ_BYTE() (*frame->ip++)

  // Return the next instruction as a constant (and advance the IP)
  #define READ_CONSTANT() (frame->function->chunk.constants.values[READ_BYTE()])

  // Takes the next two bytes and constructs a 16bit unsigned int
  #define READ_SHORT() \
    (frame->ip += 2, \
     (uint16_t) ((frame->ip[-2] << 8 | frame->ip[-1])))

  #define READ_STRING() AS_STRING(READ_CONSTANT())

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
    disassembleInstruction(&frame->function->chunk, (int) (frame->ip - frame->function->chunk.code));
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
      case OP_POP: pop(); break;
      case OP_GET_LOCAL: {
        // Might seem a little silly to push the same value
        // that can be found somewhere below in the stack to
        // the same stack again, but our instructions only know
        // how to access values at the top of the stack
        uint8_t slot = READ_BYTE();
        // frame->slots is actually a pointer with a given offset to vm.stack,
        // i.e. the beginning of the stack that this function can access, and slot
        // is an offset relative to that beginning
        push(frame->slots[slot]);
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, name, peek(0));
        pop();
        break;
      }
      // Setting a variable doesn't pop the value off the stack
      // since assignment is an expression (it could be nested
      // within some larger expression)
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        // If the key didn't already exist in the globals hash table,
        // throw a runtime error since we do not support implicit
        // variable declaration
        if (tableSet(&vm.globals, name, peek(0))) {
          tableDelete(&vm.globals, name);
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        // We do not pop the value since assigment is an
        // expression (i.e. it produces a value always)
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
      // Arithmetic
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          // We do not use BINARY_OP to save some computation
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or two strings");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      } 
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
      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
      case OP_CALL :{
        int argCount = READ_BYTE();
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        // On a successful function call, there will be a new frame
        // for the called function
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_RETURN: {
        // Function always returns a value, now that we intend to discard
        // the function's entire stack window, we pop the return value
        Value result = pop();

        vm.frameCount--;
        // If we are done interpreting everything
        if (vm.frameCount == 0) {
          pop();
          return INTERPRET_OK;
        }

        // Discard all slots that the callee was using for its parameters
        vm.stackTop = frame->slots;
        // Push the return value to the top of the stack
        push(result);
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
    }
  }
  #undef READ_BYTE
  #undef READ_SHORT
  #undef READ_CONSTANT
  #undef READ_STRING
  #undef BINARY_OP
}

InterpretResult interpret(const char* source) {
  ObjFunction* function = compile(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  // This is why the compiler reserves the first local slot for its
  // internal use, i.e. to store the implicit top level function
  push(OBJ_VAL(function));
  callValue(OBJ_VAL(function), 0);
  
  return run();
}
