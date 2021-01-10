#include <stdlib.h>

#include "memory.h"
#include "vm.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);

  // If for some reason we are out of memory
  if (result == NULL) exit(1);

  return result;
}

static void freeObject(Obj* object) {
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

void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
}
