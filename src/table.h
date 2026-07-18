#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct {
    ObjString* key;
    Value value;
} Entry;

typedef struct {
    int count;
    int capacity;
    Entry* entries;
} Table;

typedef struct {
    Value key;
    Value value;
} Entry2;

typedef struct {
    int count;
    int capacity;
    Entry2* entries;
} Table2;

void initTable(Table* table);
void initTable2(Table2* table);
void freeTable(Table* table);
void freeTable2(Table2* table);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableGet2(Table2* table, Value key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableSet2(Table2* table, Value key, Value value);
bool tableDelete(Table* table, ObjString* key);
bool tableDelete2(Table2* table, Value key);
void tableAddAll(Table* from, Table* to);
void tableMergeGuard(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars,
        int length, uint32_t hash);
void tableRemoveWhite(Table* table);
void markTable(Table* table);
void markTable2(Table2* table);

#endif
