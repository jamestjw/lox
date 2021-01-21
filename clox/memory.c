#include <stdlib.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;

  // When asking for memory, trigger GC
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif

    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
  }

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);

  // If for some reason we are out of memory
  if (result == NULL) exit(1);

  return result;
}

void markObject(Obj* object) {
  if (object == NULL) return;
  // Object graphs are not acyclic, prevent infinite loops
  if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  object->isMarked = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    // Using system realloc since we do not want this to trigger
    // a new GC
    vm.grayStack = realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);

    // To be more robust, we can allocate a “rainy day fund” block 
    // of memory when we start the VM. If the gray stack allocation 
    // fails, we free the rainy day block and try again. That may 
    // give us enough wiggle room on the heap to create the gray stack, 
    // finish the GC, and free up more memory.
    if (vm.grayStack == NULL) exit(1);
  }

  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
  // Skip values that do not require heap allocation
  if(!IS_OBJ(value)) return;
  markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}

static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject((Obj*)closure->upvalues[i]);
      }
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markArray(&function->chunk.constants);
      break;
    }
    case OBJ_UPVALUE:
      markValue(((ObjUpvalue*)object)->closed);
      break;
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}

static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  switch (object->type) {
    case OBJ_CLOSURE: {
      // Closure does not own the function (multiple closures might
      // reference the same function) and hence we do not clean up
      // the function obj here
      ObjClosure* closure = (ObjClosure*) object;
      // Although closure does not own the upvalues, it owns the array
      // of pointers and we need to free this
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      // +1 to take into account the null termination char
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
    case OBJ_FUNCTION: {
      // Note how we skip freeing the name object since
      // the garbage collector will eventually handle it
      // for us
      ObjFunction* function = (ObjFunction*) object;
      freeChunk(&function->chunk);
      FREE(OBJ_FUNCTION, object);
      break;
    }
    case OBJ_NATIVE: {
      FREE(ObjNative, object);
      break;
    }
    case OBJ_UPVALUE: {
      FREE(ObjUpvalue, object);
      break;
    }
  }
}

static void markRoots() {
  // Walk the stack of the VM
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  // Mark the closures of each call frame
  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }

  // Mark list of open upvalues
  for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }

  // Mark all variables that live in the VM's hash table
  markTable(&vm.globals);

  markCompilerRoots();
}

void traceReferences() {
  while(vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

// Sweep through all objects, and freeing those that are unmarked
// while removing them from the linked list of objects
static void sweep() {
  Obj* previous = NULL;
  Obj* object = vm.objects;
  while (object != NULL) {
    if (object->isMarked) {
      // The mark has served its purpose so we reset it,
      // in anticipation of the next GC
      object->isMarked = false;
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  markRoots();
  traceReferences();

  // Before sweeping strings, we first clear them from the 
  // string table to prevent dangling references
  tableRemoveWhite(&vm.strings);
  sweep();

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %ld bytes (from %ld to %ld) next at %ld\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,
         vm.nextGC);
#endif
}

void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }

  free(vm.grayStack);
}
