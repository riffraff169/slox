#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;

    object->next = vm.objects;
    vm.objects = object;

#ifdef DEBUG_LOG_GC
    printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif

    return object;
}

ObjBoundMethod* newBoundMethod(Value receiver, Value method) {
    ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod,
            OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass* newClass(ObjString* name) {
    ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
    klass->name = name;
    klass->superclass = vm.objectClass;
    initTable(&klass->methods);

    klass->foreignData = NULL;
    klass->callHandler = NULL;
    klass->getter = NULL;
    klass->setter = NULL;
    klass->destructor = NULL;

    return klass;
}

ObjClosure* newClosure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*,
            function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjFunction* newFunction() {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    initChunk(&function->chunk);
    return function;
}

ObjInstance* newInstance(ObjClass* klass) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->obj.klass = klass;
    initTable(&instance->fields);
    instance->foreignPtr = NULL;
    return instance;
}

ObjNative* newNative(NativeFn function) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    native->foreignData = NULL;
    return native;
}

static ObjString* allocateString(char* chars, int length,
        uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    pop();

    return string;
}

static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length,
            hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length,
            hash);
    if (interned != NULL) return interned;

    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

ObjMap* newMap() {
    ObjMap* map = ALLOCATE_OBJ(ObjMap, OBJ_MAP);

    initTable(&map->items);

    return map;
}

/*
ObjVec3* newVec3(Value x, Value y, Value z) {
    ObjVec3* vec3 = ALLOCATE_OBJ(ObjVec3, OBJ_VEC3);

    //vec3->instance.klass = vm.vec3Class;
    //initTable(&vec3->instance.fields);

    vec3->x = AS_NUMBER(x);
    vec3->y = AS_NUMBER(y);
    vec3->z = AS_NUMBER(z);

    return vec3;
}
*/

ObjArray* newArray() {
    ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);

    array->count = 0;
    array->capacity = 0;
    array->values = NULL;
    array->obj.klass = vm.arrayClass;

    // init deferred until later for gc reasons
    return array;
}

void arrayAppend(ObjArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

ObjArray* duplicateArray(ObjArray* original) {
    ObjArray* copy = newArray();
    if (original->count > 0) {
        Value* entries = ALLOCATE(Value, original->count);
        copy->values = entries;
        copy->capacity = original->count;
        copy->count = original->count;
    }

    memcpy(copy->values, original->values, sizeof(Value) * original->count);

    return copy;
}

static ObjRegex* allocateRegex(pcre2_code* code, ObjString* pattern) {
    ObjRegex* re = ALLOCATE_OBJ(ObjRegex, OBJ_REGEX);
    re->code = code;
    re->pattern = pattern;
    return re;
}

ObjRegex* newRegex(pcre2_code* code, ObjString* pattern) {
    return allocateRegex(code, pattern);
}

ObjForeign* newForeign(void* ptr, const char* name) {
    ObjForeign* foreign = ALLOCATE_OBJ(ObjForeign, OBJ_FOREIGN);
    foreign->ptr = ptr;
    foreign->name = name;
    return foreign;
}

static void printFunction(ObjFunction* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void printArray(ObjArray* array) {
    printf("[");
    for (int i = 0; i < array->count; i++) {
        printValue(array->values[i]);
        if (i < array->count - 1) printf(", ");
    }
    printf("]");
}

void printMap(ObjMap* map) {
    printf("{");
    bool first = true;

    for (int i = 0; i < map->items.capacity; i++) {
        Entry* entry = &map->items.entries[i];
        if (entry->key == NULL) continue;

        if (!first) printf(", ");

        printf("\"%s\": ", entry->key->chars);
        printValueSafe(entry->value);
        first = false;
    }
    printf("}\n");
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_FOREIGN:
            printf("<foreign %s at %p>", AS_FOREIGN(value)->name, AS_FOREIGN(value)->ptr);
            break;
        case OBJ_MAP:
            printMap(AS_MAP(value));
            break;
        case OBJ_ARRAY:
            printArray(AS_ARRAY(value));
            break;
        case OBJ_BOUND_METHOD:
            //printFunction(AS_BOUND_METHOD(value)->method->function);
            printValue(AS_BOUND_METHOD(value)->method);
            break;
        case OBJ_CLASS:
            printf("%s", AS_CLASS(value)->name->chars);
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_INSTANCE:
            printf("%s instance",
                    AS_INSTANCE(value)->obj.klass->name->chars);
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
    }
}
