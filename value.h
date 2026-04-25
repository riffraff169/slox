#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef struct {
    double x, y, z;
} Vec3;

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
    VAL_VEC3,
    VAL_SPLAT_COUNT,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;
        Vec3 vec3;
    } as;
} Value;

#define IS_BOOL(value)              ((value).type == VAL_BOOL)
#define IS_NIL(value)               ((value).type == VAL_NIL)
#define IS_NUMBER(value)            ((value).type == VAL_NUMBER)
#define IS_OBJ(value)               ((value).type == VAL_OBJ)
#define IS_VEC3(value)              ((value).type == VAL_VEC3)
#define IS_SPLAT_COUNT(value)       ((value).type == VAL_SPLAT_COUNT)

#define AS_OBJ(value)               ((value).as.obj)
#define AS_BOOL(value)              ((value).as.boolean)
#define AS_NUMBER(value)            ((value).as.number)
#define AS_VEC3(value)              ((value).as.vec3)
#define AS_SPLAT_COUNT(value)       ((value).as.number)

#define BOOL_VAL(value)             ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL                     ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value)           ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)             ((Value){VAL_OBJ, {.obj = (Obj*)object}})
#define VEC3_VAL(value)             ((Value){VAL_VEC3, {.vec3 = value}})
#define SPLAT_COUNT_VAL(count)      ((Value){VAL_SPLAT_COUNT, {.number = (double)(count)}})

typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

ObjString* valueToString(Value value);
bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValueSafe(Value value);
void printValue(Value value);

#endif
