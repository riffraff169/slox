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
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"
#include "native.h"

#define MAX32 4294967296.0


// 1. Setup phase: Capture the original depth and current stack pointer
#define VM_CALLBACK_INIT() \
    int _oldExitDepth = vm.nativeExitDepth; \
    Value* _callbackStackStart = vm.stackTop

// 2. Execute phase: Tell the vm where to return when the callback yields
#define VM_CALLBACK_ENTER() \
    vm.nativeExitDepth = vm.frameCount

// 3. Error Guard: Abort immediately if the loop encountered a runtime panic
#define VM_CALLBACK_CHECK_ERROR(resultState) \
    if ((resultState) == INTERPRET_RUNTIME_ERROR) { \
        vm.stackTop = _callbackStackStart; \
        vm.nativeExitDepth = _oldExitDepth; \
        return NIL_VAL; \
    }

// 4. Iteration reset: Clear the stack back to the stable start point for the next loop
#define VM_CALLBACK_RESET_STACK() \
    vm.stackTop = _callbackStackStart

// 5. Final Teardown: Restore the exit depth before returning a final value
#define VM_CALLBACK_EXIT() \
    vm.nativeExitDepth = _oldExitDepth


#define RUNTIME_ERROR(...) \
    do { \
        if (runtimeError(__VA_ARGS__)) { \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        frame = &vm.frames[vm.frameCount - 1]; \
    } while (false)

VM vm;
InterpretResult run();
//void initArrayMethods();

bool callValue(Value callee, int argCount);
bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount);
Value peek(int distance);
bool isFalsey(Value value);
bool isTruthy(Value value);

typedef enum {
    PROP_FOUND,
    PROP_ASYNC,
    PROP_NOT_FOUND,
    PROP_IMMUTABLE,
    PROP_FROZEN,
    PROP_GETTER,
    PROP_SETTER
} PropertyResult;

void setLastError(int errorNum, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len < 0) len = 0;
    if (len >= (int)sizeof(buffer)) len = sizeof(buffer) - 1;

    tableSet(&vm.globals, vm.errnoString, NUMBER_VAL((double)errorNum));

    ObjString* errstrVal = copyString(buffer, len);
    push(OBJ_VAL(errstrVal));

    tableSet(&vm.globals, vm.errstrString, OBJ_VAL(errstrVal));
    pop();
}

void clearLastError() {
    setLastError(0, "%s", "Success");
}

/*
static Value resultInitNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);

    tableSet(&instance->fields, vm.okString, args[0]);
    tableSet(&instance->fields, vm.valString, args[1]);
    tableSet(&instance->fields, vm.errString, args[2]);

    return args[-1];
}
*/

static Value createResult(Value value, Value errval, bool isok) {
    push(value);
    push(errval);

    Value resultValue;

    ObjInstance* result = newInstance(vm.resultClass);
    push(OBJ_VAL(result));

    tableSet(&result->fields, vm.okString, BOOL_VAL(isok));
    tableSet(&result->fields, vm.valString, value);
    tableSet(&result->fields, vm.errString, errval);

    pop();
    popn(2);
    return OBJ_VAL(result);
}

Value errorResult(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ObjString* errstr = copyString(buffer, (int)strlen(buffer));
    Value res = createResult(NIL_VAL, OBJ_VAL(errstr), false);
    return res;
}

Value okResult(Value value) {
    return createResult(value, NIL_VAL, true);
}

bool isResultOk(Value value) {
    if (!IS_INSTANCE(value)) return false;

    ObjInstance* instance = AS_INSTANCE(value);
    Value okVal;

    if (tableGet(&instance->fields, vm.okString, &okVal)) {
        return isTruthy(okVal);
    }

    return false;
}

void raiseException(Value exceptionValue) {
    vm.exceptionThrown = true;
    if (vm.tryCount == 0) {
        fprintf(stderr, "Unhandled Exception: ");
        printValue(exceptionValue);
        fprintf(stderr, "\n");
        exit(70);
    }

    //vm.tryCount--;
    TryBlock* target = &vm.tryStack[vm.tryCount - 1];

    vm.stackTop = target->stackTop;
    vm.frameCount = target->frameCount;
    CallFrame* currentFrame = &vm.frames[vm.frameCount - 1];

    if (target->catchIp != NULL) {
        uint8_t* catchTargetIp = target->catchIp;

        target->catchIp = NULL;

        if (target->finallyIp == NULL) {
            vm.tryCount--;
        }
        push(exceptionValue);
        currentFrame->ip = catchTargetIp;
    } else if (target->finallyIp != NULL) {
        target->hasUncaughtException = true;
        target->uncaughtException = exceptionValue;

        uint8_t* finallyTargetIp = target->finallyIp;
        target->finallyIp = NULL;

        currentFrame->ip = finallyTargetIp;
    } else {
        vm.tryCount--;
        raiseException(exceptionValue);
        //push(exceptionValue);
        //currentFrame->ip = target->catchIp;
    }
}

void defineClassConstant(ObjClass* klass, const char* name, Value value) {
    push(value);
    ObjString* constantName = copyString(name, (int)strlen(name));
    push(OBJ_VAL(constantName));

    tableSet(&klass->constants, constantName, value);

    pop();
    pop();
}

/*
static Value optionInitNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);

    tableSet(&instance->fields, copyString("is_some", 7), args[0]);
    tableSet(&instance->fields, copyString("val", 3), args[1]);

    return args[-1];
}
*/

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

static Value objectEachNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) {
        runtimeError("Expected a closure callback.");
        return NIL_VAL;
    }

    Value receiver = args[-1];
    ObjClosure* callback = AS_CLOSURE(args[0]);

    Value iterMethod;
    if (tableGet(&vm.strings, copyString("iter", 4), &iterMethod)) {
        // invoke iter via vmcall and run() to get the iterator object
    }
    Value iteratorObj = pop();

    int oldExitDepth = vm.nativeExitDepth;

    while (true) {
        // call iteratorObj.done()
        // if it returns true, break;

        // call iteratorObj.next() to get the current item
        Value item = pop();

        push(OBJ_VAL(callback));
        push(item);
        vm.nativeExitDepth = vm.frameCount;

        if (vmCall(callback, 1)) {
            InterpretResult state = run();
            if (state == INTERPRET_RUNTIME_ERROR) {
                vm.nativeExitDepth = oldExitDepth;
                return NIL_VAL;
            }
            pop();
        }
    }

    vm.nativeExitDepth = oldExitDepth;
    return NIL_VAL;
}

ObjClass* compileToClass(char* source, char* filename) {
    return NULL;
}

static Value compileFileNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("compile_file() expects a string filename argument.");
        return NIL_VAL;
    }

    ObjString* filename = AS_STRING(args[0]);

    char* source = readFile(filename->chars);
    if (source == NULL) {
        runtimeError("Could not open or read file '%s'.", filename->chars);
        return NIL_VAL;
    }

    ObjClass* compiledClass = compileToClass(source, filename->chars);

    free(source);

    if (compiledClass == NULL) {
        return NIL_VAL;
    }

    return OBJ_VAL(compiledClass);
}

static bool callMethodMissing(ObjClass* klass, ObjString* originalName, int argCount) {
    Value method;
    ObjClass* currentClass = klass;
    bool found = false;

    while (currentClass != NULL) {
        if (tableGet(&currentClass->methods, vm.methodMissingString, &method)) {
            found = true;
            break;
        }
        currentClass = currentClass->superclass;
    }

    /*
    printf("DEBUG: Looking for method '%s' on Class '%s' (Type Enum: %d)\n",
            originalName->chars, klass->name->chars, 0);
            */
    if (!found) {
        runtimeError("Undefined method '%s'.", originalName->chars);
        return false;
    }

    //push(OBJ_VAL(originalName));
    Value* argsStart = vm.stackTop - argCount;
    ObjArray* argsArray = newArray();
    push(OBJ_VAL(argsArray));

    for (int i = 0; i < argCount; i++) {
        arrayAppend(argsArray, argsStart[i]);
    }

    vm.stackTop = argsStart;

    push(OBJ_VAL(originalName));
    push(OBJ_VAL(argsArray));

    if (IS_NATIVE(method)) {
        NativeFn native = AS_NATIVE(method);
        Value result = native(2, vm.stackTop - 2);
        vm.stackTop -= 3;
        push(result);
        return true;
    }

    bool res = invokeFromClass(currentClass, vm.methodMissingString, 2);
    if (vm.exceptionThrown) {
        vm.exceptionThrown = false;
        return false;
    }
    vm.stackTop -= 3;
    return res;
}

void* locateAndLoadModule(const char* name) {
    char filename[256];

    snprintf(filename, sizeof(filename), "liblox_%s.so", name);

    char pathBuffer[PATH_MAX];
    void* handle = NULL;

    snprintf(pathBuffer, sizeof(pathBuffer), "./modules/%s", filename);
    if (access(pathBuffer, F_OK) == 0) {
        handle = dlopen(pathBuffer, RTLD_NOW |  RTLD_GLOBAL);
        if (handle) return handle;
    }

    const char* home = getenv("HOME");
    if (home != NULL) {
        snprintf(pathBuffer, sizeof(pathBuffer), "%s/.local/share/slox/modules/%s", home, filename);
        if (access(pathBuffer, F_OK) == 0) {
            handle = dlopen(pathBuffer, RTLD_NOW | RTLD_GLOBAL);
            if (handle) return handle;
        }

        snprintf(pathBuffer, sizeof(pathBuffer), "%s/.local/slox/modules/%s", home, filename);
        if (access(pathBuffer, F_OK) == 0) {
            handle = dlopen(pathBuffer, RTLD_NOW | RTLD_GLOBAL);
            if (handle) return handle;
        }
    }

    snprintf(pathBuffer, sizeof(pathBuffer), "/usr/local/lib/slox/modules/%s", filename);
    if (access(pathBuffer, F_OK) == 0) {
        handle = dlopen(pathBuffer, RTLD_NOW | RTLD_GLOBAL);
        if (handle) return handle;
    }

    return NULL;
}

void* loadModule(const char* name) {
    // 1. construct the filename
    //char path[256];
    //snprintf(path, sizeof(path), "./liblox_%s.so", name);

    // 2. open the shared library
    //void* handle = dlopen(path, RTLD_NOW);
    void* handle = locateAndLoadModule(name);
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

static Value arrayIterNative(int argCount, Value* args) {
    Value arrayVal = args[-1];

    // 1. look up "ArrayIterator" from the global variable state
    Value iteratorClass;
    ObjString* className = copyString("ArrayIterator", 13);
    push(OBJ_VAL(className));

    if (!tableGet(&vm.globals, className, &iteratorClass)) {
        // fallback or runtime error if stdlib failed to load it
        pop(); // classname
        return NIL_VAL;
    }
    pop(); // classname

    // 2. instantiate the iterator class by calling it
    push(iteratorClass);
    push(arrayVal);

    // this runs callValue(), which sets up a new frome for ArrayIterator.init()
    VM_CALLBACK_INIT();
    VM_CALLBACK_ENTER();

    if (vmCall(AS_CLOSURE(iteratorClass), 1)) {
        InterpretResult state = run();

        VM_CALLBACK_CHECK_ERROR(state);
    }

    Value iterInstance = peek(0);

    VM_CALLBACK_EXIT();
    pop();

    return iterInstance;
}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

ObjClass* getClassForValue(Value value) {
    // 1. Handle primitive immediate values
    if (IS_NUMBER(value)) return vm.numberClass;
    if (IS_BOOL(value)) return vm.boolClass;
    if (IS_NIL(value)) return vm.nilClass;
    if (IS_STRING(value)) return vm.stringClass;
    if (IS_VEC3(value)) return vm.vec3Class;

    // 2. Handle heap-allocated objects
    if (IS_OBJ(value)) {
        switch (OBJ_TYPE(value)) {
            case OBJ_STRING: return vm.stringClass;
            case OBJ_ARRAY: return vm.arrayClass;
            case OBJ_MAP: return vm.mapClass;
            case OBJ_CLASS: return vm.classClass;
                //return (ObjClass*)AS_OBJ(value);
                //return AS_CLASS(value)->obj.klass;
            case OBJ_CLOSURE:
            case OBJ_NATIVE: return vm.functionClass;
            case OBJ_INSTANCE: return AS_INSTANCE(value)->obj.klass;
            default: return AS_OBJ(value)->klass;
        }
    }
    return NULL;
}

bool runtimeError(const char* format, ...) {
    //printf("[DEBUG] runtimeError triggered! tryCount: %d, format: %s\n", vm.tryCount, format);

    va_list args;
    va_start(args, format);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    //printf("runtimeError: buffer: %s\n", buffer);

    if (vm.tryCount > 0) {

        TryBlock block = vm.tryStack[--vm.tryCount];

        vm.frameCount = block.frameCount;
        vm.stackTop = block.stackTop;

        ObjString* errorMsg = copyString(buffer, (int)strlen(buffer));
        push(OBJ_VAL(errorMsg));

        Value errorClassVal;
        ObjString* errorClassName = copyString("Error", 5);
        push(OBJ_VAL(errorClassName));

        bool hasErrorClass = tableGet(&vm.globals, errorClassName, &errorClassVal);
        pop();

        if (hasErrorClass && IS_CLASS(errorClassVal)) {
            ObjClass* errorClass = AS_CLASS(errorClassVal);
            ObjInstance* errorInstance = newInstance(errorClass);
            pop(); // errorMsg
            push(OBJ_VAL(errorInstance));

            ObjString* messageKey = copyString("message", 7);
            push(OBJ_VAL(messageKey));
            push(OBJ_VAL(errorMsg));

            tableSet(&errorInstance->fields, messageKey, OBJ_VAL(errorMsg));

            pop();
            pop();
        } else {
            ObjString* errorMsg = copyString(buffer, (int)strlen(buffer));
            push(OBJ_VAL(errorMsg));
        }

        vm.frames[vm.frameCount -1].ip = block.catchIp;

        //printf("in try...\n");
        vm.exceptionThrown = true;
        return false;
    }

    //fputs("\n", stderr);
    fprintf(stderr, "%s\n", buffer);
    int line = 0;

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame= &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code - 1;
        line = getLine(&function->chunk, instruction);

        const char* file = function->filename ? function->filename->chars : "unknown";

        fprintf(stderr, "[%s:%d] in ", file, line);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    //fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
    return true;
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

void defineNativeMethod(ObjClass* klass, const char* name,
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
    //return NIL_VAL; // technically never reached
}

static Value systemStrictNative(int argCount, Value* args) {
    if (argCount == 1 && IS_BOOL(args[0])) {
        vm.strictMode = AS_BOOL(args[0]);
    }
    return BOOL_VAL(vm.strictMode);
}

static Value systemWarnNative(int argCount, Value* args) {
    if (argCount == 1 && IS_BOOL(args[0])) {
        vm.warnMode = AS_BOOL(args[0]);
    }
    return BOOL_VAL(vm.warnMode);
}

static Value systemSetPrecisionNative(int argCount, Value* args) {
    int precision = 6;

    if (argCount == 1 && IS_NUMBER(args[0])) {
        precision = (int)AS_NUMBER(args[0]);
        if (precision < 0) precision = 0;
    }

    vm.numPrecision = precision;
    return NUMBER_VAL(precision);
}

static Value systemSetNotationNative(int argCount, Value* args) {
    int style = 1;
    if (argCount == 1 && IS_NUMBER(args[0])) {
        style = (int)AS_NUMBER(args[0]);
    }
    vm.numNotation = style;
    return NUMBER_VAL(style);
}

static Value systemDebugPrintNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_BOOL(args[1])) {
        runtimeError("Expected a boolean argument (true/false).");
        return BOOL_VAL(false);
    }
    vm.debugPrintCode = AS_BOOL(args[1]);
    return BOOL_VAL(vm.debugPrintCode);
}

static Value systemTraceNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_BOOL(args[0])) {
        runtimeError("Expected a boolean argument (true/false).");
        return BOOL_VAL(false);
    }
    vm.debugTraceExecution = AS_BOOL(args[0]);
    return BOOL_VAL(vm.debugTraceExecution);
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
        setLastError(errsv, "%s", errmsg);
        runtimeError("Error reading statm: %s\n", errmsg);
        pop();
        return NIL_VAL;
    }

    if (7 != fscanf(f, "%lu %lu %lu %lu %lu %lu %lu",
                &res.size, &res.resident, &res.share, &res.text,
                &res.lib, &res.data, &res.dt)) {
        int errsv = errno;
        char *errmsg = strerror(errsv);
        setLastError(errsv, "%s", errmsg);
        runtimeError("Error parsing statm: %s\n", errmsg);
        fclose(f);
        pop();
        return NIL_VAL;
    }

    fclose(f);

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
    return pop();
}

bool isInstanceOf(Value value, ObjClass* targetClass) {
    if (!IS_INSTANCE(value)) return false;

    ObjClass* klass = AS_INSTANCE(value)->obj.klass;
    while (klass != NULL) {
        if (klass == targetClass) return true;
        klass = klass->superclass;
    }
    return false;
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
    if (argCount < 1) {
        runtimeError("dot() expects 1 argument.");
        return NIL_VAL;
    }

    if (!IS_VEC3(args[0])) {
        runtimeError("Argument must be a Vec3.");
        return NIL_VAL;
    }

    Vec3 a = AS_VEC3(args[-1]);
    Vec3 b = AS_VEC3(args[0]);

    return NUMBER_VAL((a.x * b.x) +
            (a.y * b.y) + (a.z * b.z));
}

static Value vec3UnitNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("unit() expects 0 arguments.");
        return NIL_VAL;
    }
    Vec3 a = AS_VEC3(args[-1]);

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
    if (argCount < 1) {
        runtimeError("cross() expects 1 Vec3 argument.");
        return NIL_VAL;
    }

    if (!IS_VEC3(args[0])) {
        runtimeError("Argument must be a Vec3.");
        return NIL_VAL;
    }

    Vec3 a = AS_VEC3(args[-1]);
    Vec3 b = AS_VEC3(args[0]);
    Vec3 c;
    c.x = a.y * b.z - a.z * b.y;
    c.y = a.z * b.x - a.x * b.z;
    c.z = a.x * b.y - a.y * b.x;
    return VEC3_VAL(c);
}

static Value vec3LengthNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("length() expects 0 arguments.");
        return NIL_VAL;
    }

    Vec3 vec = AS_VEC3(args[-1]);
    double len = sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
    return NUMBER_VAL(len);
}

static Value vec3LengthSquaredNative(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("length_squared() expects 0 arguments.");
        return NIL_VAL;
    }

    Vec3 vec = AS_VEC3(args[-1]);
    double len = vec.x * vec.x + vec.y * vec.y + vec.z * vec.z;
    return NUMBER_VAL(len);
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

static inline Value getCheckTarget(int argCount, Value* args) {
    /*
    if (IS_STRING(args[-1]) || IS_NUMBER(args[-1]) ||
            IS_BOOL(args[-1]) || IS_NIL(args[-1]) ||
            IS_INSTANCE(args[-1]) || IS_CLOSURE(args[-1]) ||
            IS_VEC3(args[-1])) {
        return args[-1];
    }
    return (argCount > 0) ? args[0] : NIL_VAL;
    */
    if (IS_NATIVE(args[-1])) {
        return (argCount > 0) ? args[0] : NIL_VAL;
    }
    return args[-1];
}

Value systemSleepNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("System.sleep() requires a numeric duration in seconds.");
        return NIL_VAL;
    }

    double totalSeconds = AS_NUMBER(args[0]);
    if (totalSeconds < 0) totalSeconds = 0;

    struct timespec ts;
    ts.tv_sec = (time_t)totalSeconds;
    ts.tv_nsec = (long)((totalSeconds - (double)ts.tv_sec) * 1e9);
    
    nanosleep(&ts, NULL);

    return NIL_VAL;
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
    defineNativeMethod(systemClass, "set_notation", systemSetNotationNative);
    defineNativeMethod(systemClass, "set_precision", systemSetPrecisionNative);
    defineNativeMethod(systemClass, "debug_print", systemDebugPrintNative);
    defineNativeMethod(systemClass, "trace", systemTraceNative);
    defineNativeMethod(systemClass, "strict", systemStrictNative);
    defineNativeMethod(systemClass, "warn", systemWarnNative);
    defineNativeMethod(systemClass, "sleep", systemSleepNative);

    tableSet(&vm.globals, systemName, OBJ_VAL(systemClass));

    ObjInstance* systemInstance = newInstance(systemClass);
    push(OBJ_VAL(systemInstance));

    vm.includePathCount = 0;
    vm.scriptName = NULL;

    ObjArray* argsArray = newArray();
    push(OBJ_VAL(argsArray));

    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-I") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -I option requires a directory path.\n");
                exit(64);
            }
            if (vm.includePathCount < 64) {
                vm.includePaths[vm.includePathCount++] = argv[i + 1];
            }
            i += 2;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            exit(64);
        }
    }

    if (i < argc) {
        vm.scriptName = argv[i];
        i++;
    }

    const char* exeTarget = (vm.scriptName != NULL) ? vm.scriptName : argv[0];

    ObjString* exeKey = copyString("EXE", 3);
    push(OBJ_VAL(exeKey));

    ObjString* exeVal = copyString(exeTarget, strlen(exeTarget));
    push(OBJ_VAL(exeVal));

    tableSet(&systemInstance->fields, exeKey, OBJ_VAL(exeVal));
    pop();
    pop();

    for (int j = i; j < argc; j++) {
        ObjString* argStr = copyString(argv[j], strlen(argv[j]));
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

Value vec3CallHandler(int argCount, Value* args) {
    if (argCount != 3) {
        runtimeError("Vec3 construct expects 3 arguments.");
        return NIL_VAL;
    }
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        runtimeError("Vec3 arguments must be numbers.");
        return NIL_VAL;
    }
    Vec3 v = { AS_NUMBER(args[0]), AS_NUMBER(args[1]), AS_NUMBER(args[2]) };
    return VEC3_VAL(v);
}

Value universalClassGetter(Value receiver, ObjString* name) {
    if (name == copyString("class", 5)) {
        ObjClass* klass = getClassForValue(receiver);
        return OBJ_VAL(klass);
    }
    return NIL_VAL;
}

Value vec3GetterNative(Value receiver, ObjString* name) {
    Vec3 vec = AS_VEC3(receiver);

    if (name == vm.xString) return NUMBER_VAL(vec.x);
    if (name == vm.yString) return NUMBER_VAL(vec.y);
    if (name == vm.zString) return NUMBER_VAL(vec.z);

    return NIL_VAL;
}

void initVec3Library() {
    ObjString* name = copyString("Vec3", 4);
    push(OBJ_VAL(name));

    vm.vec3Class = newClass(name);
    vm.vec3Class->superclass = vm.objectClass;
    vm.vec3Class->callHandler = vec3CallHandler;
    vm.vec3Class->getter = vec3GetterNative;
    tableSet(&vm.globals, name, OBJ_VAL(vm.vec3Class));
    
    //defineNativeMethod(vm.vec3Class, "init", vec3InitNative);
    defineNativeMethod(vm.vec3Class, "dot", vec3DotNative);
    defineNativeMethod(vm.vec3Class, "cross", vec3CrossNative);
    defineNativeMethod(vm.vec3Class, "unit", vec3UnitNative);
    defineNativeMethod(vm.vec3Class, "length", vec3LengthNative);
    defineNativeMethod(vm.vec3Class, "length_squared", vec3LengthSquaredNative);
    pop();
}

static Value arrayInitMethod(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("Array constructor expects a pattern string.");
        return NIL_VAL;
    }

    ObjArray* array = newArray();
    return OBJ_VAL(array);
}

static Value mapInitMethod(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("Map init constructor expects a string.");
        return NIL_VAL;
    }
}

static Value stringInitMethod(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("String init constructor expects a string.");
        return NIL_VAL;
    }
}

static Value classSuperclassMethod(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("Expected 0 arguments but get %d.", argCount);
        return NIL_VAL;
    }
    Value receiver = args[-1];

    ObjClass* klass = getClassForValue(receiver);

    if (klass->superclass == NULL) {
        return NIL_VAL;
    }

    return OBJ_VAL(klass->superclass);
}

static Value objectRootGetter(int argCount, Value* args) {
    ObjString* name = AS_STRING(args[0]);

    if (name == vm.classString) {
        Value receiver = args[-1];
        ObjClass* klass = getClassForValue(receiver);

        return okResult(OBJ_VAL(klass));
    }

    return errorResult("Value '%s' not found", name);
}

static Value arrayCallHandler(int argCount, Value* args) {
    return OBJ_VAL(newArray());
}

static Value mapCallHandler(int argCount, Value* args) {
    return OBJ_VAL(newMap());
}

static Value boolCallHandler(int argCount, Value* args) {
    if (argCount < 1) return BOOL_VAL(false);
    return BOOL_VAL(isTruthy(args[0]));
}

static bool findMethod(ObjClass* klass, ObjString* name, Value* method) {
    ObjClass* current = klass;

    while (current != NULL) {
        Table* methods = (current->mixinsource != NULL)
            ? &current->mixinsource->methods
            : &current->methods;

        if (tableGet(methods, name, method)) {
            return true;
        }
        current = current->superclass;
    }
    return false;
}

bool isResultInstance(Value value) {
    if (!IS_INSTANCE(value)) return false;
    ObjInstance* instance = AS_INSTANCE(value);
    return instance->obj.klass == vm.resultClass;
}

PropertyResult getProperty(Value receiver, ObjString* name, Value* result) {
    // 1. dynamic local fields (exclusive to heap instances; no class check needed)
    if (IS_INSTANCE(receiver)) {
        ObjInstance* instance = AS_INSTANCE(receiver);
        if (tableGet(&instance->fields, name, result)) {
            return PROP_FOUND;
        }
    }

    // 1b. class constants pipeline
    ObjClass* constClass = NULL;
    if (IS_INSTANCE(receiver)) {
        constClass = AS_INSTANCE(receiver)->obj.klass;
    } else if (IS_CLASS(receiver)) {
        constClass = AS_CLASS(receiver);
    } else {
        constClass = getClassForValue(receiver);
    }

    if (constClass != NULL) {
        ObjClass* currentClass = constClass;
        while (currentClass != NULL) {
            Table* constants = (currentClass->mixinsource != NULL)
                ? &currentClass->mixinsource->constants
                : &currentClass->constants;

            if (tableGet(constants, name, result)) {
                return PROP_FOUND;
            }
            currentClass = currentClass->superclass;
        }
    }

    // 2. unified metadata pipeline (primitives, vectors, classes)
    ObjClass* klass = getClassForValue(receiver);
    if (klass != NULL) {
        // step a: search up the inheritance chain for a polymorphic Value getter
        ObjClass* currentClass = klass;
        Value propertyGetter = NIL_VAL;
        while (currentClass != NULL) {
            Table* getters = (currentClass->mixinsource != NULL)
                ? &currentClass->mixinsource->getters
                : &currentClass->getters;

            if (tableGet(getters, name, &propertyGetter)) {
                break;
            }
            currentClass = currentClass->superclass;
        }

        if (!IS_NIL(propertyGetter)) {
            push(receiver);

            if (callValue(propertyGetter, 0)) {
                vm.frames[vm.frameCount - 1].isGetter = true;
                return PROP_ASYNC;
            }

            *result = peek(0);
            pop();
            return PROP_FOUND;
        }

        currentClass = klass;
        Value getterValue = NIL_VAL;
        while (currentClass != NULL) {
            Value currentVGetter = (currentClass->mixinsource != NULL)
                ? currentClass->mixinsource->vGetter
                : currentClass->vGetter;

            if (!IS_NIL(currentVGetter)) {
                getterValue = currentVGetter;
                break;
            }
            currentClass = currentClass->superclass;
        }

        if (!IS_NIL(getterValue)) {
            // Set up stack frames for callValue: [getterValue, receiver, name]
            // note: receiver is currently sitting at peek(0) on the vm eval stack
            push(getterValue);
            push(receiver);
            push(OBJ_VAL(name));

            if (callValue(getterValue, 2)) {
                // its a lox script closure, a new callframe is now active
                // control flow inverted, return immediately so the main loop can run it
                vm.frames[vm.frameCount - 1].isGetter = true;
                return PROP_ASYNC;
            }

            // it was a native function, executed synchronously, result is at peek(0)
            Value getterReturnVal = peek(0);
            if (isResultInstance(getterReturnVal)) {
                // returned result instance
                if (isResultOk(getterReturnVal)) {
                    Value fakeStack[2] = { getterReturnVal, NIL_VAL };
                    *result = resultUnwrapOrNative(1, &fakeStack[1]);
                    pop();
                    return PROP_FOUND;
                } else {
                    pop();
                    push(receiver);
                }
            } else {
                // returned direct value
                *result = getterReturnVal;
                pop();
                return PROP_FOUND;
            }
        }

        // step b: check for first-class bound methods (method tear-offs)
        if (findMethod(klass, name, result)) {
            *result = OBJ_VAL(newBoundMethod(receiver, *result));
            return PROP_FOUND;
        }

        // step c: fallback to legacy direct c pointer getters
        if (klass->getter != NULL) {
            *result = klass->getter(receiver, name);
            return PROP_FOUND;
        }
    }
    return PROP_NOT_FOUND;
}

Value getPropertySync(Value receiver, ObjString* name) {
    Value result;
    PropertyResult res = getProperty(receiver, name, &result);

    if (res == PROP_FOUND) {
        return result;
    }

    if (res == PROP_NOT_FOUND) {
        return NIL_VAL;
    }

    int oldExitDepth = vm.nativeExitDepth;

    vm.nativeExitDepth = vm.frameCount - 1;
    InterpretResult intresult = run();
    vm.nativeExitDepth = oldExitDepth;
    if (intresult == INTERPRET_OK) {
        return pop();
    }

    return NIL_VAL;
}

PropertyResult setProperty(Value receiver, ObjString* name, Value value, Value* result) {
    ObjClass* constClass = NULL;

    if (IS_CLASS(receiver)) {
        ObjClass* klass = AS_CLASS(receiver);
        if (klass->isFrozen) {
            return PROP_FROZEN;
        }
    }

    if (IS_INSTANCE(receiver)) {
        if (AS_INSTANCE(receiver)->isFrozen) return PROP_FROZEN;
    }

    if (IS_INSTANCE(receiver)) {
        constClass = AS_INSTANCE(receiver)->obj.klass;
    } else if (IS_CLASS(receiver)) {
        constClass = AS_CLASS(receiver);
    } else {
        constClass = getClassForValue(receiver);
    }

    if (constClass != NULL) {
        ObjClass* currentClass = constClass;
        while (currentClass != NULL) {
            Value dummy;

            Table* constants = (currentClass->mixinsource != NULL)
                ? &currentClass->mixinsource->constants
                : &currentClass->constants;

            if (tableGet(constants, name, &dummy)) {
                return PROP_IMMUTABLE;
            }
            currentClass = currentClass->superclass;
        }
    }

    ObjClass* klass = getClassForValue(receiver);

    if (klass != NULL) {
        ObjClass* currentClass = klass;
        Value propertySetter = NIL_VAL;

        while (currentClass != NULL) {
            Table* setters = (currentClass->mixinsource != NULL)
                ? &currentClass->mixinsource->setters
                : &currentClass->setters;

            if (tableGet(setters, name, &propertySetter)) {
                break;
            }
            currentClass = currentClass->superclass;
        }

        if (!IS_NIL(propertySetter)) {
            push(propertySetter);
            push(receiver);
            push(value);

            if (callValue(propertySetter, 1)) {
                return PROP_ASYNC;
            }

            *result = peek(0);
            pop();
            return PROP_FOUND;
        }

        currentClass = klass;
        Value setterVal = NIL_VAL;

        while (currentClass != NULL) {
            Value currentVSetter = (currentClass->mixinsource != NULL)
                ? currentClass->mixinsource->vSetter
                : currentClass->vSetter;

            if (!IS_NIL(currentVSetter)) {
                setterVal = currentVSetter;
                break;
            }
            currentClass = currentClass->superclass;
        }

        if (!IS_NIL(setterVal)) {
            pop();
            pop();

            push(setterVal);
            push(receiver);
            push(OBJ_VAL(name));
            push(value);

            if (callValue(setterVal, 3)) {
                return PROP_ASYNC;
            }

            pop();
            *result = value;
            return PROP_FOUND;
        }

        if (klass->setter != NULL) {
            klass->setter(receiver, name, value);
            *result = value;
            return PROP_FOUND;
        }
    }

    if (IS_INSTANCE(receiver)) {
        ObjInstance* instance = AS_INSTANCE(receiver);
        tableSet(&instance->fields, name, value);
        *result = value;
        return PROP_FOUND;
    }

    return PROP_NOT_FOUND;
}

Value setPropertySync(Value receiver, ObjString* name, Value value) {
    Value result;
    PropertyResult res = setProperty(receiver, name, value, &result);
    
    if (res == PROP_FOUND) {
        return result;
    }

    if (res == PROP_NOT_FOUND) {
        return NIL_VAL;
    }

    int oldExitDepth = vm.nativeExitDepth;
    vm.nativeExitDepth = vm.frameCount - 1;
    InterpretResult intresult = run();
    vm.nativeExitDepth = oldExitDepth;
    if (intresult == INTERPRET_OK) {
        return pop();
    }

    return NIL_VAL;
}

static Value classAddMethodNative(int argCount, Value* args) {
    if (argCount < 2) {
        runtimeError("add_method() expects 2 arguments (name, function).");
        return NIL_VAL;
    }

    // the receiver (args[-1] must be the Class object itself
    if (!IS_CLASS(args[-1])) {
        runtimeError("add_method() can only be called on class objects.");
        return NIL_VAL;
    }

    ObjClass* klass = AS_CLASS(args[-1]);

    if (!IS_STRING(args[0])) {
        runtimeError("First argument to add_method() must be a string name.");
        return NIL_VAL;
    }
    ObjString* methodName = AS_STRING(args[0]);

    if (!IS_CLOSURE(args[1]) && !IS_NATIVE(args[1])) {
        runtimeError("Second argument to add_method() must be a callable function.");
        return NIL_VAL;
    }

    tableSet(&klass->methods, methodName, args[1]);

    return NIL_VAL;
}

static Value classNameNative(int argCount, Value* args) {
    if (!IS_CLASS(args[-1])) {
        return OBJ_VAL(copyString("Object", 6));
    }

    ObjClass* klass = AS_CLASS(args[-1]);
    return OBJ_VAL(klass->name);
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
    vm.numNotation = 1; // 1 = sci, 0 = %.0f
    vm.numPrecision = 6;
    vm.strictMode = false;
    vm.warnMode = true;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    vm.moduleCount = 0;
    vm.moduleCapacity = 0;
    vm.moduleHandles = NULL;

    vm.includePathCount = 0;

    initTable(&vm.globals);
    initTable(&vm.strings);
    initTable(&vm.globalConstants);

    ObjString* stringName = copyString("String", 6);
    push(OBJ_VAL(stringName));

    vm.stringClass = newClass(stringName);
    vm.stringClass->callHandler = strNative;
    vm.stringClass->superclass = NULL;

    stringName->obj.klass = vm.stringClass;
    pop();

    ObjString* string = NULL;

    ObjString* objectName = copyString("Object", 6);
    push(OBJ_VAL(objectName));

    vm.objectClass = newClass(objectName);
    vm.objectClass->superclass = NULL;
    pop();

    vm.stringClass->superclass = vm.objectClass;

    tableSet(&vm.globals, vm.stringClass->name, OBJ_VAL(vm.stringClass));
    tableSet(&vm.globals, vm.objectClass->name, OBJ_VAL(vm.objectClass));
    initStringClass(); // done

    string = copyString("Class", 5);
    push(OBJ_VAL(string));
    vm.classClass = newClass(string);
    vm.classClass->superclass = vm.objectClass;
    pop();

    defineNativeMethod(vm.classClass, "superclass", classSuperclassMethod);
    defineNativeMethod(vm.classClass, "add_method", classAddMethodNative);
    defineNativeMethod(vm.classClass, "name", classNameNative);

    vm.errnoString = NULL;
    vm.errnoString = copyString("errno", 5);
    vm.errstrString = NULL;
    vm.errstrString = copyString("errstr", 6);
    clearLastError();

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
    vm.xString = NULL;
    vm.xString = copyString("x", 1);
    vm.yString = NULL;
    vm.yString = copyString("y", 1);
    vm.zString = NULL;
    vm.zString = copyString("z", 1);
    vm.classString = NULL;
    vm.classString = copyString("class", 5);

    vm.methodMissingString = NULL;
    vm.methodMissingString = copyString("method_missing", 14);

    string = copyString("Function", 8);
    push(OBJ_VAL(string));
    vm.functionClass = newClass(string);
    vm.functionClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.functionClass));
    pop();

    string = copyString("Native", 6);
    push(OBJ_VAL(string));
    vm.nativeFunctionClass = newClass(string);
    vm.nativeFunctionClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.nativeFunctionClass));
    pop();

    string = copyString("Number", 6);
    push(OBJ_VAL(string));
    vm.numberClass = newClass(string);
    vm.numberClass->superclass = vm.objectClass;
    vm.numberClass->callHandler = toNumberNative;
    tableSet(&vm.globals, string, OBJ_VAL(vm.numberClass));
    pop();

    string = copyString("Bool", 4);
    push(OBJ_VAL(string));
    vm.boolClass = newClass(string);
    vm.boolClass->superclass = vm.objectClass;
    vm.boolClass->callHandler = boolCallHandler;
    tableSet(&vm.globals, string, OBJ_VAL(vm.boolClass));
    pop();

    string = copyString("Nil", 3);
    push(OBJ_VAL(string));
    vm.nilClass = newClass(string);
    vm.nilClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(vm.nilClass));
    pop();

    string = copyString("Module", 6);
    push(OBJ_VAL(string));
    vm.moduleClass = newClass(string);
    vm.moduleClass->superclass = vm.objectClass;
    pop();

    initCoreLibrary(); // done

    initResultAndOptionClass(); // done
    initMathLibrary(); // done
    initSystemLibrary(argc, argv, env);
    initProcessClass(); //
    initFileLibrary(); //
    initRegexClass(); //
    initVec3Library();
    initGCLibrary(); //
    initArrayClass(); // done
    initMapClass(); //done
    initIOClass();
    initStructClass(); //

    vm.debugPrintCode = false;
    vm.debugTraceExecution = false;
}

void freeVM() {
    freeObjects();

    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeTable(&vm.globalConstants);

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
    if (vm.stackTop - vm.stack >= STACK_MAX) {
        fprintf(stderr, "Stack overflow error.");
        exit(1);
    }
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    if (vm.stackTop - vm.stack <= 0) {
        fprintf(stderr, "Stack underflow error.");
        exit(1);
    }
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

bool vmCall(ObjClosure* closure, int argCount) {
    ObjFunction* function = closure->function;
    int namedArity = function->isVariadic ? function->arity - 1 : function->arity;

    // 1. validate argument bounds first
    // check if the user passed fewer than the absolute minimum required args
    if (function->isVariadic) {
        if (argCount < function->minArity) {
            runtimeError("Expected at least %d arguments but got %d.",
                    function->minArity, argCount);
            return false;
        }
    } else {
        if (argCount < function->minArity || argCount > function->arity) {
            runtimeError("Expected between %d and %d arguments but got %d.",
                    function->minArity, function->arity, argCount);
            return false;
        }
    }

    // 2. fill in default args safely
    if (argCount < namedArity) {
        int missing = namedArity - argCount;
        for (int i = 0; i < missing; i++) {
            int defaultIndex = (function->defaults.count - missing) + i;
            push(function->defaults.values[defaultIndex]);
        }
        argCount = namedArity;
    }

    // 3. handle variadic rest parameters
    /*
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
    */

    if (function->isVariadic) {
        int numRest = argCount - namedArity;
        if (numRest < 0) numRest = 0;

        ObjArray* restArray = newArray();
        push(OBJ_VAL(restArray));

        for (int i = 0; i < numRest; i++) {
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

    //frame->slots = vm.stackTop - closure->function->arity - 1;
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

bool callValue(Value callee, int argCount) {
    vm.exceptionThrown = false;

    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD:
                {
                    ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                    vm.stackTop[-argCount - 1] = bound->receiver;
                    int res = callValue(bound->method, argCount);
                    if (vm.exceptionThrown) {
                        vm.exceptionThrown = false;
                        return false;
                    }
                    return res;
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
                        if (vm.exceptionThrown) {
                            vm.exceptionThrown = false;

                            return false;
                        }

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
                            if (vm.exceptionThrown) {
                                vm.exceptionThrown = false;
                                return false;
                            }
                            if (vm.frameCount == 0) return false;

                            vm.stackTop -= argCount + 1;
                            push(result);
                            return true;
                        } else {
                            int res = vmCall(AS_CLOSURE(initializer), argCount);
                            if (vm.exceptionThrown) {
                                vm.exceptionThrown = false;
                                return false;
                            }
                            return res;
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
                    if (vm.exceptionThrown) {
                        vm.exceptionThrown = false;
                        return false;
                    }
                    if (vm.frameCount == 0) return false;

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

bool isFalsey(Value value) {
    if (IS_NIL(value)) return true;
    if (IS_BOOL(value)) return !AS_BOOL(value);

    if (IS_INSTANCE(value)) {
        ObjInstance* instance = AS_INSTANCE(value);

        if (instance->obj.klass == vm.resultClass) {
            Value okVal;

            if (tableGet(&instance->fields, vm.okString, &okVal)) {
                return isFalsey(okVal);
            }
            return true;
        }
        if (instance->obj.klass == vm.optionClass) {
            Value is_some;

            if (tableGet(&instance->fields, vm.isSomeString, &is_some)) {
                return isFalsey(is_some);
            }
            return true;
        }
    }
    return false;
}

bool isTruthy(Value value) {
    return !isFalsey(value);
}

bool invokeFromClass(ObjClass* klass, ObjString* name,
        int argCount) {
    ObjClass* current = klass;
    Value method;

    while (current != NULL) {
        Table* methods = (current->mixinsource != NULL)
            ? &current->mixinsource->methods
            : &current->methods;

        if (tableGet(methods, name, &method)) {
            if (IS_NATIVE(method)) {
                NativeFn native = AS_NATIVE(method);

                // 5. call the function (total args is now argCoutn + 1)
                //Value result = native(argCount + 1, vm.stackTop - argCount - 1);
                Value result = native(argCount, vm.stackTop - argCount);
                if (vm.exceptionThrown) {
                    vm.exceptionThrown = false;
                    return false;
                }
                if (vm.frameCount == 0) return false;
                vm.stackTop -= (argCount + 1);
                push(result);
                return true;
            }
            if (IS_CLOSURE(method)) {
                ObjClosure* closure = AS_CLOSURE(method);

                // if its freestanding function being used as a method,
                // adapt the stack to match a standard function layout
                if (closure->function->isfree) {
                    int expectedArgs = closure->function->arity - 1;

                    if (argCount != expectedArgs) {
                        runtimeError("Expedted %d arguments but got %d.",
                                expectedArgs, argCount);
                        return false;
                    }

                    // 1. slide the instance receiver and arguments up by one slot
                    Value* start = vm.stackTop - argCount - 1;
                    for (Value* p = vm.stackTop; p > start; p--) {
                        *p = *(p - 1);
                    }

                    // 2. drop the closure into slot 0 of this frame area
                    *start = method;

                    // 3. account for the newly added slot and the explicit self parameter
                    vm.stackTop++;
                    argCount++;
                }

                // now vmCall receives an argCount that includes 'self', matching the arity
                int res = vmCall(closure, argCount);
                if (vm.exceptionThrown) {
                    vm.exceptionThrown = false;
                    return false;
                }
                return res;
            }

            int res = callValue(method, argCount);
            if (vm.exceptionThrown) {
                vm.exceptionThrown = false;
                return false;
            }
            return res;
        }
        current = current->superclass;
    }

    runtimeError("Undefined property '%s'.", name->chars);
    return false;
}

bool classHasMethod(ObjClass* klass, ObjString* name) {
    ObjClass* current = klass;
    Value method;
    while (current != NULL) {
        Table *methods = (current->mixinsource != NULL)
             ? &current->mixinsource->methods
             : &current->methods;

        if (tableGet(methods, name, &method)) {
            return true;
        }
        current = current->superclass;
    }
    return false;
}

bool lookupClassMethod(ObjClass* klass, ObjString* name, Value* methodOut) {
    ObjClass* current = klass;
    while (current != NULL) {
        Table *methods = (current->mixinsource != NULL)
             ? &current->mixinsource->methods
             : &current->methods;

        if (tableGet(methods, name, methodOut)) {
            return true;
        }
        current = current->superclass;
    }
    return false;
}

bool invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);

    /*
    if (IS_CLASS(receiver)) {
        ObjClass* klass = AS_CLASS(receiver);
        Value method;
        bool found = false;

        ObjClass* currentClass = klass;
        while (currentClass != NULL) {
            if (tableGet(&klass->methods, name, &method)) {
                found = true;
                break;
            }
            currentClass = currentClass->superclass;
        }

        if (!found) {
            currentClass = vm.classClass;
            while (currentClass != NULL) {
                if (tableGet(&currentClass->methods, name, &method)) {
                    found = true;
                    break;
                }
                currentClass = currentClass->superclass;
            }
        }
        
        if (found) {
            if (IS_CLASS(method)) {
                vm.stackTop[-argCount - 1] = method;
            }
            return callValue(method, argCount);
        }
        runtimeError("Undefined static method '%s' on class '%s'.", name->chars, klass->name->chars);
        return false;
    }
    */

    ObjClass* klass = getClassForValue(receiver);
    
    /*
    printf("DEBUG: Looking for method '%s' on Class '%s' %d\n",
            name->chars, klass->name->chars, klass);
    printf("Number class: %d\n", vm.numberClass);
    */

    // 1. Standard lox instances
    if (IS_INSTANCE(receiver)) {
        ObjInstance* instance = AS_INSTANCE(receiver);
        Value field;

        if (tableGet(&instance->fields, name, &field)) {
            vm.stackTop[-argCount - 1] = field;
            return callValue(field, argCount);
        }

        ObjClass* currentClass = instance->obj.klass;
        while (currentClass != NULL) {
            Value method;

            Table* methods = (currentClass->mixinsource != NULL)
                ? &currentClass->mixinsource->methods
                : &currentClass->methods;

            if (tableGet(methods, name, &method)) {
                bool res = invokeFromClass(instance->obj.klass, name, argCount);
                if (vm.exceptionThrown) {
                    vm.exceptionThrown = false;
                    return false;
                }
                return res;
            }
            currentClass = currentClass->superclass;
        }

        return callMethodMissing(instance->obj.klass, name, argCount);
    } else if (IS_VEC3(receiver)) {
        ObjClass* currentClass = vm.vec3Class;
        Value method;

        while (currentClass != NULL) {
            if (tableGet(&currentClass->methods, name, &method)) {
                if (IS_NATIVE(method)) {
                    NativeFn native = AS_NATIVE(method);
                    Value result = native(argCount, vm.stackTop - argCount);
                    if (vm.frameCount == 0) return false;
                    vm.stackTop -= (argCount + 1);
                    push(result);
                    return true;
                }
                bool res = invokeFromClass(currentClass, name, argCount);
                if (vm.exceptionThrown) {
                    vm.exceptionThrown = false;
                    return false;
                }
                return res;
            }
            currentClass = currentClass->superclass;
        }

        if (vm.objectClass != NULL && tableGet(&vm.objectClass->methods, name, &method)) {
            if (IS_NATIVE(method)) {
                NativeFn native = AS_NATIVE(method);
                Value result = native(argCount, vm.stackTop - argCount);
                if (vm.frameCount == 0) return false;
                vm.stackTop -= (argCount + 1);
                push(result);
                return true;
            }
            bool res = invokeFromClass(currentClass, name, argCount);
            if (vm.exceptionThrown) {
                vm.exceptionThrown = false;
                return false;
            }
            return res;
        }

        return callMethodMissing(vm.vec3Class ? vm.vec3Class : vm.objectClass, name, argCount);
    } else if (IS_STRING(receiver)) {
        //ObjClass* currentClass = (IS_STRING(receiver)) ? vm.stringClass : vm.objectClass;
        ObjClass* currentClass = vm.stringClass;
        Value method;

        while (currentClass != NULL) {
            if (tableGet(&currentClass->methods, name, &method)) {
                if (IS_NATIVE(method)) {
                    NativeFn native = AS_NATIVE(method);
                    Value result = native(argCount, vm.stackTop - argCount);
                    if (vm.frameCount == 0) return false;
                    vm.stackTop -= (argCount + 1);
                    push(result);
                    return true;
                }

                bool res = invokeFromClass(vm.objectClass, name, argCount);
                if (vm.exceptionThrown) {
                    vm.exceptionThrown = false;
                    return false;
                }
                return res;
            }
            currentClass = currentClass->superclass;
        }
        return callMethodMissing(vm.stringClass, name, argCount);
    } else if (IS_CLASS(receiver)) {
        ObjClass* klass = AS_CLASS(receiver);
        bool res;
        Value method;

        if (lookupClassMethod(klass, name, &method)) {
            if (IS_CLASS(method)) {
                vm.stackTop[-argCount - 1] = method;
            }

            res = invokeFromClass(klass, name, argCount);
            if (vm.exceptionThrown) {
                vm.exceptionThrown = false;
                return false;
            }
        } else {
            res = invokeFromClass(vm.classClass, name, argCount);
            if (vm.exceptionThrown) {
                vm.exceptionThrown = false;
                return false;
            }
        }
        if (!res)
            return callMethodMissing(klass, name, argCount);
        return true;
    } else {
        ObjClass* klass = getClassForValue(receiver);
        if (klass != NULL) {
            bool res = invokeFromClass(klass, name, argCount);
            if (vm.exceptionThrown) {
                vm.exceptionThrown = false;
                return false;
            }
            if (!res)
                return callMethodMissing(klass, name, argCount);
            return true;
        }
    }
    runtimeError("Only instances and primitives have methods.");
    return false;
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
    Value method;
    ObjClass* current = klass;

    while (current != NULL) {
        Table* methods = (current->mixinsource != NULL)
            ? &current->mixinsource->methods
            : &current->methods;

        if (tableGet(methods, name, &method)) {
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
            if (runtimeError("Operands must be numbers.")) { \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            break; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
    } while (false)

    for (;;) {
        if (vm.debugTraceExecution) {
            printf("        ");
            for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
            printf("\n");
            disassembleInstruction(&frame->closure->function->chunk,
                    (int)(frame->ip - frame->closure->function->chunk.code));
        }

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
                        RUNTIME_ERROR("Cannot convert value to string.");
                        break;
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
                        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
                        break;
                    }
                    push(value);
                }
                break;
            case OP_GET_GLOBAL_LONG:
                {
                    ObjString* name = READ_STRING_LONG();
                    Value value;
                    if (!tableGet(&vm.globals, name, &value)) {
                        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
                        break;
                    }
                    push(value);
                }
                break;
            case OP_DEFINE_CLASS_CONST:
            case OP_DEFINE_CLASS_CONST_LONG:
                {
                    ObjString* name = (instruction == OP_DEFINE_CLASS_CONST)
                        ? READ_STRING()
                        : READ_STRING_LONG();

                    Value constantValue = peek(0);
                    Value classVal = peek(1);

                    if (!IS_CLASS(classVal)) {
                        RUNTIME_ERROR("Can only define constants inside a class scope.");
                        break;
                    }

                    ObjClass* klass = AS_CLASS(classVal);
                    tableSet(&klass->constants, name, constantValue);
                    pop();
                }
                break;
            case OP_DEFINE_GLOBAL:
                {
                    ObjString* name = READ_STRING();
                    tableSet(&vm.globals, name, peek(0));
                    pop();
                }
                break;
            case OP_DEFINE_GLOBAL_CONST:
                {
                    ObjString* name = READ_STRING();
                    tableSet(&vm.globals, name, peek(0));
                    tableSet(&vm.globalConstants, name, BOOL_VAL(true));
                    pop();
                }
                break;
            case OP_DEFINE_GLOBAL_LONG:
                {
                    ObjString* name = READ_STRING_LONG();
                    tableSet(&vm.globals, name, peek(0));
                    pop();
                }
                break;
            case OP_DEFINE_GLOBAL_CONST_LONG:
                {
                    ObjString* name = READ_STRING_LONG();
                    tableSet(&vm.globals, name, peek(0));
                    tableSet(&vm.globalConstants, name, BOOL_VAL(true));
                    pop();
                }
                break;
            case OP_SET_GLOBAL:
                {
                    ObjString* name = READ_STRING();
                    Value dummy;

                    if (tableGet(&vm.globalConstants, name, &dummy)) {
                        RUNTIME_ERROR("Canot reassign global constant '%s'.", name->chars);
                        break;
                    }

                    if (tableSet(&vm.globals, name, peek(0))) {
                        tableDelete(&vm.globals, name);
                        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
                        break;
                    }
                }
                break;
            case OP_SET_GLOBAL_LONG:
                {
                    ObjString* name = READ_STRING_LONG();
                    Value dummy;
                    if (tableGet(&vm.globalConstants, name, &dummy)) {
                        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
                        break;
                    }

                    if (tableSet(&vm.globals, name, peek(0))) {
                        tableDelete(&vm.globals, name);
                        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
                        break;
                    }
                }
                break;
            case OP_GET_PROPERTY:
            case OP_GET_PROPERTY_LONG:
                {
                    ObjString* name = (instruction == OP_GET_PROPERTY)
                        ? READ_STRING()
                        : READ_STRING_LONG();
                    
                    Value receiver = pop();
                    Value resolvedValue;

                    PropertyResult res = getProperty(receiver, name, &resolvedValue);

                    if (res == PROP_FOUND) {
                        push(resolvedValue);
                        break;
                    } else if (res == PROP_ASYNC) {
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }

                    RUNTIME_ERROR("Undefined property or method '%s'.", name->chars);
                }
                break;
            case OP_SET_PROPERTY:
            case OP_SET_PROPERTY_LONG:
                {
                    ObjString* name = (instruction == OP_SET_PROPERTY)
                        ? READ_STRING()
                        : READ_STRING_LONG();

                    Value value = peek(0);
                    Value receiver = peek(1);
                    Value final;

                    PropertyResult res = setProperty(receiver, name, value, &final);

                    if (res == PROP_FOUND) {
                        pop();
                        pop();
                        push(final);
                        break;
                    }

                    if (res == PROP_ASYNC) {
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }

                    if (res == PROP_IMMUTABLE) {
                        RUNTIME_ERROR("Cannot reassign or shadow constant property '%s'.", name->chars);
                        break;
                    }

                    if (res == PROP_FROZEN) {
                        if (IS_CLASS(receiver)) {
                            RUNTIME_ERROR("Cannot modify properties or methods on frozen class '%s'.",
                                    AS_CLASS(receiver)->name->chars);
                        } else {
                            ObjClass* instanceClass = getClassForValue(receiver);
                            RUNTIME_ERROR("Cannot modify properties on frozen instance of class '%s'.",
                                    instanceClass != NULL ? instanceClass->name->chars : "Object");
                        }
                        break;
                    }
                            
                    RUNTIME_ERROR("Cannot set property '%s' on target.", name->chars);
                    break;
                }
            case OP_GETTER:
            case OP_GETTER_LONG:
                {
                    ObjString* name = (instruction == OP_GETTER)
                        ? READ_STRING()
                        : READ_STRING_LONG();
                    Value closure = peek(0);
                    ObjClass* klass = AS_CLASS(peek(1));

                    tableSet(&klass->getters, name, closure);
                    pop();
                }
                break;
            case OP_SETTER:
            case OP_SETTER_LONG:
                {
                    ObjString* name = (instruction == OP_SETTER)
                        ? READ_STRING()
                        : READ_STRING_LONG();
                    Value closure = peek(0);
                    ObjClass* klass = AS_CLASS(peek(1));

                    tableSet(&klass->setters, name, closure);
                    pop();
                }
                break;
            case OP_GET_SUPER:
                {
                    ObjString* name = READ_STRING();
                    ObjClass* superclass = AS_CLASS(pop());

                    if (!bindMethod(superclass, name)) {
                        RUNTIME_ERROR("Can't bind method.");
                        break;
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

                    /*
                    if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                        concatenate();
                        break;
                    } 
                    */
                    if (IS_STRING(peek(0)) || IS_STRING(peek(1))) {
                        Value rawB = peek(0);
                        Value rawA = peek(1);

                        Value bVal = valueToString(rawB);
                        push(bVal);

                        Value aVal = valueToString(rawA);
                        push(aVal);

                        ObjString* aStr = AS_STRING(aVal);
                        ObjString* bStr = AS_STRING(bVal);

                        int length = aStr->length + bStr->length;
                        char* chars = ALLOCATE(char, length + 1);
                        memcpy(chars, aStr->chars, aStr->length);
                        memcpy(chars + aStr->length, bStr->chars, bStr->length);
                        chars[length] = '\0';

                        ObjString* result = takeString(chars, length);
                        popn(4);

                        push(OBJ_VAL(result));
                    } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
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
                    } else if (IS_ARRAY(peek(0)) && IS_ARRAY(peek(1))) {
                        ObjArray* b = AS_ARRAY(peek(0));
                        ObjArray* a = AS_ARRAY(peek(1));

                        ObjArray *result = newArray();
                        //result->obj.klass = vm.arrayClass;
                        push(OBJ_VAL(result));

                        for (int i = 0; i < a->count; i++) {
                            arrayAppend(result, a->values[i]);
                        }

                        for (int i = 0; i < b->count; i++) {
                            arrayAppend(result, b->values[i]);
                        }
                        vm.stackTop[-3] = vm.stackTop[-1];
                        popn(2);
                    } else {
                        RUNTIME_ERROR("Invalid operands.");
                        break;
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
                        RUNTIME_ERROR("Invalid operands.");
                        break;
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
                        RUNTIME_ERROR("Invalid operands.");
                        break;
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
                        RUNTIME_ERROR("Invalid operands.");
                        break;
                    }
                }
                break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_POW:
                {
                    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                        RUNTIME_ERROR("Operands must be numbers.");
                        break;
                    }
                    Value b = pop();
                    Value a = pop();
                    push(NUMBER_VAL(pow(AS_NUMBER(a), AS_NUMBER(b))));
                }
                break;
            case OP_XOR:
                {
                    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
                        RUNTIME_ERROR("Operands must be numbers.");
                        break;
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
                        RUNTIME_ERROR("Operands must be numbers.");
                        break;
                    }
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());

                    if (b == 0) {
                        RUNTIME_ERROR("Division by zero.");
                        break;
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
                        RUNTIME_ERROR("Operand must be a number.");
                        break;
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
                                // 1. setup
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
                                            return INTERPRET_RUNTIME_ERROR;
                                        }
                                    }

                                    //vm.nativeExitDepth = vm.frameCount - 1;
                                    //run();
                                    Value result = pop();

                                    if (!IS_NIL(result)) {
                                        printValue(result);
                                    }
                                }
                                vm.nativeExitDepth = oldExitDepth;
                            } else {
                                printValue(value);
                            }
                            //vm.stackTop = stackStart;
                            //pop();
                        } else {
                            printValue(value);
                            //if (i > 0) printf(" ");
                        }
                    }
                    popn(argCount);
                    printf("\n");
                }
                break;
            case OP_TRY:
                {
                    //uint16_t offset = READ_SHORT();
                    uint16_t catchOffset = READ_SHORT();
                    uint16_t finallyOffset = READ_SHORT();

                    if (vm.tryCount >= TRY_STACK_MAX) {
                        RUNTIME_ERROR("Stack overflow: too many nested try blocks.");
                        break;
                    }

                    TryBlock* block = &vm.tryStack[vm.tryCount++];
                    block->frameCount = vm.frameCount;
                    block->stackTop = vm.stackTop;
                    //block->catchIp = frame->ip + offset;
                    block->catchIp = catchOffset == 0 ? NULL : frame->ip + catchOffset;
                    block->finallyIp = finallyOffset == 0 ? NULL : frame->ip + finallyOffset;

                    block->isReturning = false;
                    block->hasUncaughtException = false;
                }
                break;
            case OP_END_TRY:
                {
                    TryBlock* block = &vm.tryStack[vm.tryCount - 1];
                    if (block->finallyIp == NULL) {
                        vm.tryCount--;
                    }
                    uint16_t catchOffset = READ_SHORT();

                    frame->ip += catchOffset;
                }
                break;
            case OP_END_FINALLY:
                {
                    TryBlock* block = &vm.tryStack[--vm.tryCount];

                    if (block->isReturning) {
                        push(block->returnValue);

                        Value result = pop();
                        vm.frameCount--;
                        if (vm.frameCount == 0) {
                            pop();
                            return INTERPRET_OK;
                        }
                        vm.stackTop = frame->slots;
                        push(result);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }

                    if (block->hasUncaughtException) {
                        raiseException(block->uncaughtException);
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                }
                break;
            case OP_THROW:
                {
                    Value exception = pop();

                    if (IS_STRING(exception)) {
                        Value errorClassVal;
                        ObjString* errorName = copyString("Error", 5);

                        if (tableGet(&vm.globals, errorName, &errorClassVal) && IS_CLASS(errorClassVal)) {
                            //pop();

                            ObjClass* errorClass = AS_CLASS(errorClassVal);
                            ObjInstance* errorInstance = newInstance(errorClass);
                            push(OBJ_VAL(errorInstance));

                            ObjString* messageKey = copyString("message", 7);
                            push(OBJ_VAL(messageKey));

                            Value stringMsg = peek(2);
                            tableSet(&errorInstance->fields, messageKey, stringMsg);

                            pop(); // messageKey
                            pop(); // errorInstance
                            pop(); // the origin string

                            push(OBJ_VAL(errorInstance));
                        //} else {
                        //    pop();
                        }
                    }
                    //Value exceptionToThrow = pop();
                    raiseException(exception);

                    frame = &vm.frames[vm.frameCount - 1];
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
            case OP_INSTANCEOF:
                {
                    if (!IS_CLASS(peek(0))) {
                        RUNTIME_ERROR("Right-hand side of type check must be a class.");
                        break;
                        //return INTERPRET_RUNTIME_ERROR;
                    }
                    ObjClass* targetClass = AS_CLASS(pop());
                    Value instance = pop();

                    push(BOOL_VAL(isInstanceOf(instance, targetClass)));
                }
                break;
            case OP_CALL:
                {
                    int argCount = READ_BYTE();
                    if (!callValue(peek(argCount), argCount) || vm.frameCount == 0) {
                        RUNTIME_ERROR("Call failed.");
                        break;
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

                    Value receiver = peek(argCount);
                    ObjClass* klass = getClassForValue(receiver);

                    if (klass == NULL) {
                        RUNTIME_ERROR("Method calls are not supported on this type.");
                        break;
                    }

                    if (!invoke(method, argCount)) {
                        //RUNTIME_ERROR("Undefined method '%s'.", method->chars);
                        if (vm.frameCount == 0) return INTERPRET_RUNTIME_ERROR;
                        frame = &vm.frames[vm.frameCount - 1];
                        break;
                    }
                    if (vm.frameCount == 0) return INTERPRET_RUNTIME_ERROR;
                    frame = &vm.frames[vm.frameCount - 1];
                    break;
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
                        RUNTIME_ERROR("Only instances have methods.");
                        break;
                    }

                    ObjInstance* instance = AS_INSTANCE(receiver);
                    if (!invokeFromClass(instance->obj.klass, method, totalArgs)) {
                        RUNTIME_ERROR("Call failed.");
                        break;
                    }
                    frame = &vm.frames[vm.frameCount - 1];
                }
                break;
            case OP_UNPACK:
                {
                    uint8_t expectedCount = READ_BYTE();
                    Value value = pop();

                    if (!IS_ARRAY(value)) {
                        RUNTIME_ERROR("Can only destructure arrays.");
                        break;
                    }

                    ObjArray* array = AS_ARRAY(value);
                    int actualCount = array->count;

                    // case 1: not enough elements to satisfy variables
                    if (actualCount < expectedCount) {
                        RUNTIME_ERROR("Destructuring mismatch: Expected %d elements, but array only has %d.",
                                expectedCount, actualCount);
                        break;
                    }

                    // case 2: too many elements (the strict/warn zone)
                    if (actualCount > expectedCount) {
                        if (vm.strictMode) {
                            RUNTIME_ERROR("Destructuring mismath: Strict mode active. Extraneous array elements detected.");
                            break;
                        } else if (vm.warnMode) {
                            printf("Warning: Destructuring assigment ignored %d trailing array elements.\n",
                                    actualCount - expectedCount);
                        }
                    }

                    // case 3: clean extraction (unpack in reverse order)
                    for (int i = expectedCount - 1; i >= 0; i--) {
                        push(array->values[i]);
                    }
                }
                break;
            case OP_SUPER_INVOKE:
                {
                    ObjString* method = READ_STRING();
                    int argCount = READ_BYTE();
                    ObjClass* superclass = AS_CLASS(pop());
                    if (!invokeFromClass(superclass, method, argCount)) {
                        RUNTIME_ERROR("Call failed.");
                        break;
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
                        RUNTIME_ERROR("Could not load module.");
                        break;
                    }
                    //tableSet(&vm.globals, moduleName, peek(0));
                    //pop();
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
                        RUNTIME_ERROR("Call failed.");
                        break;
                    }
                    frame = &vm.frames[vm.frameCount - 1];
                }
                break;
            case OP_SPLAT:
                {
                    Value value = peek(0);
                    if (!IS_ARRAY(value)) {
                        RUNTIME_ERROR("Can only splat arrays.");
                        break;
                    }

                    ObjArray* array = AS_ARRAY(value);
                    pop();
                    int count = array->count;
                    //int currentTotal = AS_NUMBER(pop());

                    if (vm.stackTop > vm.stack && IS_SPLAT_COUNT(peek(0))) {
                        count += AS_SPLAT_COUNT(pop());
                    }

                    if (vm.stackTop + array->count >= vm.stack + STACK_MAX) {
                        RUNTIME_ERROR("Stack overflow during splat.");
                        break;
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
                        RUNTIME_ERROR("Only classes can be included.");
                        break;
                    }

                    ObjClass* mixin = AS_CLASS(mixinVal);
                    ObjClass* target = AS_CLASS(targetVal);

                    ObjClass* proxy = newClass(mixin->name);

                    push(OBJ_VAL(proxy));

                    proxy->mixinsource = mixin;

                    proxy->superclass = target->superclass;
                    target->superclass = proxy;

                    pop();
                    //tableMergeGuard(&mixin->methods, &target->methods);
                    pop();
                }
                break;
            case OP_RETURN:
                {
                    Value result = pop();

                    if (vm.tryCount > 0 && vm.tryStack[vm.tryCount - 1].frameCount == vm.frameCount) {
                        TryBlock* block = &vm.tryStack[vm.tryCount - 1];
                        if (block->finallyIp != NULL && !block->isReturning) {
                            block->isReturning = true;
                            block->returnValue = result;
                            vm.stackTop = block->stackTop;
                            frame->ip = block->finallyIp;
                            break;
                        }
                    }
                    //Value result = pop();
                    closeUpvalues(frame->slots);

                    bool isGetterFrame = frame->isGetter;
                    bool isSetterFrame = frame->isSetter;
                    vm.frameCount--;

                    if (vm.frameCount == 0) {
                        pop();
                        return INTERPRET_OK;
                    }

                    Value* slots = frame->slots;
                    vm.stackTop = slots;

                    if (isGetterFrame) {
                        if (isResultInstance(result)) {
                            if (isResultOk(result)) {
                                Value fakeStack[2] = { result, NIL_VAL };
                                result = resultUnwrapOrNative(1, &fakeStack[1]);
                            } else {
                                runtimeError("Property getter returned an error Result state.");
                                return INTERPRET_RUNTIME_ERROR;
                            }
                        }
                    } else if (isSetterFrame) {
                        result = slots[2];
                    }

                    push(result);

                    if  (vm.frameCount == vm.nativeExitDepth) {
                        vm.nativeExitDepth = -1;
                        return INTERPRET_OK;
                    }
                    if (vm.frameCount == 0) return false;

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
                        RUNTIME_ERROR("Superclass must be a class.");
                        break;
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
                            RUNTIME_ERROR("Map keys must be strings.");
                            break;
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
                    //array->obj.klass = vm.arrayClass;
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
                        RUNTIME_ERROR("Array size must be a number.");
                        break;
                    }

                    int count = (int)AS_NUMBER(sizeVal);
                    ObjArray* array = newArray();
                    //array->obj.klass = vm.arrayClass;
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
                            RUNTIME_ERROR("Map index must be a string.");
                            break;
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
                            RUNTIME_ERROR("Vec3 index must be a number.");
                            break;
                        }

                        double rawindex = AS_NUMBER(indexValue);
                        int index = (int)rawindex;

                        if (rawindex != (double)index) {
                            RUNTIME_ERROR("Vec3 index must be a whole integer.");
                            break;
                        }

                        if (index < 0 || index > 2) {
                            RUNTIME_ERROR("Vec3 index out of bounds.");
                            break;
                        }
                        Vec3 vec3 = AS_VEC3(targetValue);
                        switch (index) {
                            case 0:
                                push(NUMBER_VAL(vec3.x));
                                break;
                            case 1:
                                push(NUMBER_VAL(vec3.y));
                                break;
                            case 2:
                                push(NUMBER_VAL(vec3.z));
                                break;
                        }
                    }

                    if (IS_ARRAY(targetValue)) {
                        ObjArray* array = AS_ARRAY(targetValue);

                        if (!IS_NUMBER(indexValue)) {
                            RUNTIME_ERROR("Array index must be a number.");
                            break;
                        }

                        int index = (int)AS_NUMBER(indexValue);
                        if (index < 0 || index >= array->count) {
                            RUNTIME_ERROR("Array index out of bounds.");
                            break;
                        }

                        push(array->values[index]);
                        break;
                    }

                    if (IS_STRING(targetValue)) {
                        if (!IS_NUMBER(indexValue)) {
                            RUNTIME_ERROR("String index must be a number.");
                            break;
                        }

                        ObjString* string = AS_STRING(targetValue);
                        int index = AS_NUMBER(indexValue);

                        if (index < 0 || index >= string->length) {
                            RUNTIME_ERROR("String index out of bounds.");
                            break;
                        }

                        push(NUMBER_VAL((double)(uint8_t)string->chars[index]));

                        break;
                    }

                    RUNTIME_ERROR("Only vec3s, maps and arrays support subscripting.");
                }
                break;
            case OP_SET_INDEX:
                {
                    Value newValue = peek(0);
                    Value indexValue = peek(1);
                    Value targetValue = peek(2);

                    if (IS_MAP(targetValue)) {
                        if (!IS_STRING(indexValue)) {
                            RUNTIME_ERROR("Map keys must be strings.");
                            break;
                        }
                        tableSet(&AS_MAP(targetValue)->items, AS_STRING(indexValue), newValue);
                        vm.stackTop[-3] = newValue;
                        popn(2);
                        //push(newValue);
                        break;
                    } else if (!IS_ARRAY(targetValue)) {
                        RUNTIME_ERROR("Only maps and arrays support subscript assignment.");
                        break;
                    }

                    ObjArray* array = AS_ARRAY(targetValue);

                    if (!IS_NUMBER(indexValue)) {
                        RUNTIME_ERROR("Array index must be a number.");
                        break;
                    }

                    int index = (int)AS_NUMBER(indexValue);
                    if (index < 0 || index >= array->count) {
                        RUNTIME_ERROR("Array index out of bounds.");
                        break;
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

InterpretResult interpret(const char* source, const char* filename) {
    const char* line = source;
    /*
    while (strncmp(line, "include ", 8) == 0) {
        char* startQuote = strchr(line, '"');
        char* endQuote = startQuote ? strchr(startQuote + 1, '"') : NULL;

        if (startQuote && endQuote) {
            int len = endQuote - startQuote - 1;
            char* incPath = malloc(len + 1);
            if (incPath == NULL) {
                fprintf(stderr, "Fatal Error: Out of memory allocating include path buffer.\n");
                exit(74);
            }

            strncpy(incPath, startQuote + 1, len);
            incPath[len] = '\0';

            char* incSource = readFile(incPath);
            if (incSource != NULL) {
                interpret(incSource, incPath);
                free(incSource);
            }
            free(incPath);
        }

        while (*line != '\n' && *line != '\0') line++;
        if (*line == '\n') line++;
    }
    */

    vm.frameCount = 0;
    vm.stackTop = vm.stack;

    ObjString* fileObj = copyString(filename, (int)strlen(filename));
    push(OBJ_VAL(fileObj));

    ObjFunction* function = compile(source, fileObj);
    pop();
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    vmCall(closure, 0);

//#define DEBUG_DUMP_COMPILED_BYTECODE
#ifdef DEBUG_DUMP_COMPILED_BYTECODE
    disassembleChunk(&function->chunk, "Compiled Script");
#endif

    InterpretResult result = run();
    if (result == INTERPRET_RUNTIME_ERROR) {
        resetStack();
    }

    return result;
}

