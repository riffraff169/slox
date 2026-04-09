#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "value.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values,
                oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

ObjString* valueToString(Value value) {
    if (IS_STRING(value)) {
        return AS_STRING(value);
    } else if (IS_BOOL(value)) {
        return AS_BOOL(value) ? copyString("true", 4) : copyString("false", 5);
    } else if (IS_NIL(value)) {
        return copyString("nil", 3);
    } else if (IS_NUMBER(value)) {
        char buffer[32];
        int length = snprintf(buffer, 32, "%g", AS_NUMBER(value));
        return copyString(buffer, length);
    } else if (IS_ARRAY(value)) {
        return copyString("[array]", 7);
    } else if (IS_MAP(value)) {
        return copyString("[map]", 5);
    } else if (IS_VEC3(value)) {
        char buffer[128];
        int length = snprintf(buffer, 128, "Vec3(%g, %g, %g)",
                AS_VEC3(value).x, AS_VEC3(value).y, AS_VEC3(value).z);
        return copyString(buffer, length);
    }

    return copyString("obj", 3);
}

void printValueSafe(Value value) {
    if (IS_STRING(value)) {
        printf("\"%s\"", AS_CSTRING(value));
    } else {
        switch (value.type) {
            case VAL_BOOL:
                printf(AS_BOOL(value) ? "true" : "false");
                break;
            case VAL_NIL:
                printf("nil");
                break;
            case VAL_NUMBER:
                printf("%g", AS_NUMBER(value));
                break;
            case VAL_OBJ:
                printObject(value);
                break;
            case VAL_VEC3:
                printf("Vec3(%g, %g, %g)", AS_VEC3(value).x,
                        AS_VEC3(value).y, AS_VEC3(value).z);
                break;
        }
    }
}

void printValue(Value value) {
    if (IS_STRING(value)) {
        printf("%s", AS_CSTRING(value));
    } else {
        printValueSafe(value);
    }
}

bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:
            return true;
        case VAL_NUMBER:
            return AS_NUMBER(a) == AS_NUMBER(b);
        case VAL_OBJ:
            return AS_OBJ(a) == AS_OBJ(b);
        case VAL_VEC3:
            return (AS_VEC3(a).x == AS_VEC3(b).x) &&
                (AS_VEC3(a).y == AS_VEC3(b).y) &&
                (AS_VEC3(a).z == AS_VEC3(b).z);
        default:
            return false;
    }
}

