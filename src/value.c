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
    // 1. fast path already a string
    if (IS_STRING(value)) {
        return value;
    }

    // 2. check if instance has a custom to_string method
    if (IS_INSTANCE(value)) {
        ObjInstance* instance = AS_INSTANCE(value);
        Value method;

        if (tableGet(&instance->obj.klass->methods, vm.toString, &method)) {
            int oldExitDepth = vm.nativeExitDepth;
            Value* callbackStackStart = vm.stackTop;
            int framesBefore = vm.frameCount;

            push(value);

            if (callValue(method, 0)) {
                if (vm.frameCount > framesBefore) {
                    vm.nativeExitDepth = framesBefore;
                    InterpretResult result = run();

                    if (result == INTERPRET_RUNTIME_ERROR) {
                        vm.stackTop = callbackStackStart;
                        vm.nativeExitDepth = oldExitDepth;
                        return NIL_VAL; // return nil on runtime error
                    }
                }

                Value resultVal = pop();
                vm.nativeExitDepth = oldExitDepth;

                if (IS_STRING(resultVal)) {
                    return resultVal;
                }

                if (valuesEqual(resultVal, value)) {
                    return resultVal;
                }

                if (!valuesEqual(resultVal, value)) {
                    return valueToString(resultVal);
                }
            }

            vm.nativeExitDepth = oldExitDepth;
        }
    }

    char* ptr = NULL;
    size_t size = 0;
    FILE* stream = open_memstream(&ptr, &size);

    printValue(stream, value);
    fclose(stream);

    ObjString* result = copyString(ptr, (int)size);
    free(ptr);

    return OBJ_VAL(result);
}

void printValueSafe(FILE* stream, Value value) {
    if (IS_STRING(value)) {
        fprintf(stream, "\"%s\"", AS_CSTRING(value));
    } else {
        switch (value.type) {
            case VAL_BOOL:
                fprintf(stream, AS_BOOL(value) ? "true" : "false");
                break;
            case VAL_NIL:
                fprintf(stream, "nil");
                break;
            case VAL_NUMBER:
                if (vm.numNotation == 1) {
                    fprintf(stream, "%.*g", vm.numPrecision, AS_NUMBER(value));
                } else {
                    fprintf(stream, "%.*f", vm.numPrecision, AS_NUMBER(value));
                }
                break;
            case VAL_OBJ:
                printObject(stream, value);
                break;
            case VAL_SPLAT_COUNT:
                fprintf(stream, "<splat count: %d>\n", AS_SPLAT_COUNT(value));
                break;
            case VAL_VEC3:
                fprintf(stream, "Vec3(%g, %g, %g)", AS_VEC3(value).x,
                        AS_VEC3(value).y, AS_VEC3(value).z);
                break;
        }
    }
}

void printValue(FILE* stream, Value value) {
    if (IS_STRING(value)) {
        fprintf(stream, "%s", AS_CSTRING(value));
    } else {
        printValueSafe(stream, value);
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
        case VAL_VEC3:
            return (AS_VEC3(a).x == AS_VEC3(b).x) &&
                (AS_VEC3(a).y == AS_VEC3(b).y) &&
                (AS_VEC3(a).z == AS_VEC3(b).z);
        case VAL_OBJ:
            return AS_OBJ(a) == AS_OBJ(b);
        default:
            return false;
    }
}

