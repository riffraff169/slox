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

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;
InterpretResult run();
//void initArrayMethods();

static bool callValue(Value callee, int argCount);
Value peek(int distance);
Value popn(int n);
static bool isFalsey(Value value);

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

static Value getMembersNative(int argCount, Value* args) {
    if (argCount < 1 || (!IS_INSTANCE(args[0]) && !IS_CLASS(args[0]))) {
        runtimeError("getMembers() expects a class or instance argument.");
        return NIL_VAL;
    }

    ObjArray* list = newArray(0);
    push(OBJ_VAL(list));

    Table* table;
    if (IS_INSTANCE(args[0])) {
        table = &AS_INSTANCE(args[0])->fields;
    } else {
        table = &AS_CLASS(args[0])->methods;
    }

    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key != NULL) {
            push(OBJ_VAL(entry->key));
            arrayAppend(list, OBJ_VAL(entry->key));
            pop();
        }
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
    if (argCount < 2) return NUMBER_VAL(0);

    if (IS_NUMBER(args[1])) return args[1];

    if (!IS_STRING(args[1])) return NUMBER_VAL(0);

    char* end;
    const char* str = AS_CSTRING(args[1]);
    double number = strtod(str, &end);

    if (str == end) return NUMBER_VAL(0);

    return NUMBER_VAL(number);
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
    }
    vm.stackTop = comparisonStackBase;
    return 0; // default to equal
}

static Value arraySortNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[0]);
    if (array->count < 2) return args[0];

    if (argCount >= 2 && IS_CLOSURE(args[1])) {
        qsort_r(array->values, array->count, sizeof(Value),
                loxSortComparator, AS_CLOSURE(args[1]));
    } else {
        // default sort (fast c)
        qsort(array->values, array->count, sizeof(Value),
                defaultSortComparator);
    }
    return args[0];
}

static Value arraySliceNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[0]);
    int count = array->count;

    int start = (argCount >= 2 && IS_NUMBER(args[1])) ?  (int)AS_NUMBER(args[1]) : 0;
    if (start < 0) start = count + start;
    if (start < 0) start = 0;
    if (start > count) start = count;

    int end = (argCount >= 3 && IS_NUMBER(args[2])) ?  (int)AS_NUMBER(args[2]) : count;
    if (end < 0) end = count + end;
    if (end < 0) end = 0;
    if (end > count) end = count;

    ObjArray* result = newArray(0);
    push(OBJ_VAL(result));

    if (end > start) {
        for (int i = start; i < end; i++) {
            arrayAppend(result, array->values[i]);
        }
    }

    return pop();
}

static Value arrayFindNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_CLOSURE(args[1])) return NIL_VAL;
    ObjArray* array = AS_ARRAY(args[0]);
    ObjClosure* callback = AS_CLOSURE(args[1]);

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
    if (argCount < 2 || !IS_CLOSURE(args[1])) return NIL_VAL;
    ObjArray* array = AS_ARRAY(args[0]);
    ObjClosure* callback = AS_CLOSURE(args[1]);

    Value* stackStart = vm.stackTop;

    for (int i = 0; i < array->count; i++) {
        push(args[1]);
        push(array->values[i]);

        vm.nativeExitDepth = vm.frameCount - 1;

        if (vmCall(callback, 1)) {
            run();
        }

        vm.stackTop = stackStart;
    }
    return NIL_VAL;
}

static Value arrayPushNative(int argCount, Value* args) {
    if (argCount < 1) return NIL_VAL;

    ObjArray* array = AS_ARRAY(args[0]);
    for (int i = 1; i < argCount; i++) {
        arrayAppend(array, args[i]);
    }
    return OBJ_VAL(array);
}

static Value arrayPopNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[0]);

    if (array->count == 0) {
        return NIL_VAL;
    }

    Value lastValue = array->values[array->count - 1];
    array->count--;
    array->values[array->count] = NIL_VAL;
    return lastValue;
}

static Value arrayReduceNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[1])) {
        return NIL_VAL;
    }

    ObjArray* array = AS_ARRAY(args[0]);
    Value callback = args[1];

    Value acc = (argCount > 1) ? args[2] : NIL_VAL;
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
    if (argCount < 1 || !IS_CLOSURE(args[1])) {
        return NIL_VAL;
    }

    ObjArray* original = AS_ARRAY(args[0]);
    Value callback = args[1];
    ObjArray* result = newArray(0);
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
    if (!IS_ARRAY(args[0])) return NIL_VAL;

    ObjArray* original = AS_ARRAY(args[0]);
    ObjArray* copy = duplicateArray(original);

    return OBJ_VAL(copy);
}

static Value arrayIsEmptyNative(int argCount, Value* args) {
    return BOOL_VAL(AS_ARRAY(args[0])->count == 0);
}

static Value arrayMapNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[1])) {
        return NIL_VAL;
    }

    ObjArray* original = AS_ARRAY(args[0]);
    Value callback = args[1];
    ObjArray* result = newArray(0);
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
    ObjArray* array = AS_ARRAY(args[0]);
    if (array->count < 2) return args[0];

    int left = 0;
    int right = array->count - 1;
    
    while (left < right) {
        Value temp = array->values[left];
        array->values[left] = array->values[right];
        array->values[right] = temp;
        left++;
        right--;
    }
    return args[0];
}

static Value arrayFlattenNative(int argCount, Value* args) {
    ObjArray* source = AS_ARRAY(args[0]);
    ObjArray* result = newArray(0);
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
    ObjMap* map = AS_MAP(args[0]);
    ObjArray* valuesArray = newArray(0);
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
    ObjMap* map = AS_MAP(args[0]);
    ObjArray* valuesArray = newArray(0);
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
    if (argCount != 1 || !IS_STRING(args[1])) {
        return BOOL_VAL(false);
    }

    ObjMap* map = AS_MAP(args[0]);
    Value dummy;
    return BOOL_VAL(tableGet(&map->items, AS_STRING(args[1]), &dummy));
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

    ObjMap* map = AS_MAP(args[0]);
    ObjString* key = AS_STRING(args[1]);
    Value removedValue;

    if (tableGet(&map->items, key, &removedValue)) {
        tableDelete(&map->items, key);
        return removedValue;
    }

    return NIL_VAL;
}

static Value mapLenNative(int argCount, Value* args) {
    return NUMBER_VAL(AS_MAP(args[0])->items.count);
}

static Value arrayLenNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[0]);
    return NUMBER_VAL(array->count);
}

static Value stringSplitNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[1])) {
        runtimeError("split() expects 1 string argument (separator).");
        return NIL_VAL;
    }

    ObjString* receiver = AS_STRING(args[0]);
    ObjString* sep = AS_STRING(args[1]);

    ObjArray* result = newArray(0);
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
    ObjString* str = AS_STRING(args[0]);
    char* start = str->chars;
    char* end = str->chars + str->length - 1;

    while (isspace(*start)) start++;

    while (end > start && isspace(*end)) end--;

    int newLength = (int)(end - start + 1);
    if (newLength <= 0) return OBJ_VAL(copyString("", 0));

    return OBJ_VAL(copyString(start, newLength));
}

static Value stringContainsNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[1])) {
        return BOOL_VAL(false);
    }

    ObjString* haystack = AS_STRING(args[0]);
    ObjString* needle = AS_STRING(args[1]);

    return BOOL_VAL(strstr(haystack->chars, needle->chars) != NULL);
}

static Value stringToUpperNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[0]);

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
    ObjString* str = AS_STRING(args[0]);

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
    ObjString* str = AS_STRING(args[0]);
    return NUMBER_VAL((double)str->length);
}

static Value arrayJoinNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[1])) {
        runtimeError("join() expects 1 string argument (separator).");
        return NIL_VAL;
    }

    ObjArray* array = AS_ARRAY(args[0]);
    ObjString* sep = AS_STRING(args[1]);

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
    if (IS_INSTANCE(value)) return AS_INSTANCE(value)->klass;
    if (IS_MAP(value)) return vm.mapClass;
    if (IS_ARRAY(value)) return vm.arrayClass;
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
    return args[0]; // return self for chaining
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
    if (argCount <= 1 || !IS_STRING(args[1])) {
        runtimeError("File.read() expects 1 string argument (path).");
        return NIL_VAL;
    }

    const char* path = AS_STRING(args[1])->chars;
    FILE* file = fopen(path, "rb");
    if (file == NULL) return NIL_VAL;

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

    ObjArray* fileList = newArray(0);
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

    ObjRegex* re = AS_REGEX(args[0]);
    ObjString* subject = AS_STRING(args[1]);

    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re->code, NULL);
    int rc = pcre2_match(re->code, (unsigned char*)subject->chars, subject->length,
                0, 0, match_data, NULL);

    pcre2_match_data_free(match_data);
    return BOOL_VAL(rc >= 0);

}

static Value regexExecNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[1])) {
        runtimeError("test() expects 1 string argument.");
        return NIL_VAL;
    }

    ObjRegex* re = AS_REGEX(args[0]);
    ObjString* subject = AS_STRING(args[1]);

    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re->code, NULL);
    int rc = pcre2_match(re->code, (unsigned char*)subject->chars, subject->length,
                0, 0, match_data, NULL);

    if (rc < 0) {
        pcre2_match_data_free(match_data);
        return NIL_VAL;
    }

    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);

    ObjArray* results = newArray(0);
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

    //ObjString* result = copyString(subject->chars + start, length);

    pcre2_match_data_free(match_data);
    return pop();
}

static Value regexInitNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[1])) {
        runtimeError("Regex constructor expects a pattern string.");
        return NIL_VAL;
    }

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

    return OBJ_VAL(newRegex(code, pattern));
}

static Value fromHexNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[1])) return NIL_VAL;

    const char* str = AS_CSTRING(args[1]);
    char* endptr;

    unsigned long long result = strtoull(str, &endptr, 16);

    if (str == endptr) return NIL_VAL;
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

static Value mathParseNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[1])) return NIL_VAL;

    const char* str = AS_CSTRING(args[1]);
    char* endptr;

    unsigned long long result = strtoull(str, &endptr, 0);

    if (str == endptr) return NIL_VAL;

    return NUMBER_VAL((double)result);
}

static Value bitTestNative(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) return NIL_VAL;

    uint64_t num = (uint64_t)AS_NUMBER(args[1]);
    int bit = (int)AS_NUMBER(args[2]);

    if (bit < 0 || bit > 63) return BOOL_VAL(false);

    return BOOL_VAL((num >> bit) & 1);
}

static Value hexNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[1])) return NIL_VAL;

    uint64_t num = (uint64_t)AS_NUMBER(args[1]);
    int precision = (argCount == 3 && IS_NUMBER(args[2])) ? (int)AS_NUMBER(args[2]) : 1;

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "0x%.*llx", precision, (unsigned long long)num);

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

    if (IS_NUMBER(value)) return OBJ_VAL(copyString("NUMBER", 6));
    if (IS_BOOL(value)) return OBJ_VAL(copyString("BOOL", 4));
    if (IS_NIL(value)) return OBJ_VAL(copyString("NIL", 3));

    if (IS_OBJ(value)) {
        switch (OBJ_TYPE(value)) {
            case OBJ_STRING:
                return OBJ_VAL(copyString("STRING", 6));
            case OBJ_NATIVE:
                return OBJ_VAL(copyString("NATIVE", 6));
            case OBJ_CLOSURE:
                return OBJ_VAL(copyString("FUNCTION", 8));
            case OBJ_CLASS:
                return OBJ_VAL(copyString("CLASS", 5));
            case OBJ_INSTANCE:
                return OBJ_VAL(AS_INSTANCE(value)->klass->name);
            default:
                return OBJ_VAL(copyString("OBJECT", 6));
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
    defineNativeMethod(mathClass, "parse", mathParseNative);
    defineNativeMethod(mathClass, "from_hex", fromHexNative);
    defineNativeMethod(mathClass, "from_bin", fromBinNative);
    //defineNativeMethod(mathClass, "ceil", mathCeilNative);
    defineNativeMethod(mathClass, "round", mathRoundNative);
    defineNativeMethod(mathClass, "to_number", toNumberNative);


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

    tableSet(&vm.globals, systemName, OBJ_VAL(systemClass));

    ObjInstance* systemInstance = newInstance(systemClass);
    push(OBJ_VAL(systemInstance));

    tableSet(&systemInstance->fields, copyString("EXE", 3),
            OBJ_VAL(copyString(argv[0], strlen(argv[0]))));

    ObjArray* argsArray = newArray(0);
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

void initRegexLibrary() {
    push(OBJ_VAL(vm.regexClass));

    defineNativeMethod(vm.regexClass, "test", regexTestNative);
    defineNativeMethod(vm.regexClass, "exec", regexExecNative);

    defineNative("Regex", regexInitNative);

    pop();
}

void initVM(int argc, const char* argv[], const char* env[]) {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;
    vm.isGC = false;

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
    vm.displayString = NULL;
    vm.displayString = copyString("display", 7);
    vm.str_add = NULL;
    vm.str_add = copyString("__add__", 7);
    vm.str_sub = NULL;
    vm.str_sub = copyString("__sub__", 7);
    vm.str_mul = NULL;
    vm.str_mul = copyString("__mul__", 7);
    vm.str_div = NULL;
    vm.str_div = copyString("__div__", 7);

    defineNative("clock", clockNative);
    defineNative("str", strNative);
    defineNative("typeof", typeofNative);
    defineNative("isnumber", isNumberNative);
    defineNative("isstring", isStringNative);
    defineNative("isbool", isBoolNative);
    defineNative("isnil", isNilNative);
    defineNative("isclass", isClassNative);
    defineNative("isinstance", isInstanceNative);

    vm.arrayClass = newClass(copyString("Array", 5));
    vm.mapClass = newClass(copyString("Map", 3));
    vm.stringClass = newClass(copyString("String", 6));
    vm.regexClass = newClass(copyString("Regex", 5));
    vm.moduleClass = newClass(copyString("Module", 6));

    defineNativeMethod(vm.arrayClass, "push", arrayPushNative);
    defineNativeMethod(vm.arrayClass, "pop", arrayPopNative);
    defineNativeMethod(vm.arrayClass, "len", arrayLenNative);
    defineNativeMethod(vm.arrayClass, "map", arrayMapNative);
    defineNativeMethod(vm.arrayClass, "dup", arrayDupNative);
    defineNativeMethod(vm.arrayClass, "isEmpty", arrayIsEmptyNative);
    defineNativeMethod(vm.arrayClass, "select", arraySelectNative);
    defineNativeMethod(vm.arrayClass, "reduce", arrayReduceNative);
    defineNativeMethod(vm.arrayClass, "join", arrayJoinNative);
    defineNativeMethod(vm.arrayClass, "each", arrayEachNative);
    defineNativeMethod(vm.arrayClass, "find", arrayFindNative);
    defineNativeMethod(vm.arrayClass, "slice", arraySliceNative);
    defineNativeMethod(vm.arrayClass, "sort", arraySortNative);
    defineNativeMethod(vm.arrayClass, "reverse", arrayReverseNative);
    defineNativeMethod(vm.arrayClass, "flatten", arrayFlattenNative);
    defineNativeMethod(vm.mapClass, "keys", mapKeysNative);
    defineNativeMethod(vm.mapClass, "values", mapValuesNative);
    defineNativeMethod(vm.mapClass, "has", mapHasNative);
    defineNativeMethod(vm.mapClass, "remove", mapRemoveNative);
    defineNativeMethod(vm.mapClass, "len", mapLenNative);
    defineNativeMethod(vm.stringClass, "trim", stringTrimNative);
    defineNativeMethod(vm.stringClass, "contains", stringContainsNative);
    defineNativeMethod(vm.stringClass, "toUpper", stringToUpperNative);
    defineNativeMethod(vm.stringClass, "toLower", stringToLowerNative);
    defineNativeMethod(vm.stringClass, "len", stringLenNative);
    defineNativeMethod(vm.stringClass, "split", stringSplitNative);

    initMathLibrary();
    initSystemLibrary(argc, argv, env);
    initFileLibrary();
    initRegexLibrary();

    defineNative("getMembers", getMembersNative);
    //initArrayMethods();
}

void freeVM() {
    freeObjects();

    freeTable(&vm.globals);
    freeTable(&vm.strings);

    vm.initString = NULL;
    vm.displayString = NULL;
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
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.",
                closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD:
                {
                    ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                    vm.stackTop[-argCount - 1] = bound->receiver;
                    return vmCall(bound->method, argCount);
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
                    NativeFn native = AS_NATIVE(callee);
                    Value result = native(argCount, vm.stackTop - argCount);
                    //if (vm.frameCount == 0) return false;

                    vm.stackTop -= argCount + 1;
                    push(result);

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

static bool invokeFromClass(ObjClass* klass, ObjString* name,
        int argCount) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
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

    return vmCall(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);

    if (IS_INSTANCE(receiver)) {
        ObjInstance* instance = AS_INSTANCE(receiver);

        Value value;
        if (tableGet(&instance->fields, name, &value)) {
            vm.stackTop[-argCount - 1] = value;
            return callValue(value, argCount);
        }

        return invokeFromClass(instance->klass, name, argCount);
    }

    ObjClass* klass = NULL;
    if (IS_ARRAY(receiver)) klass = vm.arrayClass;
    else if (IS_MAP(receiver)) klass = vm.mapClass;
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
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError("Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod* bound = newBoundMethod(peek(0),
            AS_CLOSURE(method));
    pop();
    push(OBJ_VAL(bound));
    return true;
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

InterpretResult run() {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, \
     (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_24BIT() \
    (frame->ip +=3, (uint32_t)((frame->ip[-3] <<16) | (frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG(index) \
    (frame->closure->function->chunk.constants.values[index])
#define READ_STRING() AS_STRING(READ_CONSTANT())
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
                    uint8_t b1 = READ_BYTE();
                    uint8_t b2 = READ_BYTE();
                    uint8_t b3 = READ_BYTE();
                    int index = b1 | (b2 << 8) | (b3 << 16);

                    //Value constant = frame->closure->function->chunk.constants.values[index];
                    Value constant = READ_CONSTANT_LONG(index);
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
            case OP_SET_LOCAL:
                {
                    uint8_t slot = READ_BYTE();
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
            case OP_DEFINE_GLOBAL:
                {
                    ObjString* name = READ_STRING();
                    tableSet(&vm.globals, name, peek(0));
                    pop();
                }
                break;
            case OP_DEFINE_GLOBAL_LONG:
                {
                    uint32_t index = READ_24BIT();
                    ObjString* name = AS_STRING(frame->closure->function->chunk.constants.values[index]);
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
            case OP_GET_PROPERTY:
                {
                    Value receiver = peek(0);
                    ObjString* name = READ_STRING();

                    if (IS_INSTANCE(receiver)) {
                        ObjInstance* instance = AS_INSTANCE(receiver);
                        //printf("DEBUG: Instance %p has Klass %p\n", (void*)instance, (void*)instance->klass);

                        if (instance->klass == NULL) {
                            runtimeError("Instance has no class.");
                            return INTERPRET_RUNTIME_ERROR;
                        }
                    }

                    //ObjClass* klass = NULL;
                    if (IS_INSTANCE(receiver)) {
                        ObjInstance* instance = AS_INSTANCE(receiver);
                        //ObjString* name = READ_STRING();

                        Value value;
                        if (tableGet(&instance->fields, name, &value)) {
                            pop();
                            push(value);
                            break;
                        }

                        if (instance->klass->getter != NULL) {
                            value = instance->klass->getter(instance, name);
                            if (!IS_NIL(value)) {
                                pop();
                                push(value);
                                break;
                            }
                        }

                        if (!bindMethod(instance->klass, name)) {
                            return INTERPRET_RUNTIME_ERROR;
                        }
                        break;
                    } 

                    ObjClass* klass = getClassForValue(receiver);
                    if (klass != NULL) {
                        Value method;
                        if (tableGet(&klass->methods, name, &method)) {
                            if (IS_NATIVE(method)) {
                                pop();
                                push(method);
                            } else {
                                ObjBoundMethod* bound = newBoundMethod(receiver, AS_CLOSURE(method));
                                pop();
                                push(OBJ_VAL(bound));
                            }
                            break;
                        }
                    }

                    runtimeError("Property '%s' not found.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            case OP_SET_PROPERTY:
                {
                    if (!IS_INSTANCE(peek(1))) {
                        runtimeError("Only instances have fields.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjInstance* instance = AS_INSTANCE(peek(1));
                    ObjString* name = READ_STRING();
                    Value value = peek(0);

                    if (instance->klass->setter != NULL) {
                        if (instance->klass->setter(instance, name, value)) {
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
                    uint8_t slot = READ_BYTE();
                    push(*frame->closure->upvalues[slot]->location);
                }
                break;
            case OP_SET_UPVALUE:
                {
                    uint8_t slot = READ_BYTE();
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
                    //Value b = pop();
                    //Value a = pop();
                    if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                        double b = AS_NUMBER(pop());
                        double a = AS_NUMBER(pop());
                        push(NUMBER_VAL(a + b));
                    } else if (IS_INSTANCE(peek(1))) {
                        ObjInstance* instance = AS_INSTANCE(peek(1));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->klass->methods, vm.str_add, &method)) {
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
                        popn(2);
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
                    } else if (IS_INSTANCE(peek(1))) {
                        ObjInstance* instance = AS_INSTANCE(peek(1));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->klass->methods, vm.str_sub, &method)) {
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
                    } else if (IS_INSTANCE(peek(1))) {
                        ObjInstance* instance = AS_INSTANCE(peek(1));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->klass->methods, vm.str_mul, &method)) {
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
                    } else if (IS_INSTANCE(peek(1))) {
                        ObjInstance* instance = AS_INSTANCE(peek(1));
                        Value method;
                        Value result;

                        Value* stackStart = vm.stackTop;
                        if (tableGet(&instance->klass->methods, vm.str_div, &method)) {
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
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            case OP_BITWISE_AND:
                {
                    Value b = pop();
                    Value a = pop();

                    int result = (int)AS_NUMBER(a) & (int)AS_NUMBER(b);
                    push(NUMBER_VAL((double)result));
                }
                break;
            case OP_BITWISE_OR:
                {
                    Value b = pop();
                    Value a = pop();
                    int result = (int)AS_NUMBER(a) | (int)AS_NUMBER(b);
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
                            if (tableGet(&instance->klass->methods, vm.displayString, &method)) {
                                push(value);
                                if (callValue(method, 0)) {
                                    vm.nativeExitDepth = vm.frameCount - 1;
                                    run();
                                    Value result = pop();

                                    if (!IS_NIL(result)) {
                                        printValue(result);
                                    }
                                }
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
                {
                    ObjString* method = READ_STRING();
                    int argCount = READ_BYTE();
                    Value receiver = peek(argCount);

                    if (!IS_OBJ(receiver)) {
                        printf("CRASH PREVENTED: Receiver is not an object! Type: %d\n", receiver.type);
                    }

                    if (!invoke(method, argCount) || vm.frameCount == 0) {
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
                {
                    ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                    ObjClosure* closure = newClosure(function);
                    push(OBJ_VAL(closure));
                    for (int i = 0; i < closure->upvalueCount; i++) {
                        uint8_t isLocal = READ_BYTE();
                        uint8_t index = READ_BYTE();
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
                {
                    ObjString* moduleName = AS_STRING(READ_CONSTANT());
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
            case OP_INHERIT:
                {
                    Value superclass = peek(1);
                    if (!IS_CLASS(superclass)) {
                        runtimeError("Superclass must be a class.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjClass* subclass = AS_CLASS(peek(0));
                    tableAddAll(&AS_CLASS(superclass)->methods,
                            &subclass->methods);
                    pop();
                }
                break;
            case OP_METHOD:
                defineMethod(READ_STRING());
                break;
            case OP_MAP:
                {
                    uint8_t itemCount = READ_BYTE();
                    ObjMap* map = newMap();
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
                    
                    ObjArray* array = newArray(count);
                    printValue(OBJ_VAL(array));

                    for (int i = count - 1; i >= 0; i--) {
                        array->values[i] = pop();
                    }

                    push(OBJ_VAL(array));
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
                    ObjArray* array = newArray(count);
                    for (int i = 0; i < count; i++) {
                        array->values[i] = element;
                    }
                    popn(2);
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
                    } else if (!IS_ARRAY(targetValue)) {
                        runtimeError("Only maps and arrays support subscripting.");
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

                    push(array->values[index]);

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

