#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"
#include "native.h"

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
    klass->mixinsource = NULL;
    initTable(&klass->methods);
    initTable(&klass->fields);
    initTable(&klass->constants);
    initTable(&klass->getters);
    initTable(&klass->setters);

    klass->foreignData = NULL;
    klass->callHandler = NULL;
    klass->getter = NULL;
    klass->setter = NULL;
    klass->destructor = NULL;
    klass->vGetter = NIL_VAL;
    klass->vSetter = NIL_VAL;
    klass->isFrozen = false;

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
    function->minArity = 0;
    function->isVariadic = false;
    function->upvalueCount = 0;
    function->name = NULL;
    function->filename = NULL;
    function->obj.klass = vm.functionClass;
    function->isfree = true;
    initChunk(&function->chunk);

    initValueArray(&function->defaults);
    return function;
}

ObjInstance* newInstance(ObjClass* klass) {
    ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
    instance->obj.klass = klass;
    initTable(&instance->fields);
    instance->foreignPtr = NULL;
    instance->isFrozen = false;
    return instance;
}

ObjNative* newNative(NativeFn function) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    native->foreignData = NULL;
    native->obj.klass = vm.nativeFunctionClass;
    return native;
}

static ObjString* allocateString(char* chars, int length,
        uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    string->obj.klass = vm.stringClass;

    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    pop();

    return string;
}

uint32_t hashBytes(const uint8_t* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static uint32_t hashString(const char* key, int length) {
    return hashBytes((const uint8_t*)key, length);
}

uint32_t hashValue(Value value) {
    switch (value.type) {
        case VAL_NUMBER:
            {
                double n = AS_NUMBER(value);
                return hashBytes((const uint8_t*)&n, sizeof(double));
            }
        case VAL_OBJ:
            {
                if (IS_STRING(value)) {
                    return AS_STRING(value)->hash;
                }
                Obj* obj = AS_OBJ(value);
                return hashBytes((const uint8_t*)&obj, sizeof(void*));
            }
        case VAL_VEC3:
            {
                Vec3 v = AS_VEC3(value);
                return hashBytes((const uint8_t*)&v, sizeof(Vec3));
            }
        case VAL_BOOL:
            return AS_BOOL(value) ? 1 : 2;
        case VAL_NIL:
            return 3;
        default:
#ifdef DEBUG
            fprintf(stderr, "Missing hashValue case for type %d!\n", value.type);
            abort();
            //return hashBytes((const uint8_t*)&value, sizeof(Value));
#endif
            return 0;
    }
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
    ((Obj*)map)->klass = vm.mapClass;

    initTable2(&map->items);

    return map;
}

ObjSet* newSet() {
    ObjSet* set = ALLOCATE_OBJ(ObjSet, OBJ_SET);
    ((Obj*)set)->klass = vm.setClass;
    set->isMultiset = false;

    initTable2(&set->items);

    return set;
}

bool mapGet(ObjMap* map, ObjString* key, Value* value) {
    return tableGet2(&map->items, OBJ_VAL(key), value);
}

bool mapSet(ObjMap* map, ObjString* key, Value value) {
    return tableSet2(&map->items, OBJ_VAL(key), value);
}

bool mapSetByCStr(ObjMap* map, const char* cstr, Value value) {
    ObjString* key = copyString(cstr, (int)strlen(cstr));
    push(OBJ_VAL(key));

    bool res = mapSet(map, key, value);

    pop();
    return res;
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
    ((Obj*)array)->klass = vm.arrayClass;

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
    push(OBJ_VAL(copy));

    if (original->count > 0) {
        Value* entries = ALLOCATE(Value, original->count);
        copy->values = entries;
        copy->capacity = original->count;
        copy->count = original->count;
        memcpy(copy->values, original->values, sizeof(Value) * original->count);
    }


    return AS_ARRAY(pop());
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
        Entry2* entry = &map->items.entries[i];
        if (IS_NIL(entry->key)) continue;

        if (!first) printf(", ");

        /*
        char *keystr = valueToCString(entry->key);
        printf("\"%s\": ", keystr);
        free(keystr);
        */
        printValueSafe(entry->key);
        printf(": ");
        printValueSafe(entry->value);
        first = false;
    }
    printf("}\n");
}

void printSet(ObjSet* set) {
    printf("Set(");
    bool first = true;
    for (int i = 0; i < set->items.capacity; i++) {
        Entry2* entry = &set->items.entries[i];
        if (IS_NIL(entry->key)) continue;

        if (!first) printf(", ");
        printValueSafe(entry->key);
        if (set->isMultiset) {
            printf(": ");
            printValueSafe(entry->value);
        }
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
        case OBJ_SET:
            printSet(AS_SET(value));
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
