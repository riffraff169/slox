#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

void initTable2(Table2* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

void freeTable2(Table2* table) {
    FREE_ARRAY(Entry2, table->entries, table->capacity);
    initTable2(table);
}

static Entry* findEntry(Entry* entries, int capacity,
        ObjString* key) {
    uint32_t index = key->hash & (capacity - 1);
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                return tombstone != NULL ? tombstone : entry;
            } else {
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            return entry;
        }

        index = (index + 1 ) & (capacity - 1);
    }
}

static Entry2* findEntry2(Entry2* entries, int capacity, Value key) {
    uint32_t hash = hashValue(key);
    uint32_t index = hash & (capacity - 1);
    Entry2* tombstone = NULL;

    for (;;) {
        Entry2* entry = &entries[index];

        if (IS_NIL(entry->key)) {
            if (IS_BOOL(entry->value) && AS_BOOL(entry->value)) {
                if (tombstone == NULL) tombstone = entry;
            } else {
                return tombstone != NULL ? tombstone : entry;
            }
        } else if (valuesEqual(entry->key, key)) {
            return entry;
        }

        index = (index + 1) & (capacity - 1);
    }
}

bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

bool tableGet2(Table2* table, Value key, Value* value) {
    if (table->count == 0) return false;

    Entry2* entry = findEntry2(table->entries, table->capacity, key);
    if (IS_NIL(entry->key)) return false;

    *value = entry->value;
    return true;
}

static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

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

static void adjustCapacity2(Table2* table, int capacity) {
    Entry2* entries = ALLOCATE(Entry2, capacity);

    for (int i = 0; i < capacity; i++) {
        entries[i].key = NIL_VAL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry2* entry = &table->entries[i];
        if (IS_NIL(entry->key)) continue;

        Entry2* dest = findEntry2(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry2, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool tableSet(Table* table, ObjString* key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableSet2(Table2* table, Value key, Value value) {

    if (IS_NIL(key)) {
        return false;
    }

    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity2(table, capacity);
    }

    Entry2* entry = findEntry2(table->entries, table->capacity, key);
    bool isNewKey = IS_NIL(entry->key);

    if (isNewKey && IS_NIL(entry->value)) {
        table->count++;
    }

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;

    // find
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    // tombstone
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

bool tableDelete2(Table2* table, Value key) {
    if (IS_NIL(key)) return false;
    if (table->count == 0) return false;

    Entry2* entry = findEntry2(table->entries, table->capacity, key);
    if (IS_NIL(entry->key)) return false;

    entry->key = NIL_VAL;
    entry->value = BOOL_VAL(true);
    table->count--;
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

void tableMergeGuard(Table* from, Table* to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key == NULL) continue;
        Value dummy;
        if  (!tableGet(to, entry->key, &dummy)) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

ObjString* tableFindString(Table* table, const char* chars,
        int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash & (table->capacity - 1);
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length &&
                entry->key->hash == hash &&
                memcmp(entry->key->chars, chars, length) == 0) {
            return entry->key;
        }

        index = (index + 1) & (table->capacity - 1);
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

/*
void tableRemoveWhite2(Table2* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry2* entry = &table->entries[i];
        if (IS_NIL(entry->key)) {
            if (AS_OBJ(entry->key) && !AS_OBJ(entry->key)->isMarked) {
                tableDelete2(table, entry->key);
            }
        }
    }
}
*/

void markTable(Table* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        markObject((Obj*)entry->key);
        markValue(entry->value);
    }
}

void markTable2(Table2* table) {
    for (int i = 0; i < table->capacity; i++) {
        Entry2* entry = &table->entries[i];
        markValue(entry->key);
        markValue(entry->value);
    }
}

