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
    VAL_FLOAT,
    VAL_INT, 
    VAL_OBJ,
    VAL_VEC3,
    VAL_SPLAT_COUNT,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        int64_t integer;
        Obj* obj;
        Vec3 vec3;
    } as;
} Value;

#define IS_BOOL(value)              ((value).type == VAL_BOOL)
#define IS_NIL(value)               ((value).type == VAL_NIL)
#define IS_NUMBER(value)            ((value).type == VAL_NUMBER)
#define IS_INT(value)               ((value).type == VAL_INT)
#define IS_FLOAT(value)             ((value).type == VAL_FLOAT)
#define IS_OBJ(value)               ((value).type == VAL_OBJ)
#define IS_VEC3(value)              ((value).type == VAL_VEC3)
#define IS_SPLAT_COUNT(value)       ((value).type == VAL_SPLAT_COUNT)

#define AS_OBJ(value)               ((value).as.obj)
#define AS_BOOL(value)              ((value).as.boolean)
#define AS_NUMBER(value)            ((value).as.number)
#define AS_INT(value)               ((value).as.integer)
#define AS_FLOAT(value)             ((value).as.number)
#define AS_VEC3(value)              ((value).as.vec3)
#define AS_SPLAT_COUNT(value)       ((value).as.number)

#define BOOL_VAL(value)             ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL                     ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value)           ((Value){VAL_NUMBER, {.number = value}})
#define INT_VAL(value)              ((Value){VAL_INT, {.integer = value}})
#define FLOAT_VAL(value)            ((value){VAL_FLOAT, {.number = value}})
#define OBJ_VAL(object)             ((Value){VAL_OBJ, {.obj = (Obj*)object}})
#define VEC3_VAL(value)             ((Value){VAL_VEC3, {.vec3 = value}})
#define SPLAT_COUNT_VAL(count)      ((Value){VAL_SPLAT_COUNT, {.number = (double)(count)}})

typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

Value valueToString(Value value);
bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValueSafe(FILE* stream, Value value);
void printValue(FILE* stream, Value value);

#define IS_NUMERIC(value) (IS_INT(value) || IS_NUMBER(value) || IS_FLOAT(value))

#define MAX_SAFE_INTEGER 9007199254740991LL
#define MIN_SAFE_INTEGER -9007199254740991LL

static inline bool fitsInDouble(int64_t value) {
    return value >= MIN_SAFE_INTEGER && value <= MAX_SAFE_INTEGER;
}

static inline bool isIntegerDouble(double d) {
    if (trunc(d) != d) return false;

    if (d < -9223372036854775808.0 || d >= 9223372036854775808.0) return false;

    return true;
}

// safely converts any numeric Value to double
static inline double valueToDouble(Value value) {
    if (IS_INT(value)) return (double)AS_INT(value);
    if (IS_NUMBER(value)) return AS_NUMBER(value);
    if (IS_FLOAT(value)) return AS_NUMBER(value);
    return 0.0;
}

// attempts to convert any numeric Value to int64_t
static inline bool valueToInt64(Value value, int64_t* out) {
    if (IS_INT(value)) {
        *out = AS_INT(value);
        return true;
    }

    if (IS_NUMBER(value)) {
        double d = AS_NUMBER(value);
        int64_t i = (int64_t)d;
        if ((double)i == d) { // exact whole number check
            *out = i;
            return true;
        }
    }
    return false;
}

static inline bool isExactInteger(double d) {
    return (trunc(d) == d) &&
       (d >= -9223372036854775808.0) &&
       (d < 9223372036854775808.0);
}
#endif
