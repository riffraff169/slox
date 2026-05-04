#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

#define MAX32 4294967296.0

VM vm;
InterpretResult run();
//void initArrayMethods();

static bool callValue(Value callee, int argCount);
Value peek(int distance);
Value popn(int n);
static bool isFalsey(Value value);

static uint32_t valueToUint32(Value value) {
    double num = AS_NUMBER(value);

    //return (uint32_t)fmod(num, MAX32);
    return (uint32_t)(long long)num;
}

void includeMethods(ObjClass* target, ObjClass* mixin) {
    for (int i = 0; i < mixin->methods.capacity; i++) {
        Entry* entry = &mixin->methods.entries[i];

        if (entry->key == NULL) continue;

        if (memcmp(entry->key->chars, "init", 4) == 0 && entry->key->length == 4) {
            continue;
        }

        Value dummy;
        if (!tableGet(&target->methods, entry->key, &dummy)) {
            tableSet(&target->methods, entry->key, entry->value);
        }
    }
}

static Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value strNative(int argCount, Value* args) {
    if (argCount != 1) return NIL_VAL;

    char buffer[64];
    int len = 0;

    if (IS_NUMBER(args[0])) {
        len = snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(args[0]));
    } else if (IS_BOOL(args[0])) {
        len = snprintf(buffer, sizeof(buffer), AS_BOOL(args[0]) ? "true" : "false");
    } else if (IS_NIL(args[0])) {
        len = snprintf(buffer, sizeof(buffer), "nil");
    } else if (IS_STRING(args[0])) {
        return args[0]; // already a string
    } else {
        len = snprintf(buffer, sizeof(buffer), "<object>");
    }

    return OBJ_VAL(copyString(buffer, len));
}

/*
static bool callNative(ObjNative* native, int argCount) {
    Value* argsStart = vm.stackTop - argCount - 1;
    Value result = native->function(argCount, argsStart);

    //vm.stackTop -= argCount + 1;
    vm.stackTop = argsStart;
    printValue(result);
    push(result);
    return true;
}
*/

static Value hasMethodNative(int argCount, Value* args) {
    if (argCount < 2 || (!IS_INSTANCE(args[-1])) || !IS_STRING(args[0])) {
        runtimeError("has_method() expects an instance");
        return BOOL_VAL(false);
    }
    Value instance = args[-1];
    Value name = args[0];
    Value method;

    if (tableGet(&AS_INSTANCE(instance)->obj.klass->methods, AS_STRING(name), &method)) {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value getMethodsNative(int argCount, Value* args) {
    Value receiver = args[-1];
    ObjClass* klass = NULL;

    if (IS_INSTANCE(receiver)) {
        klass = AS_INSTANCE(receiver)->obj.klass;
    } else if (IS_CLASS(receiver)) {
        klass = AS_CLASS(receiver);
    } else if (IS_OBJ(receiver)) {
        klass = AS_OBJ(receiver)->klass;
    }

    if (klass == NULL) {
        runtimeError("Cannot get methods of a non-object/non-class.");
        return NIL_VAL;
    }

    ObjArray* list = newArray();
    push(OBJ_VAL(list));

    ObjClass* current = klass;
    while (current != NULL) {
        Table* table = &current->methods;
        for (int i = 0; i < table->capacity; i++) {
            Entry* entry = &table->entries[i];
            if (entry->key != NULL) {
                arrayAppend(list, OBJ_VAL(entry->key));
            }
        }
        current = current->superclass;
    }

    return pop();
}

void* loadModule(const char* name) {
    // 1. construct the filename
    char path[256];
    snprintf(path, sizeof(path), "./liblox_%s.so", name);

    // 2. open the shared library
    void* handle = dlopen(path, RTLD_NOW);
    if (!handle) {
        runtimeError("Could not load module '%s': %s", name, dlerror());
        return NULL;
    }

    if (vm.moduleCapacity < vm.moduleCount + 1) {
        int oldCapacity = vm.moduleCapacity;
        vm.moduleCapacity = GROW_CAPACITY(oldCapacity);
        vm.moduleHandles = GROW_ARRAY(void*, vm.moduleHandles, oldCapacity, vm.moduleCapacity);
    }
    vm.moduleHandles[vm.moduleCount++] = handle;

    // 3. find the init function
    // every module must have a function: void lox_module_init(VM* vm)
    typedef void (*ModuleInitFn)(VM* vm);
    ModuleInitFn init = (ModuleInitFn)dlsym(handle, "lox_module_init");

    if (!init) {
        runtimeError("Module '%s' is missing lox_module_init.", name);
        dlclose(handle);
        return NULL;
    }

    // 4. run the init function to register classes/natives
    init(&vm);
    
    return handle;
}

static Value mathSqrtNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) {
        runtimeError("sqrt() expects 1 number argument.");
        return NIL_VAL;
    }
    return NUMBER_VAL(sqrt(AS_NUMBER(args[1])));
}

static Value mathAbsNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) {
        runtimeError("sqrt() expects 1 number argument.");
        return NIL_VAL;
    }
    return NUMBER_VAL(fabs(AS_NUMBER(args[1])));
}

static Value toNumberNative(int argCount, Value* args) {
    if (argCount < 1) return NUMBER_VAL(0);

    if (IS_NUMBER(args[0])) return args[0];
    if (!IS_STRING(args[0])) return NUMBER_VAL(0);

    char* end;
    const char* str = AS_CSTRING(args[0]);
    double number = strtod(str, &end);

    if (str == end) {
        return NUMBER_VAL(0);
    }

    return NUMBER_VAL(number);
}

static Value mathSinNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(sin(AS_NUMBER(args[1])));
}

static Value mathCosNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(cos(AS_NUMBER(args[1])));
}

static Value mathAcosNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(acos(AS_NUMBER(args[1])));
}

static Value mathTanNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(tan(AS_NUMBER(args[1])));
}

static Value mathAtan2Native(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) return NIL_VAL;
    return NUMBER_VAL(atan2(AS_NUMBER(args[1]), AS_NUMBER(args[2])));
}

static Value mathRoundNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(round(AS_NUMBER(args[1])));
}

static Value mathFloorNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(floor(AS_NUMBER(args[1])));
}

static Value mathCeilNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(ceil(AS_NUMBER(args[1])));
}

static Value mathRandomNative(int argCount, Value* args) {
    return NUMBER_VAL((double)rand() / (double)RAND_MAX);
}

static Value mathPiNative(int argCount, Value* args) {
    return NUMBER_VAL(3.14159265358979323846);
}

static Value mathExpNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[1])) return NUMBER_VAL(exp(1.0));
    return NUMBER_VAL(exp(AS_NUMBER(args[1])));
}

static int defaultSortComparator(const void* a, const void* b) {
    Value valA = *(Value*)a;
    Value valB = *(Value*)b;

    // sort numbers
    if (IS_NUMBER(valA) && IS_NUMBER(valB)) {
        double diff = AS_NUMBER(valA) - AS_NUMBER(valB);
        return (diff > 0) - (diff < 0);
    }

    // sort strings
    if (IS_STRING(valA) && IS_STRING(valB)) {
        return strcmp(AS_CSTRING(valA), AS_CSTRING(valB));
    }

    // sort booleans (false < true)
    if (IS_BOOL(valA) && IS_BOOL(valB)) {
        return (int)AS_BOOL(valA) - (int)AS_BOOL(valB);
    }

    // fallback: stable order for mixed types based on type tag
    return (int)valA.type - (int)valB.type;
}

static int loxSortComparator(const void* a, const void* b, void* userdata) {
    // 1. recover our context
    ObjClosure* callback = (ObjClosure*)userdata;
    Value valA = *(Value*)a;
    Value valB = *(Value*)b;

    // 2. setup the stack baseline
    Value* comparisonStackBase = vm.stackTop;

    // 3. push closure + 2 arguments
    push(OBJ_VAL(callback));
    push(valA);
    push(valB);

    vm.nativeExitDepth = vm.frameCount;

    if (vmCall(callback, 2)) {
        run();
        Value result = pop();

        vm.stackTop = comparisonStackBase;

        if (IS_NUMBER(result)) return (int)AS_NUMBER(result);
        if (IS_BOOL(result)) {
            if (AS_BOOL(result) == false)
                return 1;
            else
                return -1;
        }
    }
    vm.stackTop = comparisonStackBase;
    return 0; // default to equal
}

static Value arraySortSliceNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    if (array->count < 2) return args[-1];
    int start = AS_NUMBER(args[0]);
    int end = AS_NUMBER(args[1]);
    if (start < 0 || end > array->count || start > end) {
        return args[-1];
    }

    Value* sliceStart = &array->values[start];
    int count = end - start;
    if (argCount >= 4 && IS_CLOSURE(args[2])) {
        qsort_r(sliceStart, count, sizeof(Value),
                loxSortComparator, AS_CLOSURE(args[2]));
    } else {
        qsort(sliceStart, count, sizeof(Value),
                defaultSortComparator);
    }
    return args[0];
}

static Value arraySortNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    if (array->count < 2) return args[-1];

    if (argCount >= 2 && IS_CLOSURE(args[0])) {
        qsort_r(array->values, array->count, sizeof(Value),
                loxSortComparator, AS_CLOSURE(args[0]));
    } else {
        // default sort (fast c)
        qsort(array->values, array->count, sizeof(Value),
                defaultSortComparator);
    }
    return args[-1];
}

static Value arraySliceNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    int count = array->count;

    int start = (argCount >= 2 && IS_NUMBER(args[0])) ?  (int)AS_NUMBER(args[0]) : 0;
    if (start < 0) start = count + start;
    if (start < 0) start = 0;
    if (start > count) start = count;

    int end = (argCount >= 3 && IS_NUMBER(args[1])) ?  (int)AS_NUMBER(args[1]) : count;
    if (end < 0) end = count + end;
    if (end < 0) end = 0;
    if (end > count) end = count;

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    if (end > start) {
        for (int i = start; i < end; i++) {
            arrayAppend(result, array->values[i]);
        }
    }

    return pop();
}

static Value arrayFindNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_CLOSURE(args[0])) return NIL_VAL;
    ObjArray* array = AS_ARRAY(args[-1]);
    ObjClosure* callback = AS_CLOSURE(args[0]);

    Value* stackStart = vm.stackTop;

    for (int i = 0; i < array->count; i++) {

        push(array->values[i]);

        vm.nativeExitDepth = vm.frameCount; // - 1;


        if (vmCall(callback, 1)) {
            InterpretResult result_state = run();

            Value result = pop();


            if (!isFalsey(result)) {
                vm.stackTop = stackStart;
                return array->values[i];
            }
        }
        vm.stackTop = stackStart;
    }

    vm.stackTop = stackStart;
    return NIL_VAL;
}

static Value arrayEachNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) return NIL_VAL;

    ObjArray* array = AS_ARRAY(args[-1]);
    push(OBJ_VAL(array));
    ObjClosure* callback = AS_CLOSURE(args[0]);

    Value* stackStart = vm.stackTop;

    for (int i = 0; i < array->count; i++) {
        push(args[0]);
        push(array->values[i]);

        vm.nativeExitDepth = vm.frameCount;

        if (vmCall(callback, 1)) {
            run();
        }

        vm.stackTop = stackStart;
    }
    return pop();
}

static Value arrayPushNative(int argCount, Value* args) {
    if (argCount < 1) return NIL_VAL;

    ObjArray* array = AS_ARRAY(args[-1]);
    for (int i = 0; i < argCount; i++) {
        arrayAppend(array, args[i]);
    }
    return OBJ_VAL(array);
}

static Value arrayPopNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);

    if (array->count == 0) {
        return NIL_VAL;
    }

    Value lastValue = array->values[array->count - 1];
    array->count--;
    array->values[array->count] = NIL_VAL;
    return lastValue;
}

static Value arrayReduceNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) {
        return NIL_VAL;
    }

    ObjArray* array = AS_ARRAY(args[-1]);
    Value callback = args[0];

    Value acc = (argCount > 1) ? args[1] : NIL_VAL;
    int startindex = (argCount > 1) ? 0 : 1;
    if (argCount <= 1 && array->count > 0) {
        acc = array->values[0];
    }

    for (int i = startindex; i < array->count; i++) {
        push(callback);
        push(acc);
        push(array->values[i]);

        if (callValue(callback, 2)) {
            vm.nativeExitDepth = vm.frameCount - 1;

            InterpretResult res = run();

            acc = pop();
        }
    }

    return acc;
}

static Value arraySelectNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) {
        return NIL_VAL;
    }

    ObjArray* original = AS_ARRAY(args[-1]);
    Value callback = args[0];
    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    for (int i = 0; i < original->count; i++) {
        push(callback);
        push(original->values[i]);

        if (callValue(callback, 1)) {
            vm.nativeExitDepth = vm.frameCount - 1;

            InterpretResult res = run();

            if (!isFalsey(pop())) {
                arrayAppend(result, original->values[i]);
            }
        }
    }
    return pop();
}

static Value arrayDupNative(int argCount, Value* args) {
    if (!IS_ARRAY(args[-1])) return NIL_VAL;

    ObjArray* original = AS_ARRAY(args[-1]);
    ObjArray* copy = duplicateArray(original);

    return OBJ_VAL(copy);
}

static Value arrayIsEmptyNative(int argCount, Value* args) {
    return BOOL_VAL(AS_ARRAY(args[-1])->count == 0);
}

static Value arrayMapNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) {
        return NIL_VAL;
    }

    ObjArray* original = AS_ARRAY(args[-1]);
    Value callback = args[0];
    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    for (int i = 0; i < original->count; i++) {
        push(callback);
        push(original->values[i]);

        if (callValue(callback, 1)) {
            vm.nativeExitDepth = vm.frameCount - 1;

            InterpretResult res = run();

            Value testResult = peek(0);
            arrayAppend(result, testResult);
            pop();
        }
    }
    pop();
    return OBJ_VAL(result);
}

static Value arrayReverseNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    if (array->count < 2) return args[-1];

    int left = 0;
    int right = array->count - 1;
    
    while (left < right) {
        Value temp = array->values[left];
        array->values[left] = array->values[right];
        array->values[right] = temp;
        left++;
        right--;
    }
    return args[-1];
}

static Value arrayFlattenNative(int argCount, Value* args) {
    ObjArray* source = AS_ARRAY(args[-1]);
    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    for (int i = 0; i < source->count; i++) {
        Value item = source->values[i];

        if (IS_ARRAY(item)) {
            ObjArray* inner = AS_ARRAY(item);
            for (int j = 0; j < inner->count; j++) {
                arrayAppend(result, inner->values[j]);
            }
        } else {
            arrayAppend(result, item);
        }
    }
    return pop();
}

static Value mapValuesNative(int argCount, Value* args) {
    ObjMap* map = AS_MAP(args[-1]);
    ObjArray* valuesArray = newArray();
    push(OBJ_VAL(valuesArray));

    for (int i = 0; i < map->items.capacity; i++) {
        Entry* entry = &map->items.entries[i];
        if (entry->key != NULL) {
            arrayAppend(valuesArray, entry->value);
        }
    }
    return pop();
}

static Value mapKeysNative(int argCount, Value* args) {
    ObjMap* map = AS_MAP(args[-1]);
    ObjArray* valuesArray = newArray();
    push(OBJ_VAL(valuesArray));

    for (int i = 0; i < map->items.capacity; i++) {
        Entry* entry = &map->items.entries[i];
        if (entry->key != NULL) {
            arrayAppend(valuesArray, OBJ_VAL(entry->key));
        }
    }

    return pop();
}

static Value mapHasNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return BOOL_VAL(false);
    }

    ObjMap* map = AS_MAP(args[-1]);
    Value dummy;
    return BOOL_VAL(tableGet(&map->items, AS_STRING(args[0]), &dummy));
}

static Value mapRemoveNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError("Map.remove() expects exactly 1 argument.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError("Map keys must be strings.");
        return NIL_VAL;
    }

    ObjMap* map = AS_MAP(args[-1]);
    ObjString* key = AS_STRING(args[0]);
    Value removedValue;

    if (tableGet(&map->items, key, &removedValue)) {
        tableDelete(&map->items, key);
        return removedValue;
    }

    return NIL_VAL;
}

static Value mapLenNative(int argCount, Value* args) {
    return NUMBER_VAL(AS_MAP(args[-1])->items.count);
}

static Value arrayLenNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    return NUMBER_VAL(array->count);
}

static Value stringToarrayNative(int argCount, Value* args) {
    ObjString* string = AS_STRING(args[-1]);
    ObjArray* array = newArray();
    push(OBJ_VAL(array));

    for (int i = 0; i < string->length; i++) {
        uint8_t byte = (uint8_t)string->chars[i];
        arrayAppend(array, NUMBER_VAL((double)byte));
    }

    return pop();
}

static Value stringSliceNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("slice() expects at least a start index.");
        return NIL_VAL;
    }

    ObjString* dom = AS_STRING(args[-1]);
    int length = dom->length;

    int start = (int)AS_NUMBER(args[0]);
    if (start < 0) start += length;
    if (start < 0) start = 0;
    if (start > length) start = length;

    int end = length;
    if (argCount >= 2 && IS_NUMBER(args[1])) {
        end = (int)AS_NUMBER(args[1]);
        if (end < 0) end += length;
        if (end < 0) end = 0;
        if (end > length) end = length;
    }

    if (start >= end) return OBJ_VAL(copyString("", 0));

    return OBJ_VAL(copyString(dom->chars + start, end - start));
}

static Value stringSplitNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("split() expects 1 string argument (separator).");
        return NIL_VAL;
    }

    ObjString* receiver = AS_STRING(args[-1]);
    ObjString* sep = AS_STRING(args[0]);

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    // Edge case: Empty separator splits into individual characters
    if (sep->length == 0) {
        for (int i = 0; i < receiver->length; i++) {
            ObjString* charStr = copyString(receiver->chars + i, 1);
            arrayAppend(result, OBJ_VAL(charStr));
        }
        return pop();
    }

    char *text = receiver->chars;
    char* found;
    int sepLen = sep->length;

    while ((found = strstr(text, sep->chars)) != NULL) {
        int segmentLen = (int)(found - text);

        ObjString* segment = copyString(text, segmentLen);
        push(OBJ_VAL(segment));
        arrayAppend(result, OBJ_VAL(segment));
        pop();

        text = found + sepLen;
    }

    ObjString* lastSegment = copyString(text, (int)strlen(text));
    arrayAppend(result, OBJ_VAL(lastSegment));

    return pop();
}

static Value stringTrimNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[-1]);
    char* start = str->chars;
    char* end = str->chars + str->length - 1;

    while (isspace(*start)) start++;

    while (end > start && isspace(*end)) end--;

    int newLength = (int)(end - start + 1);
    if (newLength <= 0) return OBJ_VAL(copyString("", 0));

    return OBJ_VAL(copyString(start, newLength));
}

static Value stringContainsNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return BOOL_VAL(false);
    }

    ObjString* haystack = AS_STRING(args[-1]);
    ObjString* needle = AS_STRING(args[0]);

    return BOOL_VAL(strstr(haystack->chars, needle->chars) != NULL);
}

static Value stringToUpperNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[-1]);

    char* buffer = (char*)malloc(str->length + 1);
    if (buffer == NULL) {
        runtimeError("to_upper() failed to allocate memory.");
        return NIL_VAL;
    }

    for (int i = 0; i < str->length; i++) {
        buffer[i] = toupper((unsigned char)str->chars[i]);
    }
    buffer[str->length] = '\0';

    return OBJ_VAL(takeString(buffer, str->length));
}

static Value stringToLowerNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[-1]);

    char* buffer = (char*)malloc(str->length + 1);
    if (buffer == NULL) {
        runtimeError("to_lower() failed to allocate memory.");
        return NIL_VAL;
    }

    for (int i = 0; i < str->length; i++) {
        buffer[i] = tolower((unsigned char)str->chars[i]);
    }
    buffer[str->length] = '\0';

    return OBJ_VAL(takeString(buffer, str->length));
}

static Value stringLenNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[-1]);
    return NUMBER_VAL((double)str->length);
}

static Value arrayStringNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    int count = array->count;
    uint8_t* buffer = ALLOCATE(uint8_t, count);

    for (int i = 0; i < count; i++) {
        Value v = array->values[i];
        if (!IS_NUMBER(v)) {
            FREE_ARRAY(uint8_t, buffer, count);
            runtimeError("Array containers non-number at index %d.", i);
            return NIL_VAL;
        }
        double num = AS_NUMBER(v);
        if (num < 0 || num > 255) {
            FREE_ARRAY(uint8_t, buffer, count);
            runtimeError("Byte value %g out of range (0-255).", num);
            return NIL_VAL;
        }
        buffer[i] = (uint8_t)num;
    }
    return OBJ_VAL(takeString((char*)buffer, count));
}

static Value arrayJoinNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[0])) {
        runtimeError("join() expects 1 string argument (separator).");
        return NIL_VAL;
    }

    ObjArray* array = AS_ARRAY(args[-1]);
    ObjString* sep = AS_STRING(args[0]);

    if (array->count == 0) return OBJ_VAL(copyString("", 0));

    int totalLength = 0;
    for (int i = 0; i < array->count; i++) {
        Value item = array->values[i];

        ObjString* s = valueToString(item);
        push(OBJ_VAL(s));
        totalLength += s->length;

        if (i < array->count - 1) {
            totalLength += sep->length;
        }
    }

    char* buffer = (char*)malloc(totalLength + 1);
    if (buffer == NULL) {
        runtimeError("Unable to allocate memory.");
        exit(1);
    }
    char* current = buffer;

    for (int i = 0; i < array->count; i++) {
        Value itemStr = vm.stackTop[-array->count + i];
        ObjString* s = AS_STRING(itemStr);

        memcpy(current, s->chars, s->length);
        current += s->length;

        if (i < array->count - 1) {
            memcpy(current, sep->chars, sep->length);
            current += sep->length;
        }
    }
    *current = '\0';

    ObjString* result = takeString(buffer, totalLength);

    popn(array->count);

    return OBJ_VAL(result);

}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

static ObjClass* getClassForValue(Value value) {
    if (IS_INSTANCE(value)) return AS_INSTANCE(value)->obj.klass;
    if (IS_CLASS(value)) return AS_CLASS(value)->obj.klass;
    if (IS_OBJ(value)) return AS_OBJ(value)->klass;
    if (IS_MAP(value)) return vm.mapClass;
    //if (IS_ARRAY(value)) return vm.arrayClass;
    if (IS_STRING(value)) return vm.stringClass;
    return NULL;
}

void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);
    int line = 0;

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame= &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        line = getLine(&function->chunk, instruction);
        fprintf(stderr, "[line %d] in ", line);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

void defineGlobal(const char* name, Value value) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(value);
    tableSet(&vm.globals, AS_STRING(peek(1)), peek(0));
    pop();
    pop();
}

void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    //tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    tableSet(&vm.globals, AS_STRING(peek(1)), peek(0));

    pop();
    pop();
}

/*
static void defineNativeInTable(Table* table, const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(table, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}
*/

Value popn(int n) {
    vm.stackTop -= n;
    return *vm.stackTop;
}

static void defineNativeMethod(ObjClass* klass, const char* name,
        NativeFn function) {
    ObjNative* native = newNative(function);
    push(OBJ_VAL(native));

    ObjString* methodName = copyString(name, (int)strlen(name));
    push(OBJ_VAL(methodName));

    tableSet(&klass->methods, methodName, OBJ_VAL(native));

    popn(2);
}

static Value systemTimeNative(int argCount, Value* args) {
    return NUMBER_VAL((double)time(NULL));
}

static Value systemExitNative(int argCount, Value* args) {
    int code = 0;
    if (argCount == 1 && IS_NUMBER(args[1])) {
        code = (int)AS_NUMBER(args[1]);
    }
    exit(code);
    return NIL_VAL; // technically never reached
}

static Value systemGCNative(int argCount, Value* args) {
    collectGarbage();
    return NIL_VAL;
}

static Value systemShowStackNative(int argCount, Value* args) {
    printf("[SHOW_STACK]: stack: %d\n", (int)(vm.stackTop - vm.stack));
    return NIL_VAL;
}

static Value systemResetStackNative(int argCount, Value* args) {
    for (Value* slot = vm.stackTop; slot < vm.stack + STACK_MAX; slot++) {
        *slot = NIL_VAL;
    }
    return NIL_VAL;
}

typedef struct {
    unsigned long size, resident, share, text, lib, data, dt;
} statm_t;

static Value systemMemNative(int argCount, Value* args) {
    ObjMap* memmap = newMap();
    push(OBJ_VAL(memmap));

    statm_t res;
    const char* statm_path = "/proc/self/statm";
    FILE *f = fopen(statm_path, "r");
    if (!f) {
        int errsv = errno;
        char *errmsg = strerror(errsv);
        runtimeError("Error reading statm: %s\n", errmsg);
        pop();
        return NIL_VAL;
    }

    if (7 != fscanf(f, "%ld %ld %ld %ld %ld %ld %ld",
                &res.size, &res.resident, &res.share, &res.text,
                &res.lib, &res.data, &res.dt)) {
        int errsv = errno;
        char *errmsg = strerror(errsv);
        runtimeError("Error parsing statm: %s\n", errmsg);
        return NIL_VAL;
    }
    ObjString* key;
    double val;
    key = copyString("size", 4);
    push(OBJ_VAL(key));
    val = res.size;
    push(NUMBER_VAL(val));
    tableSet(&memmap->items, key, NUMBER_VAL(val));
    popn(2);

    key = copyString("resident", 8);
    push(OBJ_VAL(key));
    val = res.resident;
    push(NUMBER_VAL(val));
    tableSet(&memmap->items, key, NUMBER_VAL(val));
    popn(2);

    key = copyString("share", 5);
    push(OBJ_VAL(key));
    val = res.share;
    push(NUMBER_VAL(val));
    tableSet(&memmap->items, key, NUMBER_VAL(val));
    popn(2);

    key = copyString("text", 4);
    push(OBJ_VAL(key));
    val = res.text;
    push(NUMBER_VAL(val));
    tableSet(&memmap->items, key, NUMBER_VAL(val));
    popn(2);

    /* unused
    key = copyString("lib", 3);
    push(OBJ_VAL(key));
    val = res.lib;
    push(NUMBER_VAL(val));
    tableSet(&memmap->items, key, NUMBER_VAL(val));
    popn(2);
    */

    key = copyString("data", 4);
    push(OBJ_VAL(key));
    val = res.data;
    push(NUMBER_VAL(val));
    tableSet(&memmap->items, key, NUMBER_VAL(val));
    popn(2);

    /* unused
    key = copyString("dt", 2);
    push(OBJ_VAL(key));
    val = res.data;
    push(NUMBER_VAL(val));
    tableSet(&memmap->items, key, NUMBER_VAL(val));
    popn(2);
    */
    pop();
    return OBJ_VAL(memmap);
}

static Value fileCloseNative(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[0]);
    if (inst->foreignPtr == stdout || inst->foreignPtr == stderr) return NIL_VAL;
    if (inst->foreignPtr != NULL) {
        fclose((FILE*)inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
    return NIL_VAL;
}

static Value fileReadNative(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[0]);
    FILE* handle = (FILE*)inst->foreignPtr;
    if  (!handle) return NIL_VAL;

    int length = -1;

    if (argCount >= 1 && IS_NUMBER(args[1])) {
        length = (int)AS_NUMBER(args[1]);
    } else {
        fseek(handle, 0L, SEEK_END);
        length = ftell(handle);
        rewind(handle);
    }

    char* buffer = (char*)malloc(length + 1);
    size_t bytesRead = fread(buffer, 1, length, handle);

    if (bytesRead == 0) {
        free(buffer);
        return NIL_VAL;
    }

    ObjString* result = copyString(buffer, (int)bytesRead);
    free(buffer);

    return OBJ_VAL(result);
}

static Value fileReadlineNative(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[0]);
    FILE* handle = (FILE*)inst->foreignPtr;
    if (!handle) return NIL_VAL;

    char lineBuffer[1024];
    if (fgets(lineBuffer, sizeof(lineBuffer), handle) == NULL) {
        return NIL_VAL;
    }

    return OBJ_VAL(copyString(lineBuffer, (int)strlen(lineBuffer)));
}

static Value fileWriteNative(int argCount, Value* args) {
    if (argCount <= 1) {
        runtimeError("File.write() expects 1 string argument (data).");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[0]);
    FILE* handle = (FILE*)inst->foreignPtr;

    if (handle) {
        fprintf(handle, "%s", AS_CSTRING(args[1]));
    }
    //return args[0]; // return self for chaining
    return NIL_VAL;
}

static Value fileFlushNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[0]);
    FILE* stream = (FILE*)instance->foreignPtr;
    if (stream) fflush(stream);
    return NIL_VAL;
}

static Value fileStderrNative(int argCount, Value* args) {
    Value fileClass;
    if (!tableGet(&vm.globals, copyString("File", 4), &fileClass)) {
        return NIL_VAL;
    }

    ObjInstance* instance = newInstance(AS_CLASS(fileClass));
    instance->foreignPtr = stderr;
    return OBJ_VAL(instance);
}

static Value fileOpenNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[1])) {
        runtimeError("File.open() expects t least a path string.");
        return NIL_VAL;
    }
    const char* path = AS_CSTRING(args[1]);
    const char* mode = "r";
    FILE* handle = NULL;

    if (argCount >= 2 && IS_STRING(args[2])) {
        mode = AS_CSTRING(args[2]);
    }

    if (strcmp(path, "STDOUT") == 0) {
        handle = stdout;
        mode = "w";
    } else if (strcmp(path, "STDERR") == 0) {
        handle = stderr;
        mode = "w";
    } else {
        handle = fopen(path, mode);
    }

    if (handle == NULL) {
        return NIL_VAL;
    }

    /*
    Value fileObj;
    if (!tableGet(&vm.globals, copyString("File", 4), &fileObj)) {
        runtimeError("Global 'File' class not found.");
        return NIL_VAL;
    }
    */

    ObjClass* fileClass = AS_CLASS(args[0]);
    ObjInstance* fileInst = newInstance(fileClass);
    fileInst->foreignPtr = handle;

    return OBJ_VAL(fileInst);
}

static Value fileLoadNative(int argCount, Value* args) {
    Value pathValue;
    if (IS_STRING(args[0])) {
        pathValue = args[0];
    } else if (argCount >= 1 && IS_STRING(args[1])) {
        pathValue = args[1];
    } else {
        runtimeError("File.load() expects a string path.");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(pathValue);
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NIL_VAL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        runtimeError("Not enough memory to read file.");
        return NIL_VAL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);

    return OBJ_VAL(takeString(buffer, (int)bytesRead));
}

static Value fileSeekNative(int argCount, Value* args) {
    if (argCount <= 2 || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        runtimeError("File.seek() requires 2 numbers");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[0]);
    FILE* handle = (FILE*)inst->foreignPtr;
    if (!handle) return NIL_VAL;

    long offset = (long)AS_NUMBER(args[1]);
    int whence = (int)AS_NUMBER(args[2]);

    int result = fseek(handle, offset, whence);
    return NUMBER_VAL(result);
}

static Value fileTellNative(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[0]);
    FILE* handle = (FILE*)inst->foreignPtr;
    if (!handle) return NIL_VAL;

    return NUMBER_VAL((double)ftell(handle));
}

static Value fileSaveNative(int argCount, Value* args) {
    if (argCount <= 2 || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        runtimeError("File.read() expects (path, content).");
        return NIL_VAL;
    }

    const char* path = AS_STRING(args[1])->chars;
    const char* content = AS_STRING(args[2])->chars;

    FILE* file = fopen(path, "w");
    if (file == NULL) return BOOL_VAL(false);

    fprintf(file, "%s", content);
    fclose(file);
    return BOOL_VAL(true);
}

static Value fileExistsNative(int argCount, Value* args) {
    if (argCount <= 1 || !IS_STRING(args[1])) return BOOL_VAL(false);
    FILE* file = fopen(AS_STRING(args[1])->chars, "r");
    if (file) {
        fclose(file);
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value fileListNative(int argCount, Value* args) {
    if (argCount <= 1 || !IS_STRING(args[1])) {
        runtimeError("File.read() expects 1 string argument (directory path).");
        return NIL_VAL;
    }

    const char* path = AS_STRING(args[1])->chars;
    DIR* dir = opendir(path);

    if (dir == NULL) {
        return NIL_VAL;
    }

    ObjArray* fileList = newArray();
    push(OBJ_VAL(fileList));

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        ObjString* name = copyString(entry->d_name, (int)strlen(entry->d_name));
        push(OBJ_VAL(name));
        arrayAppend(fileList, OBJ_VAL(name));
        pop();
    }

    closedir(dir);
    return pop();
}

static Value regexTestNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[1])) {
        runtimeError("test() expects 1 string argument.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    RegexInternal* re = (RegexInternal*)instance->foreignPtr;

    if (re == NULL) {
        runtimeError("Regex not initialized.");
        return NIL_VAL;
    }

    ObjString* subject = AS_STRING(args[1]);

    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re->code, NULL);
    int rc = pcre2_match(re->code, (unsigned char*)subject->chars, subject->length,
                0, 0, match_data, NULL);

    pcre2_match_data_free(match_data);
    return BOOL_VAL(rc >= 0);
}

static Value regexMatchNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[1])) {
        runtimeError("test() expects 1 string argument.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    RegexInternal* re = (RegexInternal*)instance->foreignPtr;

    if (re == NULL) {
        runtimeError("Regex not initialized.");
        return NIL_VAL;
    }

    ObjString* subject = AS_STRING(args[1]);

    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re->code, NULL);
    int rc = pcre2_match(re->code, (unsigned char*)subject->chars, subject->length,
                0, 0, match_data, NULL);

    if (rc < 0) {
        pcre2_match_data_free(match_data);
        return NIL_VAL;
    }

    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);

    ObjArray* results = newArray();
    push(OBJ_VAL(results));

    for (int i = 0; i < rc; i++) {
        int start = (int)ovector[2 * i];
        int end = (int)ovector[2 * i + 1];

        if (start == -1) {
            arrayAppend(results, NIL_VAL);
        } else {
            ObjString* matchStr = copyString(subject->chars + start, end - start);
            push(OBJ_VAL(matchStr));
            arrayAppend(results, OBJ_VAL(matchStr));
            pop();
        }
    }

    pcre2_match_data_free(match_data);
    return pop();
}

static Value regexInitMethod(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("Regex constructor expects a pattern string.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    ObjString* pattern = AS_STRING(args[0]);

    int errornumber;
    PCRE2_SIZE erroroffset;

    pcre2_code* code = pcre2_compile(
            (unsigned char*)pattern->chars, PCRE2_ZERO_TERMINATED,
            0, &errornumber, &erroroffset, NULL);

    if (code == NULL) {
        runtimeError("Regex compilation failed.");
        return NIL_VAL;
    }

    RegexInternal* internal = ALLOCATE(RegexInternal, 1);
    internal->code = code;
    internal->pattern = pattern;

    instance->foreignPtr = internal;

    return args[-1];
}

static Value regexGetPatternNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[0]);

    if (instance->foreignPtr == NULL) {
        runtimeError("Regex instance not initialized.");
        return NIL_VAL;
    }

    RegexInternal* internal = (RegexInternal*)instance->foreignPtr;
    return OBJ_VAL(internal->pattern);
}

static Value listFieldsNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[0]);

    ObjArray* array = newArray();
    push(OBJ_VAL(array));

    for (int i = 0; i < instance->fields.capacity; i++) {
        Entry* entry = &instance->fields.entries[i];
        if (entry->key != NULL) {
            arrayAppend(array, OBJ_VAL(entry->key));
        }
    }

    pop();

    return OBJ_VAL(array);
}

static Value getFieldNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("get_field() expects a string argument.");
        return NIL_VAL;
    }

    Value receiver = args[-1];
    ObjString* fieldName = AS_STRING(args[0]);

    if (IS_INSTANCE(receiver)) {
        ObjInstance* instance = AS_INSTANCE(receiver);
        Value value;
        if (tableGet(&instance->fields, fieldName, &value)) {
            return value;
        }
    }

    return NIL_VAL;
}

static Value setFieldNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[-1])) {
        runtimeError("get_field() expects string, value arguments.");
        return NIL_VAL;
    }

    Value receiver = args[-1];
    ObjString* fieldName = AS_STRING(args[0]);
    Value value = args[1];

    if (IS_INSTANCE(receiver)) {
        ObjInstance* instance = AS_INSTANCE(receiver);
        tableSet(&instance->fields, fieldName, value);
        return value;
    }

    runtimeError("Cannot set fields on built-in types.");
    return NIL_VAL;
}

static Value getSuperclassNative(int argCount, Value* args) {
    /*
    if (argCount != 1 || (!IS_CLASS(args[0]) && !IS_INSTANCE(args[0]))) {
        runtimeError("get_superclass() expects a class or instance as the argument.");
        return NIL_VAL;
    }
    */
    Obj* obj = AS_OBJ(args[0]);
    ObjClass* klass = NULL;

    if (!IS_OBJ(args[0])) {
        runtimeError("get_superclass() expects an object.");
        return NIL_VAL;
    }
    
    if (IS_CLASS(args[0])) {
        klass = AS_CLASS(args[0]);
    }

    if (IS_INSTANCE(args[0])) {
        ObjInstance* instance = AS_INSTANCE(args[0]);
        klass = instance->obj.klass;
    }

    if (obj->klass != NULL)
        klass = obj->klass;

    if (klass != NULL && klass->superclass != NULL) {
        return OBJ_VAL(klass->superclass);
    }

    return NIL_VAL;
}

static Value fromHexNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* str = AS_CSTRING(args[0]);
    //char* endptr;

    uint32_t result = (uint32_t)strtoul(str, NULL, 0);

    //if (str == endptr) return NIL_VAL;
    return NUMBER_VAL((double)result);
}

static Value fromBinNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[1])) return NIL_VAL;

    const char* str = AS_CSTRING(args[1]);
    char* endptr;

    unsigned long long result = strtoull(str, &endptr, 2);

    if (str == endptr) return NIL_VAL;
    return NUMBER_VAL((double)result);
}

static Value mathMinNative(int argCount, Value* args) {
    if (argCount != 3) return NIL_VAL;
    return NUMBER_VAL(fmin(AS_NUMBER(args[1]), AS_NUMBER(args[2])));
}

static Value mathMaxNative(int argCount, Value* args) {
    if (argCount != 3) return NIL_VAL;
    return NUMBER_VAL(fmax(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

static Value mathParseNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[1])) return NIL_VAL;

    const char* str = AS_CSTRING(args[1]);
    char* endptr;

    unsigned long long result = strtoull(str, &endptr, 0);

    if (str == endptr) return NIL_VAL;

    return NUMBER_VAL((double)result);
}

static Value vec3InitNative(int argCount, Value* args) {
    if (argCount != 3) {
        runtimeError("Need 3 arguments.");
        return NIL_VAL;
    }

    Vec3 v;
    v.x = AS_NUMBER(args[0]);
    v.y = AS_NUMBER(args[1]);
    v.z = AS_NUMBER(args[2]);

    return VEC3_VAL(v);
}

static Value vec3DotNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_VEC3(args[0]) || !IS_VEC3(args[1])) {
        return NIL_VAL;
    }

    Vec3 a = AS_VEC3(args[0]);
    Vec3 b = AS_VEC3(args[1]);

    return NUMBER_VAL((a.x * b.x) +
            (a.y * b.y) + (a.z * b.z));
}

static Value vec3UnitNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError("unit() expects 1 Vec3 argument.");
        return NIL_VAL;
    }
    Vec3 a = AS_VEC3(args[0]);

    double mag2 = a.x * a.x + a.y * a.y + a.z * a.z;
    if (mag2 > 0) {
        double invMag = 1.0 / sqrt(mag2);
        Vec3 v;
        v.x = a.x * invMag;
        v.y = a.y * invMag;
        v.z = a.z * invMag;
        return VEC3_VAL(v);
    }
    Vec3 v = {.x = 0, .y = 0, .z = 0};
    return VEC3_VAL(v);
}

static Value vec3CrossNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_VEC3(args[0]) || !IS_VEC3(args[1])) {
        runtimeError("cross() expects 2 Vec3 arguments.");
        return NIL_VAL;
    }
    Vec3 a = AS_VEC3(args[0]);
    Vec3 b = AS_VEC3(args[1]);
    Vec3 c;
    c.x = a.y * b.z - a.z * b.y;
    c.y = a.z * b.x - a.x * b.z;
    c.z = a.x * b.y - a.y * b.x;
    return VEC3_VAL(c);
}

static Value vec3AddNative(int argCount, Value* args) {
    if (argCount < 3 || !IS_VEC3(args[1])) return args[0];

    Vec3 a = AS_VEC3(args[0]);
    Vec3 b = AS_VEC3(args[1]);
    Vec3 c;
    c.x = a.x + b.x;
    c.y = a.y + b.y;
    c.z = a.z + b.z;
    return VEC3_VAL(c);
}

static Value vec3SubNative(int argCount, Value* args) {
    if (argCount < 3 || !IS_VEC3(args[1])) return args[0];

    Vec3 a = AS_VEC3(args[0]);
    Vec3 b = AS_VEC3(args[1]);
    Vec3 c;
    c.x = a.x - b.x;
    c.y = a.y - b.y;
    c.z = a.z - b.z;
    return VEC3_VAL(c);
}

static Value vec3MulNative(int argCount, Value* args) {
    if (argCount < 3 || !IS_VEC3(args[1])) return args[0];

    Vec3 a = AS_VEC3(args[0]);
    Vec3 b = AS_VEC3(args[1]);
    Vec3 c;
    c.x = a.x * b.x;
    c.y = a.y * b.y;
    c.z = a.z * b.z;
    return VEC3_VAL(c);
}

static Value vec3DivNative(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[1])) return args[0];

    Vec3 a = AS_VEC3(args[0]);
    double b = AS_NUMBER(args[1]);
    Vec3 c;
    c.x = a.x / b;
    c.y = a.y / b;
    c.z = a.z / b;
    return VEC3_VAL(c);
}

static Value vec3NegNative(int argCount, Value* args) {
    if (argCount < 2) return args[0];

    Vec3 a = AS_VEC3(args[0]);
    Vec3 b;
    b.x = -b.x;
    b.y = -b.y;
    b.z = -b.z;
    return VEC3_VAL(b);
}

static Value bitTestNative(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) return NIL_VAL;

    uint64_t num = (uint64_t)AS_NUMBER(args[1]);
    int bit = (int)AS_NUMBER(args[2]);

    if (bit < 0 || bit > 63) return BOOL_VAL(false);

    return BOOL_VAL((num >> bit) & 1);
}

static Value hexNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        return NIL_VAL;
    }

    uint64_t num = (uint64_t)AS_NUMBER(args[0]);

    int precision = (argCount >= 2 && IS_NUMBER(args[1])) ? (int)AS_NUMBER(args[1]) : 1;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "0x%.*llx", precision, (uint64_t)num);

    return OBJ_VAL(copyString(buffer, strlen(buffer)));
}

static Value octNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;

    uint64_t num = (uint64_t)AS_NUMBER(args[1]);
    int precision = (argCount == 3 && IS_NUMBER(args[2])) ? (int)AS_NUMBER(args[2]) : 1;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "0%.*llo", precision, (unsigned long long)num);

    return OBJ_VAL(copyString(buffer, strlen(buffer)));
}

static Value binNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;

    uint64_t num = (uint64_t)AS_NUMBER(args[1]);
    int min_bits = (argCount == 3 && IS_NUMBER(args[2])) ? (int)AS_NUMBER(args[2]) : 1;

    if (min_bits > 64) min_bits = 64;
    if (min_bits < 1) min_bits = 1;

    char buffer[70];
    char* p = buffer;
    *p++ = '0';
    *p++ = 'b';

    int highest_bit = 0;
    for (int i = 63; i >= 0; i--) {
        if ((num >> i) & 1) {
            highest_bit = i;
            break;
        }
    }
    int start_bit = (highest_bit >= min_bits) ? highest_bit : min_bits - 1;

    for (int i = start_bit; i >= 0; i--) {
        *p++ = ((num >> i) & 1) ? '1' : '0';
    }
    *p = '\0';

    return OBJ_VAL(copyString(buffer, (int)(p - buffer)));
}

static Value isNumberNative(int argCount, Value* args) {
    return BOOL_VAL(argCount > 0 && IS_NUMBER(args[0]));
}

static Value isStringNative(int argCount, Value* args) {
    return BOOL_VAL(argCount > 0 && IS_STRING(args[0]));
}

static Value isBoolNative(int argCount, Value* args) {
    return BOOL_VAL(argCount > 0 && IS_BOOL(args[0]));
}

static Value isNilNative(int argCount, Value* args) {
    return BOOL_VAL(argCount > 0 && IS_NIL(args[0]));
}

static Value isClassNative(int argCount, Value* args) {
    return BOOL_VAL(argCount > 0 && IS_CLASS(args[0]));
}

static Value isInstanceNative(int argCount, Value* args) {
    return BOOL_VAL(argCount > 0 && IS_INSTANCE(args[0]));
}

static Value typeofNative(int argCount, Value* args) {
    if (argCount < 1) return OBJ_VAL(copyString("NIL", 3));
    Value value = args[0];

    // 1. Handle primitives
    if (IS_NUMBER(value)) return OBJ_VAL(copyString("Number", 6));
    if (IS_BOOL(value)) return OBJ_VAL(copyString("Bool", 4));
    if (IS_NIL(value)) return OBJ_VAL(copyString("Nil", 3));
    if (IS_VEC3(value)) return OBJ_VAL(copyString("Vec3", 4));


    if (IS_OBJ(value)) {
        Obj* obj = AS_OBJ(value);

        // 2. Header promotion check
        // If it has a class, just return that class's name
        // This covers OBJ_ISNTANCE, OBJ_ARRAY, and any other promoted types.
        if (obj->klass != NULL) {
            return OBJ_VAL(obj->klass->name);
        }

        switch (OBJ_TYPE(value)) {
            case OBJ_STRING:
                return OBJ_VAL(copyString("String", 6));
            case OBJ_NATIVE:
                return OBJ_VAL(copyString("Native", 6));
            case OBJ_CLOSURE:
                return OBJ_VAL(copyString("Function", 8));
            case OBJ_CLASS:
                return OBJ_VAL(copyString("Class", 5));
            //case OBJ_INSTANCE:
            //    return OBJ_VAL(AS_INSTANCE(value)->obj.klass->name);
            //case OBJ_ARRAY:
            //    return OBJ_VAL(copyString("Array", 5));
            //case OBJ_MAP:
            //    return OBJ_VAL(copyString("Map", 3));
            default:
                return OBJ_VAL(copyString("Object", 6));
        }
    }

    return OBJ_VAL(copyString("UNKNOWN", 7));
}

void initMathLibrary() {
    ObjString* mathName = copyString("Math", 4);
    push(OBJ_VAL(mathName));
    ObjClass* mathClass = newClass(mathName);
    push(OBJ_VAL(mathClass));

    defineNativeMethod(mathClass, "sqrt", mathSqrtNative);
    defineNativeMethod(mathClass, "abs", mathAbsNative);
    defineNativeMethod(mathClass, "floor", mathFloorNative);
    defineNativeMethod(mathClass, "ceil", mathCeilNative);
    defineNativeMethod(mathClass, "random", mathRandomNative);
    defineNativeMethod(mathClass, "pi", mathPiNative);
    defineNativeMethod(mathClass, "exp", mathExpNative);
    defineNativeMethod(mathClass, "hex", hexNative);
    defineNativeMethod(mathClass, "oct", octNative);
    defineNativeMethod(mathClass, "bin", binNative);
    defineNativeMethod(mathClass, "bit_test", bitTestNative);
    defineNativeMethod(mathClass, "min", mathMinNative);
    defineNativeMethod(mathClass, "max", mathMaxNative);
    defineNativeMethod(mathClass, "parse", mathParseNative);
    defineNativeMethod(mathClass, "from_hex", fromHexNative);
    defineNativeMethod(mathClass, "from_bin", fromBinNative);
    //defineNativeMethod(mathClass, "ceil", mathCeilNative);
    defineNativeMethod(mathClass, "round", mathRoundNative);
    defineNativeMethod(mathClass, "to_number", toNumberNative);
    defineNativeMethod(mathClass, "sin", mathSinNative);
    defineNativeMethod(mathClass, "tan", mathTanNative);
    defineNativeMethod(mathClass, "atan2", mathAtan2Native);
    defineNativeMethod(mathClass, "cos", mathCosNative);
    defineNativeMethod(mathClass, "acos", mathAcosNative);
    defineNativeMethod(mathClass, "tan", mathTanNative);


    tableSet(&vm.globals, mathName, OBJ_VAL(mathClass));

    popn(2);

    srand((unsigned int)time(NULL));
}

void initSystemLibrary(int argc, const char* argv[], const char* env[]) {
    ObjString* systemName = copyString("System", 6);
    push(OBJ_VAL(systemName));
    ObjClass* systemClass = newClass(systemName);
    push(OBJ_VAL(systemClass));

    defineNativeMethod(systemClass, "time", systemTimeNative);
    defineNativeMethod(systemClass, "exit", systemExitNative);
    defineNativeMethod(systemClass, "gc", systemGCNative);
    defineNativeMethod(systemClass, "mem", systemMemNative);
    defineNativeMethod(systemClass, "reset_stack", systemResetStackNative);
    defineNativeMethod(systemClass, "show_stack", systemShowStackNative);

    tableSet(&vm.globals, systemName, OBJ_VAL(systemClass));

    ObjInstance* systemInstance = newInstance(systemClass);
    push(OBJ_VAL(systemInstance));

    tableSet(&systemInstance->fields, copyString("EXE", 3),
            OBJ_VAL(copyString(argv[0], strlen(argv[0]))));

    ObjArray* argsArray = newArray();
    push(OBJ_VAL(argsArray));

    for (int i = 2; i < argc; i++) {
        ObjString* argStr = copyString(argv[i], strlen(argv[i]));
        push(OBJ_VAL(argStr));
        arrayAppend(argsArray, OBJ_VAL(argStr));
        pop();
    }
    tableSet(&systemInstance->fields, copyString("ARGS", 4), OBJ_VAL(argsArray));

    ObjMap* envMap = newMap();
    push(OBJ_VAL(envMap));

    for (const char **envp = env; *envp != NULL; envp++) {
        const char *entry = *envp;
        char *sep = strchr(entry, '=');

        if (sep != NULL) {

            int keyLen = (int)(sep - entry);
            int valLen = (int)strlen(sep + 1);

            ObjString* key = copyString(entry, keyLen);
            push(OBJ_VAL(key));
            ObjString* val = copyString(sep + 1, valLen);
            push(OBJ_VAL(val));

            tableSet(&envMap->items, key, OBJ_VAL(val));
            pop();
            pop();
        }
    }
    tableSet(&systemInstance->fields, copyString("ENV", 3), OBJ_VAL(envMap));
    tableSet(&vm.globals, copyString("System", 6), OBJ_VAL(systemInstance));

    popn(5);
}

void fileDestructor(ObjInstance* inst) {
    if (inst->foreignPtr != NULL) {
        fclose((FILE*)inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
}

void initFileLibrary() {
    ObjString* fileName = copyString("File", 4);
    push(OBJ_VAL(fileName));
    ObjClass* fileClass = newClass(fileName);
    push(OBJ_VAL(fileClass));
    fileClass->destructor = fileDestructor;


    defineNativeMethod(fileClass, "load", fileLoadNative);
    defineNativeMethod(fileClass, "save", fileSaveNative);
    defineNativeMethod(fileClass, "exists", fileExistsNative);
    defineNativeMethod(fileClass, "list", fileListNative);
    defineNativeMethod(fileClass, "open", fileOpenNative);
    defineNativeMethod(fileClass, "read", fileReadNative);
    defineNativeMethod(fileClass, "readline", fileReadlineNative);
    defineNativeMethod(fileClass, "write", fileWriteNative);
    defineNativeMethod(fileClass, "close", fileCloseNative);
    defineNativeMethod(fileClass, "seek", fileSeekNative);
    defineNativeMethod(fileClass, "tell", fileTellNative);
    defineNativeMethod(fileClass, "stderr", fileStderrNative);
    defineNativeMethod(fileClass, "flush", fileFlushNative);

    tableSet(&vm.globals, fileName, OBJ_VAL(fileClass));

    popn(2);
}

void initVec3Library() {
    defineNative("Vec3", vec3InitNative);
    defineNative("dot", vec3DotNative);
    defineNative("cross", vec3CrossNative);
    defineNative("unit", vec3UnitNative);
}

static Value hgfGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;

    double val = AS_NUMBER(args[0]);
    // minimum of 1.1, can't turn off gc entirely
    if (val > 1.1) {
        vm.heap_growth_factor = val;
    }
    return NIL_VAL;
}

static Value get_hgfGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_grown_factor() takes 0 arguments.");
        return NIL_VAL;
    }

    return NUMBER_VAL(vm.heap_growth_factor);
}

static Value thresholdGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;

    uint32_t val = (uint32_t)AS_NUMBER(args[0]);
    vm.init_threshold = val;
    return NIL_VAL;
}

static Value get_thresholdGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_threshold() takes 0 arguments.");
        return NIL_VAL;
    }

    return NUMBER_VAL(vm.init_threshold);
}

static Value bumpsizeGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;

    uint32_t val = (uint32_t)AS_NUMBER(args[0]);
    vm.bump_size = val;
    return NIL_VAL;
}

static Value get_bumpsizeGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_bumpsize() takes 0 arguments.");
        return NIL_VAL;
    }

    return NUMBER_VAL(vm.bump_size);
}

static Value stressmodeGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;

    vm.stress_mode = AS_NUMBER(args[0]);
    return NIL_VAL;
}

static Value get_stressmodeGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_stressmode() takes 0 arguments.");
        return NIL_VAL;
    }

    return NUMBER_VAL(vm.stress_mode);
}

static Value typeGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;

    int type = AS_NUMBER(args[0]);
    if (type == 0) {
        vm.gctype = 0;
    } else {
        vm.gctype = 1;
    }
    return NIL_VAL;
}

static Value get_typeGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_gctype() takes 0 arguments.");
        return NIL_VAL;
    }
    if (vm.gctype == 0) {
        return OBJ_VAL(copyString("linear", 6));
    } else if (vm.gctype == 1) {
        return OBJ_VAL(copyString("multiplier", 10));
    }
    // shouldn't get here
    return NIL_VAL;
}

void initGCLibrary() {
    ObjString* gcName = copyString("GC", 2);
    push(OBJ_VAL(gcName));
    ObjClass* gcClass = newClass(gcName);
    push(OBJ_VAL(gcClass));

    defineNativeMethod(gcClass, "heap_growth_factor", hgfGCNative);
    defineNativeMethod(gcClass, "get_growth_factor", get_hgfGCNative);
    defineNativeMethod(gcClass, "init_threshold", thresholdGCNative);
    defineNativeMethod(gcClass, "get_threshold", get_thresholdGCNative);
    defineNativeMethod(gcClass, "bump_size", bumpsizeGCNative);
    defineNativeMethod(gcClass, "get_bumpsize", get_bumpsizeGCNative);
    defineNativeMethod(gcClass, "stress_mode", stressmodeGCNative);
    defineNativeMethod(gcClass, "get_stress_mode", get_stressmodeGCNative);
    defineNativeMethod(gcClass, "type", typeGCNative);
    defineNativeMethod(gcClass, "get_gctype", get_typeGCNative);
    // same as System.gc()
    defineNativeMethod(gcClass, "gc", systemGCNative);

    tableSet(&vm.globals, gcName, OBJ_VAL(gcClass));

    popn(2);
}

void regexDestructor(ObjInstance* inst) {
    if (inst->foreignPtr != NULL) {
        RegexInternal* re = (RegexInternal*)inst->foreignPtr;
        pcre2_code_free(re->code);
        inst->foreignPtr = NULL;
    }
}

void initRegexClass() {
    ObjString* string = NULL;
    string = copyString("Regex", 5);
    vm.regexClass = newClass(copyString("Regex", 5));
    vm.regexClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.regexClass));
    push(OBJ_VAL(vm.regexClass));

    defineNativeMethod(vm.regexClass, "init", regexInitMethod);
    defineNativeMethod(vm.regexClass, "test", regexTestNative);
    defineNativeMethod(vm.regexClass, "match", regexMatchNative);
    defineNativeMethod(vm.regexClass, "get_pattern", regexGetPatternNative);

    //defineNative("Regex", regexInitNative);
    vm.regexClass->destructor = regexDestructor;
    defineGlobal("Regex", OBJ_VAL(vm.regexClass));

    pop();
}

static Value arrayInitMethod(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("Array constructor expects a pattern string.");
        return NIL_VAL;
    }

    ObjArray* array = newArray();
    array->obj.klass = vm.arrayClass;
    array->count = 0;
    array->capacity = 0;
    array->values = NULL;
    return OBJ_VAL(array);
}

static Value mapInitMethod(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("Regex constructor expects a pattern string.");
        return NIL_VAL;
    }
}

static Value stringInitMethod(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("Regex constructor expects a pattern string.");
        return NIL_VAL;
    }
}

static Value objectClassMethod(int argCount, Value* args) {
    Obj* obj = AS_OBJ(args[-1]);
    return OBJ_VAL(obj->klass);
}

static Value arrayNativeConstructor(int argCount, Value* args) {
    ObjArray* array = newArray();
    array->obj.klass = vm.arrayClass;

    if (argCount > 0) {
        array->values = ALLOCATE(Value, argCount);
        array->capacity = argCount;
        array->count = argCount;

        for (int i = 0; i < argCount; i++) {
            array->values[i] = args[i];
        }
    }
    return OBJ_VAL(array);
}

static Value mapNativeConstructor(int argCount, Value* args) {
    ObjMap* map = newMap();
    map->obj.klass = vm.mapClass;
    push(OBJ_VAL(map));

    for (int i = 0; i < argCount; i+=2 ) {
        Value value = args[i];
        Value key = args[i+1];

        if (!IS_STRING(key)) {
            runtimeError("Map keys must be strings.");
            return NIL_VAL;
        }
        tableSet(&map->items, AS_STRING(key), value);
    }

    return OBJ_VAL(map);
}

void initArrayClass() {
    ObjString* string = NULL;

    string = copyString("Array", 5);
    vm.arrayClass = newClass(string);
    vm.arrayClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.arrayClass));
    push(OBJ_VAL(vm.arrayClass));

    defineNative("Array", arrayNativeConstructor);

    //defineNativeMethod(vm.arrayClass, "init", arrayInitMethod);
    defineNativeMethod(vm.arrayClass, "push", arrayPushNative);
    defineNativeMethod(vm.arrayClass, "pop", arrayPopNative);
    defineNativeMethod(vm.arrayClass, "len", arrayLenNative);
    defineNativeMethod(vm.arrayClass, "map", arrayMapNative);
    defineNativeMethod(vm.arrayClass, "dup", arrayDupNative);
    defineNativeMethod(vm.arrayClass, "is_empty", arrayIsEmptyNative);
    defineNativeMethod(vm.arrayClass, "select", arraySelectNative);
    defineNativeMethod(vm.arrayClass, "reduce", arrayReduceNative);
    defineNativeMethod(vm.arrayClass, "join", arrayJoinNative);
    defineNativeMethod(vm.arrayClass, "each", arrayEachNative);
    defineNativeMethod(vm.arrayClass, "find", arrayFindNative);
    defineNativeMethod(vm.arrayClass, "slice", arraySliceNative);
    defineNativeMethod(vm.arrayClass, "sort", arraySortNative);
    defineNativeMethod(vm.arrayClass, "sort_slice", arraySortSliceNative);
    defineNativeMethod(vm.arrayClass, "reverse", arrayReverseNative);
    defineNativeMethod(vm.arrayClass, "flatten", arrayFlattenNative);
    defineNativeMethod(vm.arrayClass, "to_string", arrayStringNative);
    pop();
}


void initMapClass() {
    ObjString* string = NULL;

    string = copyString("Map", 3);
    vm.mapClass = newClass(string);
    vm.mapClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.mapClass));
    push(OBJ_VAL(vm.mapClass));

    defineNative("Map", mapNativeConstructor);

    //defineNativeMethod(vm.mapClass, "init", mapInitMethod);
    defineNativeMethod(vm.mapClass, "keys", mapKeysNative);
    defineNativeMethod(vm.mapClass, "values", mapValuesNative);
    defineNativeMethod(vm.mapClass, "has", mapHasNative);
    defineNativeMethod(vm.mapClass, "remove", mapRemoveNative);
    defineNativeMethod(vm.mapClass, "len", mapLenNative);
    pop();
}

void initStringClass() {
    ObjString* string = NULL;

    string = copyString("String", 6);
    vm.stringClass = newClass(string);
    vm.stringClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.stringClass));
    push(OBJ_VAL(vm.stringClass));

    //defineNativeMethod(vm.stringClass, "init", stringInitMethod);
    defineNativeMethod(vm.stringClass, "trim", stringTrimNative);
    defineNativeMethod(vm.stringClass, "contains", stringContainsNative);
    defineNativeMethod(vm.stringClass, "to_upper", stringToUpperNative);
    defineNativeMethod(vm.stringClass, "to_lower", stringToLowerNative);
    defineNativeMethod(vm.stringClass, "len", stringLenNative);
    defineNativeMethod(vm.stringClass, "split", stringSplitNative);
    defineNativeMethod(vm.stringClass, "slice", stringSliceNative);
    defineNativeMethod(vm.stringClass, "to_array", stringToarrayNative);
    pop();
}

static Value objectGetSuperclassMethod(int argCount, Value* args) {
    Obj* obj = AS_OBJ(args[-1]);

    if (obj->klass != NULL && obj->klass->superclass != NULL) {
        return OBJ_VAL(obj->klass->superclass);
    }

    return NIL_VAL;
}

static Value packInt32(int argCount, Value* args) {
}

static Value packByte(int argCount, Value* args) {
}

void initVM(int argc, const char* argv[], const char* env[]) {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.isGC = false;
    vm.heap_growth_factor = 2.0;
    vm.init_threshold = 0;
    vm.nextGC = 1024 * 1024;
    vm.bump_size = 1024 * 1024 * 64;
    vm.stress_mode = 0; // 0 = normal, 1 = always, 2 = never
    vm.gctype = 1;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    vm.moduleCount = 0;
    vm.moduleCapacity = 0;
    vm.moduleHandles = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings);
    //initTable(&vm.giTypes);

    vm.initString = NULL;
    vm.initString = copyString("init", 4);
    vm.toString = NULL;
    vm.toString = copyString("to_string", 9);
    vm.str_add = NULL;
    vm.str_add = copyString("__add__", 7);
    vm.str_sub = NULL;
    vm.str_sub = copyString("__sub__", 7);
    vm.str_mul = NULL;
    vm.str_mul = copyString("__mul__", 7);
    vm.str_div = NULL;
    vm.str_div = copyString("__div__", 7);
    vm.str_neg = NULL;
    vm.str_neg = copyString("__neg__", 7);
    vm.xString = copyString("x", 1);
    vm.yString = copyString("y", 1);
    vm.zString = copyString("z", 1);

    defineNative("clock", clockNative);
    defineNative("str", strNative);
    defineNative("typeof", typeofNative);
    defineNative("isnumber", isNumberNative);
    defineNative("isstring", isStringNative);
    defineNative("isbool", isBoolNative);
    defineNative("isnil", isNilNative);
    defineNative("isclass", isClassNative);
    defineNative("isinstance", isInstanceNative);
    defineNative("packInt32", packInt32);
    defineNative("packByte", packByte);

    ObjString* string = NULL;

    string = copyString("Object", 6);
    vm.objectClass = newClass(string);
    vm.objectClass->superclass = NULL;
    tableSet(&vm.globals, string, OBJ_VAL(vm.objectClass));

    /*
    string = copyString("Number", 6);
    vm.numberClass = newClass(string);
    vm.numberClass->superclass = NULL;
    tableSet(&vm.globals, string, OBJ_VAL(vm.numberClass));

    string = copyString("Bool", 5);
    vm.boolClass = newClass(string);
    vm.boolClass->superclass = NULL;
    tableSet(&vm.globals, string, OBJ_VAL(vm.boolClass));

    string = copyString("Nil", 5);
    vm.nilClass = newClass(string);
    vm.nilClass->superclass = NULL;
    tableSet(&vm.globals, string, OBJ_VAL(vm.nilClass));
    */

    //defineNative("get_class", objectClassMethod);
    
    /*
    string = copyString("Array", 5);
    vm.arrayClass = newClass(string);
    vm.arrayClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.arrayClass));

    string = copyString("Map", 3);
    vm.mapClass = newClass(string);
    vm.mapClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.mapClass));

    string = copyString("String", 6);
    vm.stringClass = newClass(string);
    vm.stringClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.stringClass));

    string = copyString("Regex", 5);
    vm.regexClass = newClass(copyString("Regex", 5));
    vm.regexClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.regexClass));
    */

    vm.moduleClass = newClass(copyString("Module", 6));

    defineNativeMethod(vm.objectClass, "fields", listFieldsNative);
    defineNativeMethod(vm.objectClass, "get_field", getFieldNative);
    defineNativeMethod(vm.objectClass, "set_field", setFieldNative);
    defineNativeMethod(vm.objectClass, "get_methods", getMethodsNative);
    defineNativeMethod(vm.objectClass, "has_method", hasMethodNative);
    defineNativeMethod(vm.objectClass, "responds_to", hasMethodNative);
    defineNativeMethod(vm.objectClass, "get_superclass", objectGetSuperclassMethod);

    initMathLibrary();
    initSystemLibrary(argc, argv, env);
    initFileLibrary();
    initRegexClass();
    initVec3Library();
    initGCLibrary();
    initArrayClass();
    initMapClass();
    initStringClass();

}

void freeVM() {
    freeObjects();

    freeTable(&vm.globals);
    freeTable(&vm.strings);

    vm.initString = NULL;
    vm.toString = NULL;
    vm.str_add = NULL;
    vm.str_sub = NULL;
    vm.str_mul = NULL;
    vm.str_div = NULL;

    for (int i = 0; i < vm.moduleCount; i++) {
        if (vm.moduleHandles[i] != NULL) {
            dlclose(vm.moduleHandles[i]);
        }
    }
    FREE_ARRAY(void*, vm.moduleHandles, vm.moduleCapacity);
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

bool vmCall(ObjClosure* closure, int argCount) {
    ObjFunction* function = closure->function;
    int namedArity = function->isVariadic ? function->arity - 1 : function->arity;

    if (argCount < namedArity) {
        int missing = namedArity - argCount;
        for (int i = 0; i < missing; i++) {
            int defaultIndex = (function->defaults.count - missing) + i;
            push(function->defaults.values[defaultIndex]);
        }
        argCount = namedArity;
    }

    if (function->isVariadic) {
        if (argCount < function->minArity) {
            runtimeError("Expected at least %d arguments but got %d.",
                    function->minArity, argCount);
            return false;
        }
    } else {
        if (argCount < closure->function->minArity || argCount > closure->function->arity) {
            runtimeError("Expected between %d and %d arguments but got %d.",
                    closure->function->minArity, closure->function->arity, argCount);
            return false;
        }
    }

    if (function->isVariadic) {
        int numRest = argCount - namedArity;
        if (numRest < 0) numRest = 0;

        ObjArray* restArray = newArray();
        push(OBJ_VAL(restArray));

        for (int i = 0; i < numRest; i++) {
            //Value val = peek(numRest - i + 1);
            Value val = vm.stackTop[-(numRest + 1) + i];
            arrayAppend(restArray, val);
        }
        vm.stackTop -= (numRest + 1);
        push(OBJ_VAL(restArray));

        argCount = namedArity + 1;
    } 

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - closure->function->arity - 1;
    return true;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD:
                {
                    ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                    vm.stackTop[-argCount - 1] = bound->receiver;
                    return callValue(bound->method, argCount);
                    /*
                    vm.stackTop[-argCount - 1] = bound->receiver;
                    if (IS_CLOSURE(bound->method)) {
                        ObjClosure* closure = AS_CLOSURE(bound->method);
                        return vmCall(closure, argCount);
                    } else if (IS_NATIVE(bound->method)) {
                        NativeFn native = AS_NATIVE(bound->method);
                        Value result = native(argCount, vm.stackTop - argCount);
                        return true;
                    }
                    return false;
                    */
                    //return vmCall(bound->method, argCount);
                }
            case OBJ_CLASS:
                {
                    ObjClass* klass = AS_CLASS(callee);

                    if (klass->callHandler != NULL) {
                        Value result = klass->callHandler(argCount, vm.stackTop - argCount);

                        if (vm.frameCount == 0) return false;

                        vm.stackTop -= argCount + 1;
                        push(result);
                        return true;
                    }

                    vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                    Value initializer;
                    if (tableGet(&klass->methods, vm.initString,
                                &initializer)) {
                        if (IS_NATIVE(initializer)) {
                            NativeFn native = AS_NATIVE(initializer);
                            Value result = native(argCount, vm.stackTop - argCount);

                            vm.stackTop -= argCount + 1;
                            push(result);
                            return true;
                        } else {
                            return vmCall(AS_CLOSURE(initializer), argCount);
                        }
                    } else if (argCount != 0) {
                        runtimeError("Expect 0 arguments but got %d.", argCount);
                        return false;
                    }
                    return true;
                }
            case OBJ_CLOSURE:
                return vmCall(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE:
                {
                    //printf("[CALLVALUE] OBJ_NATIVE\n");
                    NativeFn native = AS_NATIVE(callee);
                    Value result = native(argCount, vm.stackTop - argCount);
                    //if (vm.frameCount == 0) return false;

                    vm.stackTop -= argCount + 1;
                    push(result);
                    //printf("[CALLVALUE] OBJ_NATIVE done\n");

                    return true;
                }
            default:
                break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool isTruthy(Value value) {
    return !IS_NIL(value) || (IS_BOOL(value) && AS_BOOL(value));
}

static bool findMethod(ObjClass* klass, ObjString* name, Value* method) {
    ObjClass* current = klass;
    int i = 0;
    while (current != NULL) {
        if (tableGet(&current->methods, name, method)) {
            return true;
        }
        current = current->superclass;
        i++;
    }
    return false;
}

static bool invokeFromClass(ObjClass* klass, ObjString* name,
        int argCount) {
    ObjClass* current = klass;
    Value method;

    while (current != NULL) {
        //printf("Looking for '%s' in class '%s'\n", name->chars, current->name->chars);
        if (tableGet(&current->methods, name, &method)) {
            return callValue(method, argCount);
        }
        current = current->superclass;
    }

    if (IS_NATIVE(method)) {
        NativeFn native = AS_NATIVE(method);

        // 1. Capture the receiver (currently at -argCount - 1)
        Value receiver = vm.stackTop[-argCount - 1];

        // 2. Put the method obj where the reciver was (this becomes args[-1])
        vm.stackTop[-argCount - 1] = method;

        // 3. shift, move all exist arguments up one slot
        // we go backward from the top
        for (int i = 0; i < argCount; i++) {
            vm.stackTop[-i] = vm.stackTop[-i - 1];
        }
        
        // 4. place the receiver in the now vacant first argument slot
        vm.stackTop[-argCount] = receiver;
        vm.stackTop++;

        // 5. call the function (total args is now argCoutn + 1)
        Value result = native(argCount + 1, vm.stackTop - argCount - 1);

        // 6. cleanup
        vm.stackTop -= (argCount + 2);
        push(result);
        return true;
    }

    if (IS_CLOSURE(method)) {
        return vmCall(AS_CLOSURE(method), argCount);
    }

    runtimeError("Undefined property '%s'.", name->chars);
    return false;
}

static bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);

    if (IS_INSTANCE(receiver) || IS_VEC3(receiver)) {
        ObjInstance* instance = AS_INSTANCE(receiver);

        Value value;
        if (tableGet(&instance->fields, name, &value)) {
            vm.stackTop[-argCount - 1] = value;
            return callValue(value, argCount);
        }

        return invokeFromClass(instance->obj.klass, name, argCount);
    }

    if (!IS_OBJ(receiver) && !IS_ARRAY(receiver)) {
        runtimeError("Only objects have methods.");
        return false;
    }

    Obj* obj = AS_OBJ(receiver);
    ObjClass* klass = NULL;

    if (IS_ARRAY(receiver)) {
        klass = AS_ARRAY(receiver)->obj.klass;
    }

    if (obj->klass != NULL) {
        return invokeFromClass(obj->klass, name, argCount);
    }

    //if (IS_ARRAY(receiver)) klass = vm.arrayClass;
    if (IS_MAP(receiver)) klass = vm.mapClass;
    else if (IS_STRING(receiver)) klass = vm.stringClass;
    else if (IS_REGEX(receiver)) klass = vm.regexClass;

    if (klass != NULL) {
        return invokeFromClass(klass, name, argCount);
    }

    if (IS_CLASS(receiver)) {
        ObjClass* klass = AS_CLASS(receiver);
        return invokeFromClass(klass, name, argCount);
    }

    runtimeError("Only instances and collections have methods.");
    return false;
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
    Value method;
    ObjClass* current = klass;

    while (current != NULL) {
        if (tableGet(&klass->methods, name, &method)) {
            ObjBoundMethod* bound = newBoundMethod(peek(0),
                    method);
                    //AS_CLOSURE(method));
            pop();
            push(OBJ_VAL(bound));
            return true;
        }
        current = current->superclass;
    }

    runtimeError("Undefined property '%s'.", name->chars);
    return false;
}

static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL &&
            vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod(ObjString* name) {
    Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    tableSet(&klass->methods, name, method);
    pop();
}

static void concatenate() {
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

Value numberToValue(double num) {
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "%g", num);
    return OBJ_VAL(copyString(buffer, length));
}


static inline uint32_t read24(uint8_t* ip) {
    return(ip[0] << 16) | (ip[1] << 8) | ip[2];
}

InterpretResult run() {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];
    //printf("STACK DEPTH: %ld | FRAME: %d | OP: %d\n",
     //       (long)(vm.stackTop - vm.stack), vm.frameCount, *frame->ip);

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, \
     (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_24BIT() \
    (frame->ip +=3, (uint32_t)((frame->ip[-3] <<16) | (frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
//#define READ_CONSTANT_LONG(i) (frame->closure->function->chunk.constants.values[i])
#define READ_CONSTANT_LONG() \
    (frame->ip += 3, \
     frame->closure->function->chunk.constants.values[read24(frame->ip - 3)])

#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_STRING_LONG() \
    AS_STRING(READ_CONSTANT_LONG())
#define BINARY_OP(valueType, op) \
    do { \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("        ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(&frame->closure->function->chunk,
                (int)(frame->ip - frame->closure->function->chunk.code));
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT:
                {
                    Value constant = READ_CONSTANT();
                    push(constant);
                }
                break;
            case OP_CONSTANT_LONG:
                {
                    /*
                    uint8_t b1 = READ_BYTE();
                    uint8_t b2 = READ_BYTE();
                    uint8_t b3 = READ_BYTE();
                    //int index = b1 | (b2 << 8) | (b3 << 16);
                    int index = (b1 << 16) | (b2 << 8) | b3;
                    */

                    //Value constant = frame->closure->function->chunk.constants.values[index];
                    //Value constant = READ_CONSTANT_LONG(index);
                    Value constant = READ_CONSTANT_LONG();
                    push(constant);
                }
                break;
            case OP_STR:
                {
                    Value value = peek(0);
                    if (IS_STRING(value)) break;

                    char buffer[32];
                    int length = 0;

                    if (IS_NUMBER(value)) {
                        length = snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(value));
                    } else if (IS_BOOL(value)) {
                        length = snprintf(buffer, sizeof(buffer), AS_BOOL(value) ? "true" : "false");
                    } else if (IS_NIL(value)) {
                        length = snprintf(buffer, sizeof(buffer), "nil");
                    } else {
                        runtimeError("Cannot convert value to string.");
                    }

                    pop();
                    push(OBJ_VAL(copyString(buffer, length)));
                }
                break;
            case OP_NIL:
                push(NIL_VAL);
                break;
            case OP_TRUE:
                push(BOOL_VAL(true));
                break;
            case OP_FALSE:
                push(BOOL_VAL(false));
                break;
            case OP_POP:
                pop();
                break;
            case OP_POPN:
                {
                    int n = READ_BYTE();
                    popn(n);
                }
                break;
            case OP_GET_LOCAL:
                {
                    uint8_t slot = READ_BYTE();
                    Value val = frame->slots[slot];
                    push(val);
                }
                break;
            case OP_GET_LOCAL_LONG:
                {
                    int slot = READ_24BIT();
                    Value val = frame->slots[slot];
                    push(val);
                }
                break;
            case OP_SET_LOCAL:
                {
                    uint8_t slot = READ_BYTE();
                    frame->slots[slot] = peek(0);
                }
                break;
            case OP_SET_LOCAL_LONG:
                {
                    int slot = READ_24BIT();
                    frame->slots[slot] = peek(0);
                }
                break;
            case OP_GET_GLOBAL:
                {
                    ObjString* name = READ_STRING();
                    Value value;
                    if (!tableGet(&vm.globals, name, &value)) {
                        runtimeError("Undefined variable '%s'.", name->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    push(value);
                }
                break;
            case OP_GET_GLOBAL_LONG:
                {
                    ObjString* name = READ_STRING_LONG();
                    Value value;
                    if (!tableGet(&vm.globals, name, &value)) {
                        runtimeError("Undefined variable '%s'.", name->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    push(value);
                }
                break;
            case OP_DEFINE_GLOBAL:
                {
                    ObjString* name = READ_STRING();
                    tableSet(&vm.globals, name, peek(0));
                    pop();
                }
                break;
            case OP_DEFINE_GLOBAL_LONG:
                {
                    //uint32_t index = READ_24BIT();
                    //ObjString* name = AS_STRING(frame->closure->function->chunk.constants.values[index]);
                    ObjString* name = READ_STRING_LONG();
                    tableSet(&vm.globals, name, peek(0));
                    pop();
                }
                break;
            case OP_SET_GLOBAL:
                {
                    ObjString* name = READ_STRING();
                    if (tableSet(&vm.globals, name, peek(0))) {
                        tableDelete(&vm.globals, name);
                        runtimeError("Undefined variable '%s'.", name->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_SET_GLOBAL_LONG:
                {
                    ObjString* name = READ_STRING_LONG();
                    if (tableSet(&vm.globals, name, peek(0))) {
                        tableDelete(&vm.globals, name);
                        runtimeError("Undefined variable '%s'.", name->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_GET_PROPERTY:
            case OP_GET_PROPERTY_LONG:
                {
                    ObjString* name = (instruction == OP_GET_PROPERTY)
                        ? READ_STRING()
                        : READ_STRING_LONG();
                    
                    Value receiver = peek(0);

                    if (IS_VEC3(receiver)) {
                        Vec3 vec = AS_VEC3(receiver);
                        if (name == vm.xString) {
                            pop();
                            push(NUMBER_VAL(vec.x));
                        } else if (name == vm.yString) {
                            pop();
                            push(NUMBER_VAL(vec.y));
                        } else if (name == vm.zString) {
                            pop();
                            push(NUMBER_VAL(vec.z));
                        }
                        break;
                    } 

                    if (IS_INSTANCE(receiver)) {
                        ObjInstance* instance = AS_INSTANCE(receiver);

                        if (instance->obj.klass == NULL) {
                            runtimeError("Instance has no class.");
                            return INTERPRET_RUNTIME_ERROR;
                        }

                        Value value;
                        if (tableGet(&instance->fields, name, &value)) {
                            pop();
                            push(value);
                            break;
                        }

                        if (instance->obj.klass->getter != NULL) {
                            value = instance->obj.klass->getter(instance, name);
                            if (!IS_NIL(value)) {
                                pop();
                                push(value);
                                break;
                            }
                        }

                        if (instance->obj.klass != NULL) {
                            if (findMethod(instance->obj.klass, name, &value)) {
                                ObjBoundMethod* bound = newBoundMethod(peek(0), value);
                                pop();
                                push(OBJ_VAL(bound));
                                break;
                            }
                        }
                        runtimeError("Undefined property '%s'.", name->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    } 

                    ObjClass* klass = getClassForValue(receiver);
                    if (klass != NULL) {
                        Value method;
                        if (findMethod(klass, name, &method)) {

                            ObjBoundMethod* bound = newBoundMethod(receiver, method);
                            pop();
                            push(OBJ_VAL(bound));
                            break;
                        }
                    }

                    runtimeError("Property '%s' not found.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            case OP_SET_PROPERTY:
            case OP_SET_PROPERTY_LONG:
                {
                    if (!IS_INSTANCE(peek(1))) { // && !IS_VEC3(peek(1))) 
                    //if (!IS_INSTANCE(peek(1)))
                        runtimeError("Only instances have fields.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjInstance* instance = AS_INSTANCE(peek(1));
                    ObjString* name = (instruction == OP_SET_PROPERTY)
                        ? READ_STRING()
                        : READ_STRING_LONG();

                    Value value = peek(0);

                    //if (IS_VEC3(peek(1))) {
                        /*
                        Vec3 vec = AS_VEC3(peek(1));

                        if (name->length == 1) {
                            double val = AS_NUMBER(value);
                            switch (name->chars[0]) {
                                case 'x':
                                    vec.x = val;
                                    break;
                                case 'y':
                                    vec.y = val;
                                    break;
                                case 'z':
                                    vec.z = val;
                                    break;
                                default:
                                    runtimeError("Vec3 properties are restricted to x, y, z.");
                                    break;
                            }
                            popn(2);
                            push(value);
                            break;
                        } else {
                        */
                        //popn(2);
                        //runtimeError("Vec3 properties are read-only.");
                        //return INTERPRET_RUNTIME_ERROR;
                        //}
                        //break;
                    //}
                    if (instance->obj.klass->setter != NULL) {
                        if (instance->obj.klass->setter(instance, name, value)) {
                            pop();
                            pop();
                            push(value);
                            break;
                        }
                    }

                    tableSet(&instance->fields, name, value);
                    pop();
                    pop();
                    push(value);
                    break;
                }
            case OP_GET_SUPER:
                {
                    ObjString* name = READ_STRING();
                    ObjClass* superclass = AS_CLASS(pop());

                    if (!bindMethod(superclass, name)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_EQUAL:
                {
                    Value b = pop();
                    Value a = pop();
                    push(BOOL_VAL(valuesEqual(a, b)));
                }
                break;
            case OP_GET_UPVALUE:
                {
                    uint16_t slot = (READ_BYTE() << 8);
                    slot |= READ_BYTE();
                    push(*frame->closure->upvalues[slot]->location);
                }
                break;
            case OP_SET_UPVALUE:
                {
                    uint16_t slot = (READ_BYTE() << 8);
                    slot |= READ_BYTE();
                    *frame->closure->upvalues[slot]->location = peek(0);
                }
                break;
            case OP_GREATER:
                BINARY_OP(BOOL_VAL, >);
                break;
            case OP_LESS:
                BINARY_OP(BOOL_VAL, <);
                break;
            case OP_ADD:
                {

                    if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                        concatenate();
                        break;
                    } 
                    if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                        double b = AS_NUMBER(pop());
                        double a = AS_NUMBER(pop());
                        push(NUMBER_VAL(a + b));
                    } else if (IS_VEC3(peek(1)) && IS_NUMBER(peek(0))) {
                        double d = AS_NUMBER(pop());
                        Vec3 a = AS_VEC3(pop());
                        Vec3 b;
                        b.x = a.x + d;
                        b.y = a.y + d;
                        b.z = a.z + d;
                        push(VEC3_VAL(b));
                    } else if (IS_NUMBER(peek(1)) && IS_VEC3(peek(0))) {
                        Vec3 b = AS_VEC3(pop());
                        double d = AS_NUMBER(pop());
                        Vec3 a;
                        a.x = b.x + d;
                        a.y = b.y + d;
                        a.z = b.z + d;
                        push(VEC3_VAL(a));
                    } else if (IS_VEC3(peek(1)) && IS_VEC3(peek(0))) {
                        Vec3 b = AS_VEC3(pop());
                        Vec3 a = AS_VEC3(pop());
                        Vec3 c;
                        c.x = a.x + b.x;
                        c.y = a.y + b.y;
                        c.z = a.z + b.z;
                        push(VEC3_VAL(c));
                    } else if (IS_INSTANCE(peek(1))) {
                        ObjInstance* instance = AS_INSTANCE(peek(1));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->obj.klass->methods, vm.str_add, &method)) {
                            if (callValue(method, 1)) {
                                vm.nativeExitDepth = vm.frameCount - 1;
                                run();
                                result = pop();
                            }
                        }
                        vm.stackTop = stackStart;
                        popn(2);
                        push(result);
                    } else {
                        runtimeError("Invalid operands.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_SUBTRACT:
                {
                    if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                        double b = AS_NUMBER(pop());
                        double a = AS_NUMBER(pop());
                        push(NUMBER_VAL(a - b));
                    } else if (IS_VEC3(peek(1)) && IS_NUMBER(peek(0))) {
                        double d = AS_NUMBER(pop());
                        Vec3 a = AS_VEC3(pop());
                        Vec3 b;
                        b.x = a.x - d;
                        b.y = a.y - d;
                        b.z = a.z - d;
                        push(VEC3_VAL(b));
                    } else if (IS_VEC3(peek(1)) && IS_VEC3(peek(0))) {
                        Vec3 b = AS_VEC3(pop());
                        Vec3 a = AS_VEC3(pop());
                        Vec3 c;
                        c.x = a.x - b.x;
                        c.y = a.y - b.y;
                        c.z = a.z - b.z;
                        push(VEC3_VAL(c));
                    } else if (IS_INSTANCE(peek(1))) {
                        ObjInstance* instance = AS_INSTANCE(peek(1));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->obj.klass->methods, vm.str_sub, &method)) {
                            if (callValue(method, 1)) {
                                vm.nativeExitDepth = vm.frameCount - 1;
                                run();
                                result = pop();
                            }
                        }
                        vm.stackTop = stackStart;
                        popn(2);
                        push(result);
                    } else {
                        runtimeError("Invalid operands.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_MULTIPLY:
                {
                    if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                        double b = AS_NUMBER(pop());
                        double a = AS_NUMBER(pop());
                        push(NUMBER_VAL(a * b));
                    } else if (IS_VEC3(peek(1)) && IS_NUMBER(peek(0))) {
                        double d = AS_NUMBER(pop());
                        Vec3 a = AS_VEC3(pop());
                        Vec3 b;
                        b.x = a.x * d;
                        b.y = a.y * d;
                        b.z = a.z * d;
                        push(VEC3_VAL(b));
                    } else if (IS_NUMBER(peek(1)) && IS_VEC3(peek(0))) {
                        Vec3 b = AS_VEC3(pop());
                        double d = AS_NUMBER(pop());
                        Vec3 a;
                        a.x = b.x * d;
                        a.y = b.y * d;
                        a.z = b.z * d;
                        push(VEC3_VAL(a));
                    } else if (IS_VEC3(peek(1)) && IS_VEC3(peek(0))) {
                        Vec3 b = AS_VEC3(pop());
                        Vec3 a = AS_VEC3(pop());
                        Vec3 c;
                        c.x = a.x * b.x;
                        c.y = a.y * b.y;
                        c.z = a.z * b.z;
                        push(VEC3_VAL(c));
                    } else if (IS_INSTANCE(peek(1))) {
                        ObjInstance* instance = AS_INSTANCE(peek(1));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->obj.klass->methods, vm.str_mul, &method)) {
                            if (callValue(method, 1)) {
                                vm.nativeExitDepth = vm.frameCount - 1;
                                run();
                                result = pop();
                            }
                        }
                        vm.stackTop = stackStart;
                        popn(2);
                        push(result);
                    } else {
                        runtimeError("Invalid operands.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_DIVIDE:
                {
                    if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                        double b = AS_NUMBER(pop());
                        double a = AS_NUMBER(pop());
                        push(NUMBER_VAL(a / b));
                    } else if (IS_VEC3(peek(1)) && IS_NUMBER(peek(0))) {
                        double d = AS_NUMBER(pop());
                        Vec3 a = AS_VEC3(pop());
                        Vec3 b;
                        b.x = a.x / d;
                        b.y = a.y / d;
                        b.z = a.z / d;
                        push(VEC3_VAL(b));
                    } else if (IS_INSTANCE(peek(1))) {
                        ObjInstance* instance = AS_INSTANCE(peek(1));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->obj.klass->methods, vm.str_div, &method)) {
                            if (callValue(method, 1)) {
                                vm.nativeExitDepth = vm.frameCount - 1;
                                run();
                                result = pop();
                            }
                        }
                        vm.stackTop = stackStart;
                        popn(2);
                        push(result);
                    } else {
                        runtimeError("Invalid operands.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_POW:
                {
                    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                        runtimeError("Operands must be numbers.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    Value b = pop();
                    Value a = pop();
                    push(NUMBER_VAL(pow(AS_NUMBER(a), AS_NUMBER(b))));
                }
                break;
            case OP_XOR:
                {
                    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                        runtimeError("Operands must be numbers.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    uint32_t b = valueToUint32(pop());
                    uint32_t a = valueToUint32(pop());

                    uint32_t result = a ^ b;
                    push(NUMBER_VAL((double)result));
                }
                break;
            case OP_MOD:
                {
                    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                        runtimeError("Operands must be numbers.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());

                    if (b == 0) {
                        runtimeError("Division by zero.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    push(NUMBER_VAL(fmod(a, b)));
                }
                break;
            case OP_BITWISE_NOT:
                {
                    uint32_t a = valueToUint32(pop());
                    push(NUMBER_VAL((double)~a));
                    //uint32_t val = (uint32_t)AS_NUMBER(pop());

                    //push(NUMBER_VAL((double)~val));
                }
                break;
            case OP_SHL:
                {
                    uint32_t amount = valueToUint32(pop());
                    uint32_t value = valueToUint32(pop());

                    // masking the amount by 31 is a common cpu behavior to prevent
                    // undefined behavior with shifts >= bit width.
                    push(NUMBER_VAL((double)(value << (amount & 31))));
                }
                break;
            case OP_SHR:
                {
                    uint32_t amount = valueToUint32(pop());
                    uint32_t value = valueToUint32(pop());

                    // using uint32_t ensures a LOGICAL shift (fills with 0)
                    // rather than an ARITHMETIC shift (fills with sign bit)
                    push(NUMBER_VAL((double)(value >> (amount & 31))));
                }
                break;
            case OP_NEGATE:
                {
                    if (IS_NUMBER(peek(0))) {
                        push(NUMBER_VAL(-AS_NUMBER(pop())));
                    } else if (IS_VEC3(peek(0))) {
                        Vec3 a = AS_VEC3(pop());
                        Vec3 b;
                        b.x = -a.x;
                        b.y = -a.y;
                        b.z = -a.z;
                        push(VEC3_VAL(b));
                    } else if (IS_INSTANCE(peek(0))) {
                        ObjInstance* instance = AS_INSTANCE(peek(0));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->obj.klass->methods, vm.str_neg, &method)) {
                            if (callValue(method, 0)) {
                                vm.nativeExitDepth = vm.frameCount - 1;
                                run();
                                result = pop();
                            }
                        }
                        vm.stackTop = stackStart;
                        push(result);
                    } else {
                        runtimeError("Operand must be a number.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_BITWISE_AND:
                {
                    uint32_t b = valueToUint32(pop());
                    uint32_t a = valueToUint32(pop());

                    uint32_t result = a & b;
                    push(NUMBER_VAL((double)result));
                }
                break;
            case OP_BITWISE_OR:
                {
                    uint32_t b = valueToUint32(pop());
                    uint32_t a = valueToUint32(pop());

                    uint32_t result = a | b;
                    push(NUMBER_VAL((double)result));
                }
                break;
            case OP_PRINT:
                {
                    int argCount = READ_BYTE();

                    for (int i = argCount - 1; i >= 0; i--) {
                        Value value = peek(i);
                        if (IS_INSTANCE(value)) {
                            ObjInstance* instance = AS_INSTANCE(value);
                            Value method;

                            Value* stackStart = vm.stackTop;
                            if (tableGet(&instance->obj.klass->methods, vm.toString, &method)) {
                                push(value);
                                if (callValue(method, 0)) {
                                    vm.nativeExitDepth = vm.frameCount - 1;
                                    run();
                                    Value result = pop();

                                    if (!IS_NIL(result)) {
                                        printValue(result);
                                    }
                                }
                            } else {
                                printValue(value);
                            }
                            vm.stackTop = stackStart;
                            //pop();
                        } else {
                            printValue(value);
                            if (i > 0) printf(" ");
                        }
                    }
                    popn(argCount);
                    printf("\n");
                }
                break;
            case OP_JUMP:
                {
                    uint16_t offset = READ_SHORT();
                    frame->ip += offset;
                }
                break;
            case OP_JUMP_IF_FALSE:
                {
                    uint16_t offset = READ_SHORT();
                    if (isFalsey(peek(0))) frame->ip += offset;
                }
                break;
            case OP_JUMP_IF_TRUE:
                {
                    uint16_t offset = READ_SHORT();
                    if (!isFalsey(peek(0))) frame->ip += offset;
                }
                break;
            case OP_LOOP:
                {
                    uint16_t offset = READ_SHORT();
                    frame->ip -= offset;
                }
                break;
            case OP_DUP:
                push(peek(0));
                break;
            case OP_CALL:
                {
                    int argCount = READ_BYTE();
                    if (!callValue(peek(argCount), argCount) || vm.frameCount == 0) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    frame = &vm.frames[vm.frameCount - 1];
                }
                break;
            case OP_INVOKE:
            case OP_INVOKE_LONG:
                {
                    /*
                    printf("[DEBUG STACK]: ");
                    for (int i = 0; i < (vm.stackTop - vm.stack); i++) {
                        printValue(vm.stack[i]);
                        printf(" | ");
                    }
                    printf("\n");
                    */

                    ObjString* method = (instruction == OP_INVOKE)
                        ? READ_STRING()
                        : READ_STRING_LONG();
                    int argCount = READ_BYTE();
                    int receiverIndex = (vm.stackTop - vm.stack) - 1 - argCount;
                    Value receiver = peek(argCount);

                    /*
                    printf("[INVOKE]: %s with %d args. Receiver type: %d\n", method->chars, argCount, receiver.type);
                    printf("[INVOKE]: %s | Args: %d | Stack Depth: %ld | Looking at Index: %d | Type: %d\n",
                            method->chars, argCount, (vm.stackTop - vm.stack), receiverIndex, receiver.type);
                    */

                    if (IS_VEC3(receiver)) {
                        Vec3 vec = AS_VEC3(peek(argCount));

                        if (method->length == 6 && memcmp(method->chars, "length", 6) == 0) {
                            if (argCount != 0) {
                                runtimeError("method length() expects 0 arguments.");
                                return INTERPRET_RUNTIME_ERROR;
                            }

                            double len = sqrt(vec.x * vec.x +
                                    vec.y * vec.y + vec.z * vec.z);
                            popn(argCount + 1);
                            push(NUMBER_VAL(len));
                            break;
                        } else if (method->length == 14 && memcmp(method->chars, "length_squared", 14) == 0) {
                            if (argCount != 0) {
                                runtimeError("method length() expects 0 arguments.");
                                return INTERPRET_RUNTIME_ERROR;
                            }
                            
                            double len = vec.x * vec.x +
                                    vec.y * vec.y + vec.z * vec.z;
                            popn(argCount + 1);
                            push(NUMBER_VAL(len));
                            break;
                        } else if (method->length == 5 && memcmp(method->chars, "cross", 5) == 0) {
                            if (argCount != 1) {
                                runtimeError("method cross() expects 1 Vec3 argument.");
                                return INTERPRET_RUNTIME_ERROR;
                            }

                            Vec3 b = AS_VEC3(pop());
                            Vec3 a = AS_VEC3(pop());
                            Vec3 c;
                            c.x = a.y * b.z - a.z * b.y;
                            c.y = a.z * b.x - a.x * b.z;
                            c.z = a.x * b.y - a.y * b.x;
                            push(VEC3_VAL(c));
                            break;
                        } else if (method->length == 4 && memcmp(method->chars, "unit", 4) == 0) {
                            if (argCount != 0) {
                                runtimeError("method unit() expects 0 arguments.");
                                return INTERPRET_RUNTIME_ERROR;
                            }

                            double mag2 = vec.x * vec.x + vec.y * vec.y +
                                    vec.z * vec.z;
                            if (mag2 > 0) {
                                double invMag = 1.0 / sqrt(mag2);
                                Vec3 a;
                                a.x = vec.x * invMag;
                                a.y = vec.y * invMag;
                                a.z = vec.z * invMag;
                                pop();
                                push(VEC3_VAL(a));
                                break;
                            } else {
                                pop();
                                Vec3 a = {.x = 0, .y = 0, .z = 0};
                                push(VEC3_VAL(a));
                                break;
                            }
                        } else if (method->length == 3 && memcmp(method->chars, "dot", 3) == 0) {
                            if (argCount != 1) {
                                runtimeError("Method dot() expects 1 argument.");
                                return INTERPRET_RUNTIME_ERROR;
                            }
                            if (!IS_VEC3(peek(0))) {
                                runtimeError("Dot product argument must be a Vec3.");
                                return INTERPRET_RUNTIME_ERROR;
                            }

                            Vec3 other = AS_VEC3(pop());
                            pop();
                            double result = (vec.x * other.x) +
                                (vec.y * other.y) + (vec.z * other.z);
                            push(NUMBER_VAL(result));
                        } else {
                            runtimeError("Method %s does not exist.", method->chars);
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        break;
                    }

                    if (IS_OBJ(receiver)) {
                        //printValue(receiver);
                        Obj* obj = AS_OBJ(receiver);

                        // 1. If its a class, look at its own methods (Static Call)
                        if (IS_CLASS(receiver)) {
                            ObjClass* klass = AS_CLASS(receiver);
                            if (invokeFromClass(klass, method, argCount)) {
                                frame = &vm.frames[vm.frameCount - 1];
                                break;
                            }
                        }

                        // 2. Otherwise, look at the methods if its class (Instance call)
                        if (invokeFromClass(obj->klass, method, argCount)) {
                            frame = &vm.frames[vm.frameCount - 1];
                        } else if (!invoke(method, argCount) || vm.frameCount == 0) {
                            return INTERPRET_RUNTIME_ERROR;
                        } else {
                            frame = &vm.frames[vm.frameCount - 1];
                        }
                        break;
                    }
                    printf("CRASH PREVENTED: Receiver is not an object! Type: %d\n", receiver.type);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            case OP_INVOKE_SPLAT:
                {
                    ObjString* method = READ_STRING();
                    int staticCount = READ_BYTE();
                    int dynamicCount = 0;

                    if (IS_SPLAT_COUNT(peek(0))) {
                        dynamicCount = AS_SPLAT_COUNT(pop());
                    } else {
                        Value sentinel = peek(staticCount);
                        if (IS_SPLAT_COUNT(sentinel)) {
                            dynamicCount = AS_SPLAT_COUNT(sentinel);

                            for (int i = staticCount; i > 0; i--) {
                                vm.stackTop[-i - 1] = vm.stackTop[-i];
                            }
                            vm.stackTop--;
                        }
                    }

                    int totalArgs = staticCount + dynamicCount;

                    Value receiver = peek(totalArgs);

                    if (!IS_INSTANCE(receiver)) {
                        runtimeError("Only instances have methods.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjInstance* instance = AS_INSTANCE(receiver);
                    if (!invokeFromClass(instance->obj.klass, method, totalArgs)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    frame = &vm.frames[vm.frameCount - 1];
                }
                break;
            case OP_SUPER_INVOKE:
                {
                    ObjString* method = READ_STRING();
                    int argCount = READ_BYTE();
                    ObjClass* superclass = AS_CLASS(pop());
                    if (!invokeFromClass(superclass, method, argCount)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    frame = &vm.frames[vm.frameCount - 1];
                }
                break;
            case OP_CLOSURE:
            case OP_CLOSURE_LONG:
                {
                    ObjFunction* function = NULL;
                    if (instruction == OP_CLOSURE) {
                        function = AS_FUNCTION(READ_CONSTANT());
                    } else {
                        function = AS_FUNCTION(READ_CONSTANT_LONG());
                    }

                    ObjClosure* closure = newClosure(function);
                    push(OBJ_VAL(closure));
                    for (int i = 0; i < closure->upvalueCount; i++) {
                        uint8_t isLocal = READ_BYTE();
                        uint16_t index = (READ_BYTE() << 8) | READ_BYTE();
                        if (isLocal) {
                            closure->upvalues[i] =
                                captureUpvalue(frame->slots + index);
                        } else {
                            closure->upvalues[i] = frame->closure->upvalues[index];
                        }
                    }
                }
                break;
            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;
            case OP_IMPORT: 
            case OP_IMPORT_LONG:
                {
                    ObjString* moduleName;
                    if (instruction == OP_IMPORT) {
                        moduleName = AS_STRING(READ_CONSTANT());
                    } else {
                        moduleName = AS_STRING(READ_CONSTANT_LONG());
                    }

                    /*
                    if (access(moduleName->chars, F_OK) == -1) {
                        runtimeError("Module file not found at %s", moduleName->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    */

                    void* handle = loadModule(moduleName->chars);
                    if (handle == NULL) {
                        runtimeError("Could not load module.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    //tableSet(&vm.globals, moduleName, peek(0));
                    pop();
                }
                break;
            case OP_CALL_SPLAT:
                {
                    //int dynamicCount = (int)AS_NUMBER(pop());
                    int staticCount = READ_BYTE();
                    int dynamicCount = 0;

                    if (IS_SPLAT_COUNT(peek(0))) {
                        dynamicCount = AS_SPLAT_COUNT(pop());
                    } else {
                        Value sentinel = peek(staticCount);
                        if (IS_SPLAT_COUNT(sentinel)) {
                            dynamicCount = AS_SPLAT_COUNT(sentinel);

                            for (int i = staticCount; i > 0; i--) {
                                vm.stackTop[-i - 1] = vm.stackTop[-i];
                            }
                            vm.stackTop--;
                        }
                    }

                    int totalArgs = dynamicCount + staticCount;

                    Value callee = peek(totalArgs);
                    if (!callValue(callee, totalArgs)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    frame = &vm.frames[vm.frameCount - 1];
                }
                break;
            case OP_SPLAT:
                {
                    Value value = peek(0);
                    if (!IS_ARRAY(value)) {
                        runtimeError("Can only splat arrays.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjArray* array = AS_ARRAY(value);
                    pop();
                    int count = array->count;
                    //int currentTotal = AS_NUMBER(pop());

                    if (vm.stackTop > vm.stack && IS_SPLAT_COUNT(peek(0))) {
                        count += AS_SPLAT_COUNT(pop());
                    }

                    if (vm.stackTop + array->count >= vm.stack + STACK_MAX) {
                        runtimeError("Stack overflow during splat.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    for (int i = 0; i < array->count; i++) {
                        push(array->values[i]);
                    }

                    //push(NUMBER_VAL((double)array->count + currentTotal));
                    push(SPLAT_COUNT_VAL(count));

                }
                break;
            case OP_INCLUDE:
                {
                    Value mixinVal = peek(0);
                    Value targetVal = peek(1);

                    if (!IS_CLASS(mixinVal) || !IS_CLASS(targetVal)) {
                        runtimeError("Only classes can be included.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjClass* mixin = AS_CLASS(mixinVal);
                    ObjClass* target = AS_CLASS(targetVal);

                    tableMergeGuard(&mixin->methods, &target->methods);
                    pop();
                }
                break;
            case OP_RETURN:
                {
                    Value result = pop();
                    closeUpvalues(frame->slots);
                    vm.frameCount--;
                    if (vm.frameCount == 0) {
                        pop();
                        return INTERPRET_OK;
                    }

                    vm.stackTop = frame->slots;
                    push(result);

                    if  (vm.frameCount == vm.nativeExitDepth) {
                        vm.nativeExitDepth = -1;
                        return INTERPRET_OK;
                    }

                    frame = &vm.frames[vm.frameCount - 1];
                }
                break;
            case OP_CLASS:
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
            case OP_CLASS_LONG:
                push(OBJ_VAL(newClass(READ_STRING_LONG())));
                break;
            case OP_INHERIT:
                {
                    Value superclass = peek(1);
                    if (!IS_CLASS(superclass)) {
                        runtimeError("Superclass must be a class.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjClass* subclass = AS_CLASS(peek(0));
                    subclass->superclass = AS_CLASS(superclass);
                    //tableAddAll(&AS_CLASS(superclass)->methods,
                    //        &subclass->methods);
                    pop();
                }
                break;
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
            case OP_METHOD_LONG:
                defineMethod(READ_STRING_LONG());
                break;
            case OP_MAP:
                {
                    uint8_t itemCount = READ_BYTE();
                    ObjMap* map = newMap();
                    map->obj.klass = vm.mapClass;
                    push(OBJ_VAL(map));

                    for (int i = 0; i < itemCount; i++) {
                        Value value = peek(1);
                        Value key = peek(2);

                        if (!IS_STRING(key)) {
                            runtimeError("Map keys must be strings.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        tableSet(&map->items, AS_STRING(key), value);
                        Value mapVal = pop();
                        popn(2);
                        push(mapVal);
                    }
                }
                break;
            case OP_ARRAY:
                {
                    uint8_t count = READ_BYTE();
                    
                    ObjArray* array = newArray();
                    array->obj.klass = vm.arrayClass;
                    push(OBJ_VAL(array));

                    if (count > 0) {
                        Value* entries = ALLOCATE(Value, count);
                        array->values = entries;
                        array->capacity = count;
                        array->count = count;
                    }

                    for (int i = count - 1; i >= 0; i--) {
                        array->values[i] = vm.stackTop[- (count - i + 1)];
                    }

                    Value arrayVal = pop();
                    vm.stackTop -= count;
                    push(arrayVal);
                }
                break;
            case OP_ARRAY_FILL:
                {
                    Value sizeVal = peek(0);
                    Value element = peek(1);

                    if (!IS_NUMBER(sizeVal)) {
                        runtimeError("Array size must be a number.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    int count = (int)AS_NUMBER(sizeVal);
                    ObjArray* array = newArray();
                    array->obj.klass = vm.arrayClass;
                    push(OBJ_VAL(array));
                    if (count > 0) {
                        Value* entries = ALLOCATE(Value, count);
                        array->values = entries;
                        array->capacity = count;
                        array->count = count;
                        for (int i = 0; i < count; i++) {
                            array->values[i] = element;
                        }
                    }
                    popn(3);
                    push(OBJ_VAL(array));
                }
                break;
            case OP_GET_INDEX:
                {
                    Value indexValue = pop();
                    Value targetValue = pop();

                    if (IS_MAP(targetValue)) {
                        if (!IS_STRING(indexValue)) {
                            runtimeError("Map index must be a string.");
                            return INTERPRET_RUNTIME_ERROR;
                        }

                        Value result;
                        if (tableGet(&AS_MAP(targetValue)->items, AS_STRING(indexValue), &result)) {
                            push(result);
                        } else {
                            push(NIL_VAL);
                        }
                        break;
                    }

                    if (IS_VEC3(targetValue)) {
                        if (!IS_NUMBER(indexValue)) {
                            runtimeError("Vec3 index must be a number.");
                            return INTERPRET_RUNTIME_ERROR;
                        }

                        int index = (int)AS_NUMBER(indexValue);
                        if (index < 0 || index > 2) {
                            runtimeError("Array index out of bounds.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        Vec3 vec3 = AS_VEC3(targetValue);
                        if (index == 0)
                            push(NUMBER_VAL(vec3.x));
                        if (index == 1)
                            push(NUMBER_VAL(vec3.y));
                        if (index == 2)
                            push(NUMBER_VAL(vec3.z));
                        break;
                    }

                    if (IS_ARRAY(targetValue)) {
                        ObjArray* array = AS_ARRAY(targetValue);

                        if (!IS_NUMBER(indexValue)) {
                            runtimeError("Array index must be a number.");
                            return INTERPRET_RUNTIME_ERROR;
                        }

                        int index = (int)AS_NUMBER(indexValue);
                        if (index < 0 || index >= array->count) {
                            runtimeError("Array index out of bounds.");
                            return INTERPRET_RUNTIME_ERROR;
                        }

                        push(array->values[index]);
                        break;
                    }

                    if (IS_STRING(targetValue)) {
                        if (!IS_NUMBER(indexValue)) {
                            runtimeError("String index must be a number.");
                            return INTERPRET_RUNTIME_ERROR;
                        }

                        ObjString* string = AS_STRING(targetValue);
                        int index = AS_NUMBER(indexValue);

                        if (index < 0 || index >= string->length) {
                            runtimeError("String index out of bounds.");
                            return INTERPRET_RUNTIME_ERROR;
                        }

                        push(NUMBER_VAL((double)(uint8_t)string->chars[index]));

                        break;
                    }

                    runtimeError("Only vec3s, maps and arrays support subscripting.");
                    return INTERPRET_RUNTIME_ERROR;

                }
                break;
            case OP_SET_INDEX:
                {
                    Value newValue = peek(0);
                    Value indexValue = peek(1);
                    Value targetValue = peek(2);

                    if (IS_MAP(targetValue)) {
                        if (!IS_STRING(indexValue)) {
                            runtimeError("Map keys must be strings.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        tableSet(&AS_MAP(targetValue)->items, AS_STRING(indexValue), newValue);
                        popn(3);
                        push(newValue);
                        break;
                    } else if (!IS_ARRAY(targetValue)) {
                        runtimeError("Only maps and arrays support subscript assignment.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjArray* array = AS_ARRAY(targetValue);

                    if (!IS_NUMBER(indexValue)) {
                        runtimeError("Array index must be a number.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    int index = (int)AS_NUMBER(indexValue);
                    if (index < 0 || index >= array->count) {
                        runtimeError("Array index out of bounds.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    array->values[index] = newValue;
                    // pop args
                    popn(3);
                    // push result
                    push(newValue);
                }
                break;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    vmCall(closure, 0);

    return run();
}

