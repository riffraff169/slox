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
static InterpretResult run();
//void initArrayMethods();

static bool callValue(Value callee, int argCount);
static Value peek(int distance);
Value popn(int n);
static bool isFalsey(Value value);

static Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
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
    if (argCount != 1 || !IS_NUMBER(args[1])) {
        runtimeError("sqrt() expects 1 number argument.");
        return NIL_VAL;
    }
    return NUMBER_VAL(sqrt(AS_NUMBER(args[1])));
}

static Value mathAbsNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[1])) {
        runtimeError("sqrt() expects 1 number argument.");
        return NIL_VAL;
    }
    return NUMBER_VAL(fabs(AS_NUMBER(args[1])));
}

static Value mathFloorNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[1])) return NIL_VAL;
    return NUMBER_VAL(floor(AS_NUMBER(args[1])));
}

static Value mathCeilNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_NUMBER(args[1])) return NIL_VAL;
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

static Value arrayPushNative(int argCount, Value* args) {
    if (argCount < 1) return NIL_VAL;

    ObjArray* array = AS_ARRAY(args[0]);
    for (int i = 1; i <= argCount; i++) {
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
    if (argCount != 1 || !IS_STRING(args[1])) {
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
        runtimeError("join() expects 1 string argument (separator).");
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
        runtimeError("join() expects 1 string argument (separator).");
        return NIL_VAL;
    }

    for (int i = 0; i < str->length; i++) {
        buffer[i] = tolower((unsigned char)str->chars[i]);
    }
    buffer[str->length] = '\0';

    return OBJ_VAL(takeString(buffer, str->length));
}

static Value arrayJoinNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[1])) {
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

static Value fileReadNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[1])) {
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

static Value fileWriteNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
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
    if (argCount != 1 || !IS_STRING(args[1])) return BOOL_VAL(false);
    FILE* file = fopen(AS_STRING(args[1])->chars, "r");
    if (file) {
        fclose(file);
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value fileListNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[1])) {
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

    tableSet(&vm.globals, mathName, OBJ_VAL(mathClass));

    popn(2);

    srand((unsigned int)time(NULL));
}

void initSystemLibrary() {
    ObjString* systemName = copyString("System", 6);
    push(OBJ_VAL(systemName));
    ObjClass* systemClass = newClass(systemName);
    push(OBJ_VAL(systemClass));

    defineNativeMethod(systemClass, "time", systemTimeNative);
    defineNativeMethod(systemClass, "exit", systemExitNative);
    defineNativeMethod(systemClass, "gc", systemGCNative);

    tableSet(&vm.globals, systemName, OBJ_VAL(systemClass));

    popn(2);
}

void initFileLibrary() {
    ObjString* fileName = copyString("File", 4);
    push(OBJ_VAL(fileName));
    ObjClass* fileClass = newClass(fileName);
    push(OBJ_VAL(fileClass));

    defineNativeMethod(fileClass, "read", fileReadNative);
    defineNativeMethod(fileClass, "write", fileWriteNative);
    defineNativeMethod(fileClass, "exists", fileExistsNative);
    defineNativeMethod(fileClass, "list", fileListNative);

    tableSet(&vm.globals, fileName, OBJ_VAL(fileClass));

    popn(2);
}

void initRegexLibrary() {
    //ObjString* regexName = copyString("Regex", 5);
    //push(OBJ_VAL(regexName));
    //vm.regexClass = newClass(copyString("Regex", 5));
    push(OBJ_VAL(vm.regexClass));

    //defineNativeMethod(vm.regexClass, "init", regexInitNative);
    defineNativeMethod(vm.regexClass, "test", regexTestNative);
    defineNativeMethod(vm.regexClass, "exec", regexExecNative);

    defineNative("Regex", regexInitNative);

    pop();
    //tableSet(&vm.globals, regexName, OBJ_VAL(regexClass));

    //popn(2);
}

void initVM() {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    vm.moduleCount = 0;
    vm.moduleCapacity = 0;
    vm.moduleHandles = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings);
    initTable(&vm.giTypes);

    vm.initString = NULL;
    vm.initString = copyString("init", 4);

    defineNative("clock", clockNative);

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
    defineNativeMethod(vm.mapClass, "keys", mapKeysNative);
    defineNativeMethod(vm.mapClass, "values", mapValuesNative);
    defineNativeMethod(vm.mapClass, "has", mapHasNative);
    defineNativeMethod(vm.mapClass, "remove", mapRemoveNative);
    defineNativeMethod(vm.mapClass, "len", mapLenNative);
    defineNativeMethod(vm.stringClass, "split", stringSplitNative);
    defineNativeMethod(vm.stringClass, "trim", stringTrimNative);
    defineNativeMethod(vm.stringClass, "contains", stringContainsNative);
    defineNativeMethod(vm.stringClass, "toUpper", stringToUpperNative);
    defineNativeMethod(vm.stringClass, "toLower", stringToLowerNative);

    initMathLibrary();
    initSystemLibrary();
    initFileLibrary();
    initRegexLibrary();

    defineNative("getMembers", getMembersNative);
    //initArrayMethods();
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeTable(&vm.giTypes);

    vm.initString = NULL;

    for (int i = 0; i < vm.moduleCount; i++) {
        if (vm.moduleHandles[i] != NULL) {
            dlclose(vm.moduleHandles[i]);
        }
    }
    FREE_ARRAY(void*, vm.moduleHandles, vm.moduleCapacity);

    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount) {
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
                    return call(bound->method, argCount);
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
                            return call(AS_CLOSURE(initializer), argCount);
                        }
                    } else if (argCount != 0) {
                        runtimeError("Expect 0 arguments but got %d.", argCount);
                        return false;
                    }
                    return true;
                }
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
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

    return call(AS_CLOSURE(method), argCount);
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

static InterpretResult run() {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)

#define READ_SHORT() \
    (frame->ip += 2, \
     (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])

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
                    push(frame->slots[slot]);
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

                    ObjInstance* instance = AS_INSTANCE(receiver);
                    printf("DEBUG: Instance %p has Klass %p\n", (void*)instance, (void*)instance->klass);

                    if (instance->klass == NULL) {
                        runtimeError("Instance has no class.");
                        return INTERPRET_RUNTIME_ERROR;
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

                    /*
                    if (IS_ARRAY(receiver)) {
                        klass = vm.arrayClass;
                        break;
                    }

                    if (IS_MAP(receiver)) {
                        klass = vm.mapClass;
                        break;
                    }

                    if (IS_STRING(receiver)) {
                        klass = vm.stringClass;
                        break;
                    }
                    */

                    /*
                    if (IS_ARRAY(receiver)) {
                        ObjString* name = READ_STRING();
                        Value method;
                        if (tableGet(&vm.arrayMethods, name, &method)) {
                            ObjBoundMethod* bound = newBoundMethod(receiver, AS_CLOSURE(method));
                            pop();
                            push(OBJ_VAL(bound));
                            break;
                        }
                        runtimeError("Array has no method '%s'.", name->chars);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    */

                    /*
                    if (!IS_INSTANCE(peek(0))) {
                        runtimeError("Only instances have properties.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjInstance* instance = AS_INSTANCE(peek(0));
                    ObjString* name = READ_STRING();

                    Value value;
                    if (tableGet(&instance->fields, name, &value)) {
                        pop();
                        push(value);
                        break;
                    }

                    if (!bindMethod(instance->klass, name)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    */
                    runtimeError("Property '%s' nothave found.", name->chars);
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
                    tableSet(&instance->fields, READ_STRING(), peek(0));
                    Value value = pop();
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
                    } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                        double b = AS_NUMBER(pop());
                        double a = AS_NUMBER(pop());
                        push(NUMBER_VAL(a + b));
                    } else {
                        runtimeError(
                                "Operands must be two numbers or two strings.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            case OP_SUBTRACT:
                BINARY_OP(NUMBER_VAL, -);
                break;
            case OP_MULTIPLY:
                BINARY_OP(NUMBER_VAL, *);
                break;
            case OP_DIVIDE:
                BINARY_OP(NUMBER_VAL, /);
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
                    Value b = pop();
                    Value a = pop();

                    double ad = AS_NUMBER(a);
                    double bd = AS_NUMBER(b);

                    if (bd == 0) {
                        runtimeError("Division by zero.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    double res = fmod(ad, bd);
                    push(NUMBER_VAL(res == 0.0 ? 0.0 : res));
                }
                ;
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            case OP_PRINT:
                {
                    int argCount = READ_BYTE();

                    for (int i = argCount - 1; i >= 0; i--) {
                        printValue(peek(i));
                        if (i > 0) printf(" ");
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
    call(closure, 0);

    return run();
}

