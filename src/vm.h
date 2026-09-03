#ifndef slox_vm_h
#define slox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)
#define MAX_INCLUDE_PATHS 64


typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
    bool isGetter;
    bool isSetter;
    bool isTimer;
} CallFrame;

typedef struct {
    int frameCount;
    Value* stackTop;
    uint8_t* catchIp;
    uint8_t* finallyIp;

    bool isReturning;
    Value returnValue;
    bool hasUncaughtException;
    Value uncaughtException;
} TryBlock;

#define TRY_STACK_MAX 64

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    uint32_t instructionCount;

    Value stack[STACK_MAX];
    Value* stackTop;
    Table globals;
    Table strings;
    Table globalConstants;
    Table requires;
    ObjString* initString;
    ObjString* toString;
    ObjUpvalue* openUpvalues;

    ObjString* str_add;
    ObjString* str_sub;
    ObjString* str_mul;
    ObjString* str_div;
    ObjString* str_neg;
    ObjString* str_lt;
    ObjString* str_gt;
    ObjString* str_le;
    ObjString* str_ge;
    ObjString* str_eq;
    ObjString* xString;
    ObjString* yString;
    ObjString* zString;

    //Table arrayMethods;

    bool isGC;
    size_t bytesAllocated;
    size_t nextGC;
    int heap_growth_factor;
    int gcCount;
    size_t init_threshold;
    size_t bump_size;
    int stress_mode;
    int gctype; // 1: multiplier, 0: linear
    uint8_t numNotation; // 1 = sci %g, 0 = %.0f
    uint8_t numPrecision; // default 6
    bool strictMode;
    bool warnMode;

    Obj* objects;
    int grayCount;
    int grayCapacity;
    Obj** grayStack;

    ObjClass* objectClass;
    ObjClass* objectMetaClass;
    ObjClass* classClass;
    ObjClass* classMetaClass;
    ObjClass* stringClass;
    ObjClass* stringMetaClass;

    ObjClass* numberClass;
    ObjClass* numberMetaClass;
    ObjClass* boolClass;
    ObjClass* boolMetaClass;
    ObjClass* nilClass;
    ObjClass* nilMetaClass;
    ObjClass* arrayClass;
    ObjClass* arrayMetaClass;
    ObjClass* mapClass;
    ObjClass* mapMetaClass;
    ObjClass* setClass;
    ObjClass* setMetaClass;
    ObjClass* mathClass;
    ObjClass* mathMetaClass;
    ObjClass* regexClass;
    ObjClass* regexMetaClass;
    ObjClass* moduleClass;
    ObjClass* moduleMetaClass;
    ObjClass* gcClass;
    ObjClass* gcMetaClass;
    ObjClass* bufferClass;
    ObjClass* bufferMetaClass;
    ObjClass* resultClass;
    ObjClass* resultMetaClass;
    ObjClass* optionClass;
    ObjClass* optionMetaClass;
    ObjClass* functionClass;
    ObjClass* functionMetaClass;
    ObjClass* nativeFunctionClass;
    ObjClass* nativeFunctionMetaClass;
    ObjClass* vec3Class;
    ObjClass* vec3MetaClass;

    int nativeExitDepth;
    bool noStdLib;

    int moduleCount;
    int moduleCapacity;
    void** moduleHandles;

    int lastErrno;

    ObjString* errnoString;
    ObjString* errstrString;

    ObjString* okString;
    ObjString* valString;
    ObjString* errString;
    ObjString* classString;
    ObjString* isSomeString;
    ObjString* methodMissingString;

    TryBlock tryStack[TRY_STACK_MAX];
    int tryCount;
    bool exceptionThrown;

    bool debugPrintCode;
    bool debugTraceExecution;
    ObjArray* includePaths;
    const char* scriptName;

    ValueArray atExitHooks;
    ValueArray globalRoots;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void defineNative(const char* name, NativeFn function);
bool runtimeError(const char* format, ...);
void initVM(int argc, const char* argv[], const char* env[]);
void freeVM();
bool vmCall(ObjClosure* closure, int argCount);
InterpretResult run();
InterpretResult interpret(const char* source, const char* filename);
void push(Value value);
Value pop();
Value popn(int n);
Value peek(int distance);
void setLastError(int errorNum, const char* format, ...);
bool isFalsey(Value value);
bool isTruthy(Value value);
bool callValue(Value callee, int argCount);
bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount);
bool invoke(ObjString* name, int argCount);
void defineNativeMethod(ObjClass* klass, const char* name, NativeFn function);
ObjClass* getClassForValue(Value value);
Value errorResult(const char* format, ...);
Value okResult(Value value);
void clearLastError();
void defineClassConstant(ObjClass* klass, const char* name, Value value);
bool isCallable(Value value);
void runAtExitHooks();
void nativeBindFunction(ObjClass* klass, const char* name, NativeFn fn);
Value objectClassNameNative(int argCount, Value* args);
Value classNameNative(int argCount, Value* args);
Value classSuperclassNative(int argCount, Value* args);
Value createResult(Value value, Value errval, bool isok);
int vmAddRoot(Value value);
void vmRemoveRoot(int handle);

#endif
