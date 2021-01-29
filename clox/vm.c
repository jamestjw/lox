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
  vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;

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
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  initTable(&vm.globals);
  initTable(&vm.strings);

  // String copying involves allocation of objects, which can
  // trigger a GC, to avoid the GC reading initString before it is
  // fully initialized we do this.
  vm.initString = NULL; 
  vm.initString = copyString("init", 4);

  defineNative("clock", clockNative);
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  vm.initString = NULL;
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

static bool call(ObjClosure* closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
    return false;
  }
  
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  // Point the frame's ip to the beginning of the function's
  // bytecode
  frame->ip = closure->function->chunk.code;

  // First slot is reserved for the function itself, which
  // is why we need a -1 here
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch(OBJ_TYPE(callee)) {
      case OBJ_BOUND_METHOD: {
        ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
        // Ensure that in slot 0 of the locals in the stack frame,
        // we can find the receiver of the method call
        vm.stackTop[-argCount - 1] = bound->receiver;
        return call(bound->method, argCount);
      }
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        // Top of the stack consists of the args followed by the class,
        // after doing this and popping of all args, the new instance
        // will be at the top of the stack
        vm.stackTop[-argCount-1] = OBJ_VAL(newInstance(klass));

        // After allocating the instance in the runtime, we look for an 
        // init() method and call it if it exists
        Value initializer;
        if (tableGet(&klass->methods, vm.initString, &initializer)) {
          return call(AS_CLOSURE(initializer), argCount);
        } else if (argCount != 0) {
          runtimeError("Expected 0 arguments but got %d.", argCount);
          return false;
        }
        return true;
      }
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
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

static bool invokeFromClass(ObjClass* klass , ObjString* name, int argCount) {
  Value method;
  if(!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }
  return call(AS_CLOSURE(method), argCount);
}

// When this invoked, we expect the arguments to the function
// to be at the top of the stack followed by the instance
// on which this method is invoked from.
static bool invoke(ObjString* name, int argCount) {
  Value receiver = peek(argCount);
  
  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(receiver);

  // Handle the case that this isn't actually a method call, but a field
  // that contains a callable
  Value value;
  if (tableGet(&instance->fields, name, &value)) {
    // Set the field on the stack in place of the receiver
    // under the argument list
    vm.stackTop[-argCount - 1] = value;
    return callValue(value, argCount);
  }

  return invokeFromClass(instance->klass, name, argCount);
}

// Looks up the class for a method of a particular name, if
// it exists it will be pushed to the stack and the function
// returns true.
static bool bindMethod(ObjClass* klass, ObjString* name) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
  // Pop the instance and push the bound method
  pop();
  push(OBJ_VAL(bound));
  return true;
}

static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;

  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  // If there is an existing upvalue that is the one
  // we are searching for
  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue* createdUpvalue = newUpvalue(local);

  // Note how here upvalue is definitely in a slot lower than where
  // we want to place the newly created upvalue, and prevUpvalue should
  // be one slot above where we want to place our new value
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }
  return createdUpvalue;
}

// Given a pointer to a stack slot, this closes all open
// upvalues that point to that slot or above it on the stack
static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    // We simply make the Upvalue own the value of the closed upvalue
    upvalue->closed = *upvalue->location;
    // Here we quite simply point it to the copy of the value that is owned
    // by the upvalue
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

// Top of the stack is a closure followed by a class
static void defineMethod(ObjString* name) {
  Value method = peek(0);
  ObjClass *klass = AS_CLASS(peek(1));

  tableSet(&klass->methods, name, method);
  // Pop the closure, but we leave the class there
  // as there might be more methods
  pop();
}

// nil and false are falsey, everything else is truthy
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  // Peek instead of popping since reallocation of new
  // result string below could trigger GC on these strings
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);

  pop();
  pop();

  push(OBJ_VAL(result));
}

static InterpretResult run() {
  // Storing the current frame in a local variable will encourage
  // the C compiler to store this pointer in a register
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

  // Return a byte instruction and advance the instruction pointer
  #define READ_BYTE() (*frame->ip++)

  // Return the next instruction as a constant (and advance the IP)
  #define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])

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
    disassembleInstruction(&frame->closure->function->chunk, (int) (frame->ip - frame->closure->function->chunk.code));
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
      case OP_GET_UPVALUE: {
                             uint8_t slot = READ_BYTE();
                             push(*frame->closure->upvalues[slot]->location);
                             break;
                           }
      case OP_SET_UPVALUE: {
                             uint8_t slot = READ_BYTE();
                             *frame->closure->upvalues[slot]->location = peek(0);
                             break;
                           }
      case OP_GET_SUPER: {
                           ObjString* name = READ_STRING();
                           ObjClass* superclass = AS_CLASS(pop());
                           if (!bindMethod(superclass, name)) {
                             return INTERPRET_RUNTIME_ERROR;
                           }
                         }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GET_PROPERTY: {
        // Instance should be at the top of stack when processing this OP 
        if (!IS_INSTANCE(peek(0))) {
          runtimeError("Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(peek(0));
        ObjString* name = READ_STRING();

        Value value;
        if (tableGet(&instance->fields, name, &value)) {
          pop(); // Pop the instance
          push(value);
          break;
        }

        // If what we are trying to access is neither a property
        // or a method, we should throw an error
        if (!bindMethod(instance->klass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SET_PROPERTY: { 
        // Top of the stack is the value followed by the instance 
        if (!IS_INSTANCE(peek(1))) {
          runtimeError("Only instances have fields.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(peek(1));
        tableSet(&instance->fields, READ_STRING(), peek(0));

        Value value = pop();
        pop();

        // Setting properties is an expression
        push(value);
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
      case OP_INVOKE: { 
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        if (!invoke(method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        // On a successful function call, there will be a new frame
        // for the called function
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_SUPER_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        ObjClass* superclass = AS_CLASS(pop());
        if (!invokeFromClass(superclass, method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));

        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal) {
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            // To note here that while we are still in the middle of defining
            // this function, the current function in the frame is referring
            // to the one that encloses the function that we are defining
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_CLOSE_UPVALUE:
        closeUpvalues(vm.stackTop - 1);
        pop();
        break;
      case OP_RETURN: {
        // Function always returns a value, now that we intend to discard
        // the function's entire stack window, we pop the return value
        Value result = pop();

        // Close all remaining open upvalues owned by the returning function
        closeUpvalues(frame->slots);

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
      case OP_CLASS:
        push(OBJ_VAL(newClass(READ_STRING())));
        break;
      case OP_INHERIT: {
        Value superclass = peek(1);
        
        if (!IS_CLASS(superclass)) {
          runtimeError("Superclass must be a class.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjClass* subclass = AS_CLASS(peek(0));
        tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
        // Pop the subclass
        pop();
        break;
      }
      case OP_METHOD:
        defineMethod(READ_STRING());
        break;
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
  
  ObjClosure* closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  callValue(OBJ_VAL(closure), 0);
  
  return run();
}
