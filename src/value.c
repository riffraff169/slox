#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

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

Value valueToString(Value value) {
    if (IS_STRING(value)) {
        return value;
    }

    char buffer[128];
    int length = 0;

    if (IS_BOOL(value)) {
        length = snprintf(buffer, sizeof(buffer), AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        length = snprintf(buffer, sizeof(buffer), "nil");
    } else if (IS_NUMBER(value)) {
        length = snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(value));
        /*
    } else if (IS_ARRAY(value)) {
        return copyString("[array]", 7);
    } else if (IS_MAP(value)) {
        return copyString("[map]", 5);
        */
    } else if (IS_VEC3(value)) {
        length = snprintf(buffer, sizeof(buffer), "Vec3(%g, %g, %g)",
                AS_VEC3(value).x, AS_VEC3(value).y, AS_VEC3(value).z);
    } else if (IS_OBJ(value)) {
        ObjClass* klass = getClassForValue(value);
        if (klass != NULL) {
            length = snprintf(buffer, sizeof(buffer), "<object %s>", klass->name->chars);
        } else {
            length = snprintf(buffer, sizeof(buffer), "<object>");
        }
    } else {
        length = snprintf(buffer, sizeof(buffer), "unknown");
    }

    return OBJ_VAL(copyString(buffer, length));
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
                if (vm.numNotation == 1) {
                    printf("%.*g", vm.numPrecision, AS_NUMBER(value));
                } else {
                    printf("%.*f", vm.numPrecision, AS_NUMBER(value));
                }
                break;
            case VAL_OBJ:
                printObject(value);
                break;
            case VAL_SPLAT_COUNT:
                printf("<splat count: %d>\n", AS_SPLAT_COUNT(value));
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

