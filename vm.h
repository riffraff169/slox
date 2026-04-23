#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;
    Table globals;
    Table strings;
    ObjString* initString;
    ObjString* toString;
    ObjUpvalue* openUpvalues;

    ObjString* str_add;
    ObjString* str_sub;
    ObjString* str_mul;
    ObjString* str_div;
    ObjString* str_neg;
    ObjString* xString;
    ObjString* yString;
    ObjString* zString;

    Table arrayMethods;

    bool isGC;
    size_t bytesAllocated;
    size_t nextGC;
    int heap_growth_factor;
    int init_threshold;
    int bump_size;
    int stress_mode;
    int gctype; // 1: multiplier, 0: linear

    Obj* objects;
    int grayCount;
    int grayCapacity;
    Obj** grayStack;

    ObjClass* objectClass;
    ObjClass* arrayClass;
    ObjClass* mapClass;
    ObjClass* stringClass;
    ObjClass* mathClass;
    ObjClass* regexClass;
    ObjClass* moduleClass;
    ObjClass* gcClass;
    int nativeExitDepth;

    int moduleCount;
    int moduleCapacity;
    void** moduleHandles;

    //Table giTypes;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void defineNative(const char* name, NativeFn function);
void runtimeError(const char* format, ...);
void initVM(int argc, const char* argv[], const char* env[]);
void freeVM();
bool vmCall(ObjClosure* closure, int argCount);
InterpretResult run();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();
Value peek(int distance);

#endif
