#ifndef clox_compiler_h
#define clox_compiler_h

#include "vm.h"

// Returns a boolean to indicate the whether the
// compilation was successful
bool compile(const char* source, Chunk* chunk);

#endif

