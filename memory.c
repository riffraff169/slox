#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) {
        if (vm.stress_mode == 1) { // run always
            collectGarbage();
        }

#ifndef DEBUG_DISABLE_GC
        if (!vm.isGC && vm.bytesAllocated > vm.nextGC && vm.init_threshold < vm.bytesAllocated) {
            if (vm.stress_mode != 2) // run never
                collectGarbage();
        }
#endif
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

void markObject(Obj* object) {
    if (object == NULL) return;
    if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->isMarked = true;

    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        vm.grayStack = (Obj**)realloc(vm.grayStack,
                sizeof(Obj*) * vm.grayCapacity);

        if (vm.grayStack == NULL) exit(1);
    }

    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_REGEX:
            {
                ObjRegex* re = (ObjRegex*)object;
                markObject((Obj*)re->pattern);
            }
            break;
        case OBJ_MAP:
            {
                ObjMap* map = (ObjMap*)object;
                markTable(&map->items);
            }
            break;
        case OBJ_ARRAY:
            {
                ObjArray* array = (ObjArray*)object;
                if (array->values == NULL) break;
                for (int i = 0; i < array->count; i++) {
                    markValue(array->values[i]);
                }
            }
            break;
        case OBJ_BOUND_METHOD:
            {
                ObjBoundMethod* bound = (ObjBoundMethod*)object;
                markValue(bound->receiver);
                markObject((Obj*)bound->method);
            }
            break;
        case OBJ_CLASS:
            {
                ObjClass* klass = (ObjClass*)object;
                markObject((Obj*)klass->name);
                markTable(&klass->methods);
            }
            break;
        case OBJ_CLOSURE:
            {
                ObjClosure* closure = (ObjClosure*)object;
                markObject((Obj*)closure->function);
                for (int i = 0; i < closure->upvalueCount; i++) {
                    markObject((Obj*)closure->upvalues[i]);
                }
            }
            break;
        case OBJ_FUNCTION:
            {
                ObjFunction* function = (ObjFunction*)object;
                markObject((Obj*)function->name);
                markArray(&function->chunk.constants);
            }
            break;
        case OBJ_INSTANCE:
            {
                ObjInstance* instance = (ObjInstance*)object;
                markObject((Obj*)instance->klass);
                markTable(&instance->fields);
            }
            break;
        case OBJ_UPVALUE:
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch (object->type) {
        case OBJ_FOREIGN:
            {
                FREE(ObjForeign, object);
            }
            break;
        case OBJ_REGEX:
            {
                ObjRegex* re = (ObjRegex*)object;
                pcre2_code_free(re->code);
                FREE(ObjRegex, object);
            }
            break;
        case OBJ_MAP:
            {
                ObjMap* map = (ObjMap*)object;
                freeTable(&map->items);
                FREE(ObjMap, object);
            }
            break;
        case OBJ_ARRAY:
            {
                ObjArray* array = (ObjArray*)object;
                FREE_ARRAY(Value, array->values, array->capacity);
                FREE(ObjArray, object);
            }
            break;
        case OBJ_BOUND_METHOD:
            FREE(ObjBoundMethod, object);
            break;
        case OBJ_CLASS:
            {
                ObjClass* klass = (ObjClass*)object;
                freeTable(&klass->methods);
                FREE(ObjClass, object);
            }
            break;
        case OBJ_CLOSURE:
            {
                ObjClosure* closure = (ObjClosure*)object;
                FREE_ARRAY(ObjUpvalue*, closure->upvalues,
                        closure->upvalueCount);
                FREE(ObjClosure, object);
            }
            break;
        case OBJ_FUNCTION:
            {
                ObjFunction* function = (ObjFunction*)object;
                freeChunk(&function->chunk);
                FREE(ObjFunction, object);
            }
            break;
        case OBJ_INSTANCE:
            {
                ObjInstance* instance = (ObjInstance*)object;
                if (instance->klass != NULL && instance->klass->destructor != NULL) {
                    instance->klass->destructor(instance);
                }

                freeTable(&instance->fields);
                FREE(ObjInstance, object);
            }
            break;
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
        case OBJ_STRING:
            {
                //printf("Freeing string: %s\n", ((ObjString*)object)->chars);
                ObjString* string = (ObjString*)object;
                FREE_ARRAY(char, string->chars, string->length + 1);
                FREE(ObjString, object);
            }
            break;
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
    }
}

static void markRoots() {
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }

    for (ObjUpvalue* upvalue = vm.openUpvalues;
            upvalue != NULL;
            upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    markTable(&vm.globals);
    markCompilerRoots();
    markObject((Obj*)vm.initString);
    markObject((Obj*)vm.toString);
    markObject((Obj*)vm.str_add);
    markObject((Obj*)vm.str_sub);
    markObject((Obj*)vm.str_mul);
    markObject((Obj*)vm.str_div);
    markObject((Obj*)vm.str_neg);
    markObject((Obj*)vm.xString);
    markObject((Obj*)vm.yString);
    markObject((Obj*)vm.zString);
    markObject((Obj*)vm.arrayClass);
    markObject((Obj*)vm.mapClass);
    markObject((Obj*)vm.stringClass);
    markObject((Obj*)vm.moduleClass);
    markObject((Obj*)vm.regexClass);
    markObject((Obj*)vm.mathClass);
    //markObject((Obj*)vm.vec3Class);
    markObject((Obj*)vm.gcClass);
}

static void traceReferences() {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep() {
    Obj* previous = NULL;
    Obj* object = vm.objects;
    while (object != NULL) {
        if (object->isMarked) {
            object->isMarked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) {
                previous->next = object;
            } else {
                vm.objects = object;
            }

            freeObject(unreached);
        }
    }
}

void collectGarbage() {
    vm.isGC = true;
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
    size_t before = vm.bytesAllocated;
#endif

    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings);
    /*
    printf("[GC]: strings table capacity: %d\n", vm.strings.capacity);
    printf("[GC]: strings table count: %d\n", vm.strings.count);
    printf("[GC]: strings table size: %d\n", sizeof(vm.strings.entries) * vm.strings.capacity);
    */
    sweep();

    if (vm.gctype == 1) {
        vm.nextGC = vm.bytesAllocated * vm.heap_growth_factor;
    } else if (vm.gctype == 0) {
        vm.nextGC = vm.bytesAllocated + vm.bump_size;
    }

    /*
    malloc_trim(0);
    int count = 0;
    Obj* object = vm.objects;
    while (object != NULL) {
        count++;
        object = object->next;
    }
    printf("[GC] Total Objects remaining in heap: %d\n", count);
    */

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %z\n",
            before - vm.bytesAllocated, before, vm.bytesAllocated,
            vm.nextGC);
#endif
    vm.isGC = false;
}

void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;

        freeObject(object);

        object = next;
    }

    free(vm.grayStack);
}
