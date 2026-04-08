#ifndef clox_object_h
#define clox_object_h

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value)         (AS_OBJ(value)->type)

#define IS_VEC3(value)          isObjType(value, OBJ_VEC3)
#define IS_FOREIGN(value)       isObjType(value, OBJ_FOREIGN)
#define IS_REGEX(value)         isObjType(value, OBJ_REGEX)
#define IS_MAP(value)           isObjType(value, OBJ_MAP)
#define IS_ARRAY(value)         isObjType(value, OBJ_ARRAY)
#define IS_BOUND_METHOD(value)  isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)         isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value)       isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)      isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value)      isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)        isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)        isObjType(value, OBJ_STRING)

#define AS_VEC3(value)          ((ObjVec3*)AS_OBJ(value))
#define AS_FOREIGN(value)       ((ObjForeign*)AS_OBJ(value))
#define AS_REGEX(value)         ((ObjRegex*)AS_OBJ(value))
#define AS_MAP(value)           ((ObjMap*)AS_OBJ(value))
#define AS_ARRAY(value)         ((ObjArray*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)       ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value) \
    (((ObjNative*)AS_OBJ(value))->function)
#define AS_NATIVE_OBJ(value)    ((ObjNative*)AS_OBJ(value))
#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE,
    OBJ_ARRAY,
    OBJ_MAP,
    OBJ_FOREIGN,
    OBJ_REGEX,
    OBJ_VEC3   
} ObjType;

struct Obj {
    ObjType type;
    bool isMarked;
    struct Obj* next;
};

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function;
    ObjString* name;
    void* foreignData;
} ObjNative;

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct ObjInstance ObjInstance;
typedef struct ObjClass ObjClass;

typedef Value (*ClassCallFn)(int argCount, Value* args);
typedef Value (*ForeignGetFn)(ObjInstance* instance, ObjString* name);
typedef bool (*ForeignSetFn)(ObjInstance* instance, ObjString* name, Value value);
typedef void (*DestructorFn)(ObjInstance* instance);

typedef struct ObjClass {
    Obj obj;
    ObjString* name;
    Table methods;

    void* foreignData;
    ClassCallFn callHandler;
    ForeignGetFn getter;
    ForeignSetFn setter;
    DestructorFn destructor;
} ObjClass;

typedef struct ObjInstance {
    Obj obj;
    ObjClass* klass;
    void* foreignPtr;
    //bool isBoxed;
    Table fields;
} ObjInstance;

typedef struct {
    Obj obj;
    Value receiver;
    ObjClosure* method;
} ObjBoundMethod;

typedef struct {
    Obj obj;
    int count;
    int capacity;
    Value* values;
} ObjArray;

typedef struct {
    Obj obj;
    Table items;
} ObjMap;

typedef struct {
    Obj obj;
    pcre2_code* code;
    ObjString* pattern;
} ObjRegex;

typedef struct {
    Obj obj;
    void* ptr;
    const char* name;
} ObjForeign;

typedef struct {
    Obj obj;
    double x, y, z;
} ObjVec3;

ObjVec3* newVec3(Value x, Value y, Value z);
ObjForeign* newForeign(void* ptr, const char* name);
ObjRegex* newRegex(pcre2_code* code, ObjString* pattern);
ObjMap* newMap();
void arrayAppend(ObjArray* array, Value value);
ObjArray* newArray();
ObjBoundMethod* newBoundMethod(Value receiver,
        ObjClosure* method);
ObjClass* newClass(ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjInstance* newInstance(ObjClass* klass);
ObjNative* newNative(NativeFn function);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjUpvalue* newUpvalue(Value* slot);
ObjArray* duplicateArray(ObjArray* original);
void printArray(ObjArray *array);
void printObject(Value value);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
