#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"

#define GROW_CAPACITY(capacity) \
  ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
  (type*)reallocate(pointer, sizeof(type) * (oldCount), \
      sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
  reallocate(pointer, sizeof(type) * (oldCount), 0)

// If the old size is zero and the new size is non-zero, allocate new block
// If the old size is non zero and the new size is zero, free the allocation
// If the old size is lesser than the new size, grow the allocation and vice versa.
void *reallocate(void* pointer, size_t oldSize, size_t newSize);

#endif
