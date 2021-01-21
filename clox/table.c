#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

// Whenever we encounter a tombstone, we store it in a local variable first,
// once we encounter an empty entry, then we can be sure that the key doesn't
// exist in the table, in which case we can then return the first tombstone 
// entry that we discovered (if any).
static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  uint32_t index = key->hash % capacity;
  Entry* tombstone = NULL;

  for (;;) {
    Entry* entry = &entries[index];

    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        // Now that we encountered an empty entry, we know for sure
        // that the key doesnt exist, we either return any previously found
        // tombstone entries, or this empty entry itself
        return tombstone != NULL ? tombstone : entry;
      } else {
        // We found a tombstone, we only store the first tombstone
        // that we encountered, since this is the one we want to return
        if (tombstone == NULL) tombstone = entry;
      }
    } else if (entry->key == key) {
      // We found the entry with the right key
      return entry;
    }

    // Cycle along to the next index to probe
    index = (index + 1) % capacity;
  }
}

bool tableGet(Table* table, ObjString* key, Value* value) {
  // When the table is empty, the array might still not be
  // initialised yet, hence this check is more than an optimisation
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

static void adjustCapacity(Table* table, int capacity) {
  Entry* entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  // Recalculate buckets for existing entries, we walk through
  // all elements in the existing table and insert them to the
  // new table. Since we skip entries with NULL keys, we will not
  // copy tombstone entries, and makes it necessary for us to recount
  // the number of entries we have
  table->count = 0;
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key == NULL) continue;

    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableSet(Table *table, ObjString* key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(table, capacity);
  }  

  Entry* entry = findEntry(table->entries, table->capacity, key);

  bool isNewKey = entry->key == NULL;

  // Only increment count if we are not overwriting an
  // existing entry and the entry wasn't previously a 
  // tombstone entry (in which case its count would 
  // already be accounted for)
  if (isNewKey && IS_NIL(entry->value)) {
    table->count++;
  }

  entry->key = key;
  entry->value = value;

  return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
  if (table->count == 0) return false;

  // Find the entry
  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  // Place a tombstone in the entry
  entry->key = NULL;
  entry->value = BOOL_VAL(true);

  return true;
}

void tableAddAll(Table* from, Table* to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if (entry->key != NULL) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
  if (table->count == 0) return NULL;

  uint32_t index  = hash % table->capacity;

  for (;;) {
    Entry* entry = &table->entries[index];
    if (entry->key == NULL) {
      // Stop if we find an empty non-tombstone entry
      if (IS_NIL(entry->value)) return NULL;
    } else if (entry->key->length == length && 
        entry->key->hash == hash && 
        memcmp(entry->key->chars, chars, length) == 0) {
      return entry -> key;
    }

    index = (index + 1) % table->capacity;
  }
}

void tableRemoveWhite(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key != NULL && !entry->key->obj.isMarked) {
      tableDelete(table, entry->key);
    }
  }
}

void markTable(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    // Mark the string key and the value
    markObject((Obj*)entry->key);
    markValue(entry->value);
  }
}
