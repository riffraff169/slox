#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

#define MAX_KEY_COMPARE_DEPTH 64

static bool valuesEqualStrictAtDepth(Value a, Value b,
        Obj** pathA, Obj** pathB, int depth);

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
            int framesBefore = vm.frameCount;

            push(value);

            if (!callValue(method, 0)) {
                vm.nativeExitDepth = oldExitDepth;
                return NIL_VAL;
            }

            if (vm.frameCount > framesBefore) {
                vm.nativeExitDepth = framesBefore;
                InterpretResult result = run();
                vm.nativeExitDepth = oldExitDepth;

                if (vm.exceptionThrown || result == INTERPRET_RUNTIME_ERROR) {
                    return NIL_VAL; // exception is pending, or the error was already reported
                }
            } else {
                vm.nativeExitDepth = oldExitDepth;
            }

            Value resultVal = pop();

            if (IS_STRING(resultVal)) {
                return resultVal;
            }

            // A to_string() that returns the receiver itself would recurse
            // forever; fall through to the default representation instead.
            if (!valuesEqualIdentity(resultVal, value)) {
                return valueToString(resultVal);
            }
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
        ObjString* string = AS_STRING(value);
        fputc('"', stream);
        for (int i = 0; i < string->length; i++) {
            char c = string->chars[i];
            switch (c) {
                case '\0': fputs("\\0", stream); break;
                case '\n': fputs("\\n", stream); break;
                case '\r': fputs("\\r", stream); break;
                case '\t': fputs("\\t", stream); break;
                case '\\': fputs("\\\\", stream); break;
                case '"': fputs("\\\"", stream); break;
                default:
                          if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
                              fputc(c, stream);
                          } else {
                              fprintf(stream, "\\x%02x", (unsigned char)c);
                          }
                          break;
            }
        }
        fputc('"', stream);
        //fprintf(stream, "\"%s\"", AS_CSTRING(value));
    } else {
        switch (value.type) {
            case VAL_BOOL:
                fprintf(stream, AS_BOOL(value) ? "true" : "false");
                break;
            case VAL_NIL:
                fprintf(stream, "nil");
                break;
            case VAL_INT:
                fprintf(stream, "%" PRId64, AS_INT(value));
                break;
            case VAL_FLOAT:
                switch (vm.numNotation) {
                    case 1:
                        fprintf(stream, "%.*e", vm.numPrecision, AS_FLOAT(value));
                        break;
                    case 2:
                        fprintf(stream, "%.*f", vm.numPrecision, AS_FLOAT(value));
                        break;
                    case 0:
                    default:
                        fprintf(stream, "%.*g", vm.numPrecision, AS_FLOAT(value));
                        break;
                }
                break;
            case VAL_OBJ:
                printObject(stream, value);
                break;
            case VAL_SPLAT_COUNT:
                fprintf(stream, "<splat count: %d>", (int)AS_SPLAT_COUNT(value));
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
        ObjString* string = AS_STRING(value);
        //fprintf(stream, "%s", AS_CSTRING(value));
        fwrite(string->chars, sizeof(char), string->length, stream);
    } else {
        printValueSafe(stream, value);
    }
}

int compareStrings(const ObjString* a, const ObjString* b) {
    size_t minLength = (size_t)((a->length < b->length) ? a->length : b->length);
    int result = memcmp(a->chars, b->chars, minLength);

    if (result < 0) return -1;
    if (result > 0) return 1;
    if (a->length == b->length) return 0;

    return a->length < b->length ? -1 : 1;
}

bool valuesEqualIdentity(Value a, Value b) {
    if (a.type != b.type) return false;

    switch (a.type) {
        case VAL_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:
            return true;
        case VAL_INT:
            return AS_INT(a) == AS_INT(b);
        case VAL_FLOAT:
            return AS_FLOAT(a) == AS_FLOAT(b);
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

bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) {
        // cross-type numeric equality coercion (eg 100 == 100.0)
        if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
            return valueToDouble(a) == valueToDouble(b);
        }
        return false;
    }

    switch (a.type) {
        case VAL_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:
            return true;
        case VAL_INT:
            return AS_INT(a) == AS_INT(b);
        case VAL_FLOAT:
            return AS_FLOAT(a) == AS_FLOAT(b);
        case VAL_VEC3:
            return (AS_VEC3(a).x == AS_VEC3(b).x) &&
                (AS_VEC3(a).y == AS_VEC3(b).y) &&
                (AS_VEC3(a).z == AS_VEC3(b).z);
        case VAL_OBJ:
            if ((IS_ARRAY(a) || IS_MAP(a) || IS_SET(a)) &&
                (IS_ARRAY(b) || IS_MAP(b) || IS_SET(b)) &&
                AS_OBJ(a)->type == AS_OBJ(b)->type) {
                Obj* pathA[MAX_KEY_COMPARE_DEPTH];
                Obj* pathB[MAX_KEY_COMPARE_DEPTH];
                return valuesEqualStrictAtDepth(a, b, pathA, pathB, 0);
            }
            return AS_OBJ(a) == AS_OBJ(b);
        default:
            return false;
    }
}

static int pathIndex(Obj** path, int depth, Obj* obj) {
    for (int i = 0; i < depth; i++) {
        if (path[i] == obj) return i;
    }
    return -1;
}

static bool tableEntriesMatch(Table2* a, Table2* b,
        Obj** pathA, Obj** pathB, int depth) {
    if (tableLiveCount2(a) != tableLiveCount2(b)) return false;

    // Keys are unique within a table, so a match for every entry in A is a
    // bijection. Comparing both key and value keeps maps and multisets honest.
    for (int i = 0; i < a->capacity; i++) {
        Entry2* entryA = &a->entries[i];
        if (IS_NIL(entryA->key)) continue;

        bool found = false;
        for (int j = 0; j < b->capacity; j++) {
            Entry2* entryB = &b->entries[j];
            if (IS_NIL(entryB->key)) continue;
            if (valuesEqualStrictAtDepth(entryA->key, entryB->key,
                    pathA, pathB, depth) &&
                valuesEqualStrictAtDepth(entryA->value, entryB->value,
                    pathA, pathB, depth)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static bool containersEqual(Value a, Value b,
        Obj** pathA, Obj** pathB, int depth) {
    Obj* objA = AS_OBJ(a);
    Obj* objB = AS_OBJ(b);

    // Coinductive cycle guard: a pair already being compared is assumed equal
    // only if both sides revisit it at the same position in their paths.
    int indexA = pathIndex(pathA, depth, objA);
    int indexB = pathIndex(pathB, depth, objB);
    if (indexA >= 0 || indexB >= 0) return indexA == indexB;

    if (depth >= MAX_KEY_COMPARE_DEPTH) return objA == objB;

    pathA[depth] = objA;
    pathB[depth] = objB;

    if (objA->type == OBJ_ARRAY) {
        ObjArray* arrayA = AS_ARRAY(a);
        ObjArray* arrayB = AS_ARRAY(b);
        if (arrayA->count != arrayB->count) return false;
        for (int i = 0; i < arrayA->count; i++) {
            if (!valuesEqualStrictAtDepth(arrayA->values[i], arrayB->values[i],
                    pathA, pathB, depth + 1)) return false;
        }
        return true;
    }

    if (objA->type == OBJ_SET) {
        ObjSet* setA = AS_SET(a);
        ObjSet* setB = AS_SET(b);
        if (setA->isMultiset != setB->isMultiset) return false;
        return tableEntriesMatch(&setA->items, &setB->items,
                pathA, pathB, depth + 1);
    }

    ObjMap* mapA = AS_MAP(a);
    ObjMap* mapB = AS_MAP(b);
    return tableEntriesMatch(&mapA->items, &mapB->items,
            pathA, pathB, depth + 1);
}

static bool valuesEqualStrictAtDepth(Value a, Value b,
        Obj** pathA, Obj** pathB, int depth) {
    if (a.type != b.type) return false;

    switch (a.type) {
        case VAL_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:
            return true;
        case VAL_INT:
            return AS_INT(a) == AS_INT(b);
        case VAL_FLOAT:
            return AS_FLOAT(a) == AS_FLOAT(b);
        case VAL_VEC3:
            return (AS_VEC3(a).x == AS_VEC3(b).x) &&
                (AS_VEC3(a).y == AS_VEC3(b).y) &&
                (AS_VEC3(a).z == AS_VEC3(b).z);
        case VAL_OBJ:
            if ((IS_ARRAY(a) || IS_MAP(a) || IS_SET(a)) &&
                (IS_ARRAY(b) || IS_MAP(b) || IS_SET(b)) &&
                AS_OBJ(a)->type == AS_OBJ(b)->type) {
                return containersEqual(a, b, pathA, pathB, depth);
            }
            return AS_OBJ(a) == AS_OBJ(b);
        default:
            return false;
    }
}

bool valuesEqualStrict(Value a, Value b) {
    Obj* pathA[MAX_KEY_COMPARE_DEPTH];
    Obj* pathB[MAX_KEY_COMPARE_DEPTH];
    return valuesEqualStrictAtDepth(a, b, pathA, pathB, 0);
}

