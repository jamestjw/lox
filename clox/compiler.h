#ifndef clox_compiler_h
#define clox_compiler_h

#include "vm.h"

// Returns a pointer to a ObjFunction if compilation was successful,
// returns a null ptr otherwise (preventing the VM from trying to execute
// a function with possibly invalid bytecode)
ObjFunction* compile(const char* source);
void markCompilerRoots();

#endif

