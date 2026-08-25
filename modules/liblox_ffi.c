#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <ffi.h>
#include "vm.h"

typedef struct {
    void* handle;
} SloxFFILib;

typedef struct {
    void* fnPtr;
    ffi_cif cif;
    ffi_type* rtype;
    ffi_type** argTypes;
    int argCount;
    char* rtypeName;
    char** argTypeNames;
} SloxFFIFunc;

static ObjClass* gFFIFuncClass = NULL;

static ffi_type* parseFFIType(const char* typeStr) {
    if (strcmp(typeStr, "void") == 0) return &ffi_type_void;
    if (strcmp(typeStr, "int") == 0 ||
            strcmp(typeStr, "int32") == 0 ||
            strcmp(typeStr, "int32_t") == 0)
        return &ffi_type_sint32;
    if (strcmp(typeStr, "uint") == 0 ||
            strcmp(typeStr, "uint32") == 0 ||
            strcmp(typeStr, "uint32_t") == 0)
        return &ffi_type_uint32;
    if (strcmp(typeStr, "double") == 0 || strcmp(typeStr, "number") == 0) return &ffi_type_double;
    if (strcmp(typeStr, "float") == 0) return &ffi_type_float;
    if (strcmp(typeStr, "string") == 0) return &ffi_type_pointer;
    if (strcmp(typeStr, "pointer") == 0) return &ffi_type_pointer;
    if (strcmp(typeStr, "bool") == 0) return &ffi_type_uint8;
    return NULL;
}

// memory cleanup destructors
void ffiLibDestructor(ObjInstance* inst) {
    if (inst->foreignPtr) {
        SloxFFILib* lib = (SloxFFILib*)inst->foreignPtr;
        if (lib->handle) dlclose(lib->handle);
        free(lib);
        inst->foreignPtr = NULL;
    }
}

void ffiFuncDestructor(ObjInstance* inst) {
    if (inst->foreignPtr) {
        SloxFFIFunc* fn = (SloxFFIFunc*)inst->foreignPtr;
        if (fn->argTypes) free(fn->argTypes);
        if (fn->rtypeName) free(fn->rtypeName);
        if (fn->argTypeNames) {
            for (int i = 0; i < fn->argCount; i++) {
                if (fn->argTypeNames[i]) free(fn->argTypeNames[i]);
            }
            free(fn->argTypeNames);
        }
        free(fn);
        inst->foreignPtr = NULL;
    }
}

static void freeFFIFuncDestructor(ObjNative* native) {
    if (native->foreignData != NULL) {
        SloxFFIFunc* fn = (SloxFFIFunc*)native->foreignData;

        if (fn->argTypes) free(fn->argTypes);
        if (fn->argTypeNames) {
            for (int i = 0; i < fn->argCount; i++) {
                free(fn->argTypeNames[i]);
            }
            free(fn->argTypeNames);
        }
        if (fn->rtypeName) free(fn->rtypeName);

        free(fn);
        native->foreignData = NULL;
    }
}

// core marshalling engine
static Value executeFFICall(SloxFFIFunc* fn, int argCount, Value* args) {
    if (argCount != fn->argCount) {
        runtimeError("FFI call expected %d arguments, got %d.", fn->argCount, argCount);
        return NIL_VAL;
    }

    void** ffiArgs = NULL;
    void** valueAllocations = NULL;

    if (fn->argCount > 0) {
        ffiArgs = (void**)malloc(sizeof(void*) * fn->argCount);
        valueAllocations = (void**)malloc(sizeof(void*) * fn->argCount);
    }

    for (int i = 0; i < fn->argCount; i++) {
        Value val = args[i];
        const char* tname = fn->argTypeNames[i];

        if (strcmp(tname, "int") == 0 || strcmp(tname, "int32") == 0) {
            int32_t* ptr = (int32_t*)malloc(sizeof(int32_t));
            *ptr = IS_NUMBER(val) ? (int32_t)AS_NUMBER(val) : 0;
            valueAllocations[i] = ptr;
            ffiArgs[i] = ptr;
        } else if (strcmp(tname, "double") == 0 || strcmp(tname, "number") == 0) {
            double* ptr = (double*)malloc(sizeof(double));
            *ptr = IS_NUMBER(val) ? AS_NUMBER(val) : 0.0;
            valueAllocations[i] = ptr;
            ffiArgs[i] = ptr;
        } else if (strcmp(tname, "string") == 0) {
            const char** ptr = (const char**)malloc(sizeof(char*));
            if (IS_BUFFER(val)) {
                *ptr = (const char*)AS_BUFFER(val)->bytes;
            } else if (IS_STRING(val)) {
                *ptr = AS_CSTRING(val);
            } else {
                *ptr = NULL;
            }
            valueAllocations[i] = ptr;
            ffiArgs[i] = ptr;
        } else if (strcmp(tname, "pointer") == 0) {
            void** ptr = (void**)malloc(sizeof(void*));
            if (IS_NIL(val)) {
                *ptr = NULL;
            } else if (IS_BUFFER(val)) {
                *ptr = (void*)AS_BUFFER(val)->bytes;
            } else if (IS_NUMBER(val)) {
                *ptr = (void*)(uintptr_t)AS_NUMBER(val);
            } else if (IS_INSTANCE(val) && AS_INSTANCE(val)->foreignPtr) {
                *ptr = AS_INSTANCE(val)->foreignPtr;
            } else {
                *ptr = NULL;
            }
            valueAllocations[i] = ptr;
            ffiArgs[i] = ptr;
        } else if (strcmp(tname, "bool") == 0) {
            uint8_t* ptr = (uint8_t*)malloc(sizeof(uint8_t));
            *ptr = IS_BOOL(val) ? (AS_BOOL(val) ? 1 : 0) : 0;
            valueAllocations[i] = ptr;
            ffiArgs[i] = ptr;
        }
    }

    /*
    printf("[FFI DEBUG] Invoking function at %p (%d args, return: %s)\n",
            fn->fnPtr, fn->argCount, fn->rtypeName);

    for (int i = 0; i < fn->argCount; i++) {
        void* derefVal = *(void**)ffiArgs[i];
        printf("  arg[%d] handle=%p -> value=%p", i, ffiArgs[i], derefVal);

        if (derefVal != NULL) {
            uint8_t* bytes = (uint8_t*)derefVal;
            printf(" | bytes [0..39]:\n    ");
            for (int b = 0; b < 40; b++) {
                printf("%02x ", bytes[b]);
                if ((b + 1) % 8 == 0 && b < 39) printf("| ");
            }
        }
        printf("\n");
    }
    printf("---------------------------------------------------\n");
    */

    Value result = NIL_VAL;

    if (strcmp(fn->rtypeName, "void") == 0) {
        ffi_call(&fn->cif, FFI_FN(fn->fnPtr), NULL, ffiArgs);
    } else if (strcmp(fn->rtypeName, "int") == 0 || strcmp(fn->rtypeName, "int32") == 0) {
        int32_t resVal = 0;
        ffi_call(&fn->cif, FFI_FN(fn->fnPtr), &resVal, ffiArgs);
        result = NUMBER_VAL((double)resVal);
    } else if (strcmp(fn->rtypeName, "double") == 0 || strcmp(fn->rtypeName, "number") == 0) {
        double resVal = 0.0;
        ffi_call(&fn->cif, FFI_FN(fn->fnPtr), &resVal, ffiArgs);
        result = NUMBER_VAL(resVal);
    } else if (strcmp(fn->rtypeName, "string") == 0) {
        char* resVal = NULL;
        ffi_call(&fn->cif, FFI_FN(fn->fnPtr), &resVal, ffiArgs);
        if (resVal) {
            ObjString* str = copyString(resVal, (int)strlen(resVal));
            result = OBJ_VAL(str);
        }
    } else if (strcmp(fn->rtypeName, "pointer") == 0) {
        void* resVal = NULL;
        ffi_call(&fn->cif, FFI_FN(fn->fnPtr), &resVal, ffiArgs);
        result = NUMBER_VAL((double)(uintptr_t)resVal);
    } else if (strcmp(fn->rtypeName, "bool") == 0) {
        uint8_t resVal = 0;
        ffi_call(&fn->cif, FFI_FN(fn->fnPtr), &resVal, ffiArgs);
        result = BOOL_VAL(resVal != 0);
    }

    for (int i = 0; i < fn->argCount; i++) {
        free(valueAllocations[i]);
    }
    if (valueAllocations) free(valueAllocations);
    if (ffiArgs) free(ffiArgs);

    return result;
}

// FFIFunction call handler and method
Value ffiFuncCallHandler(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[-1]);
    SloxFFIFunc* fn = (SloxFFIFunc*)inst->foreignPtr;
    if (!fn || !fn->fnPtr) {
        runtimeError("Cannot invoke uninitialized FFI function.");
        return NIL_VAL;
    }
    return executeFFICall(fn, argCount, args);
}

Value ffiFuncCallMethod(int argCount, Value* args) {
    return ffiFuncCallHandler(argCount, args);
}

// ffi constructor: var lib = FFI("libname.so");
Value ffiCallHandler(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("FFI requires library name string.");
        return NIL_VAL;
    }

    const char* libPath = AS_CSTRING(args[0]);
    void* handle = dlopen(libPath, RTLD_LAZY | RTLD_GLOBAL);

    if (!handle) {
        runtimeError("Failed to load dynamic library '%s': %s", libPath, dlerror());
        return NIL_VAL;
    }

    SloxFFILib* lib = (SloxFFILib*)malloc(sizeof(SloxFFILib));
    lib->handle = handle;

    ObjClass* klass = AS_CLASS(args[-1]);
    ObjInstance* inst = newInstance(klass);
    inst->foreignPtr = (void*)lib;
    return OBJ_VAL(inst);
}

Value ffiNativeCallHandler(int argCount, Value* args) {
    ObjNative* nativeFn = (ObjNative*)AS_OBJ(args[-1]);
    SloxFFIFunc* fn = (SloxFFIFunc*)nativeFn->foreignData;

    if (!fn || !fn->fnPtr) {
        runtimeError("Cannot invoke uninitialized FFI function.");
        return NIL_VAL;
    }

    return executeFFICall(fn, argCount, args);
}

// ffi method: lib.bind("symbol_name", "return_type", ["arg_type1", ...])
Value ffiBindMethod(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[-1]);
    SloxFFILib* lib = (SloxFFILib*)inst->foreignPtr;

    if (!lib || !lib->handle) {
        runtimeError("Cannot bind symbol on closed FFI library handle.");
        return NIL_VAL;
    }

    if (argCount < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("bind() requires symbol name and returntype strings.");
        return NIL_VAL;
    }

    const char* symName = AS_CSTRING(args[0]);
    const char* rtypeStr = AS_CSTRING(args[1]);

    void* fnPtr = dlsym(lib->handle, symName);

    if (!fnPtr) {
        fnPtr = dlsym(RTLD_DEFAULT, symName);
    }

    if (!fnPtr) {
        runtimeError("Symbol '%s' not found: %s", symName, dlerror());
        return NIL_VAL;
    }

    ffi_type* rtype = parseFFIType(rtypeStr);
    if (!rtype) {
        runtimeError("Unknown return type '%s'.", rtypeStr);
        return NIL_VAL;
    }

    int numArgs = 0;
    ffi_type** argTypes = NULL;
    char** argTypeNames = NULL;
    if (argCount > 2 && IS_ARRAY(args[2])) {
        ObjArray* arr = AS_ARRAY(args[2]);
        numArgs = arr->count;
        if (numArgs > 0) {
            argTypes = (ffi_type**)malloc(sizeof(ffi_type*) * numArgs);
            argTypeNames = (char**)malloc(sizeof(char*) * numArgs);

            for (int i = 0; i < numArgs; i++) {
                if (!IS_STRING(arr->values[i])) {
                    runtimeError("Argument types must be strings.");
                    free(argTypes);
                    free(argTypeNames);
                    return NIL_VAL;
                }
                const char* typeName = AS_CSTRING(arr->values[i]);
                argTypes[i] = parseFFIType(typeName);
                argTypeNames[i] = strdup(typeName);

                if (!argTypes[i]) {
                    runtimeError("Unknown argument type '%s'.", typeName);
                    return NIL_VAL;
                }
            }
        }
    }

    SloxFFIFunc* fn = (SloxFFIFunc*)malloc(sizeof(SloxFFIFunc));
    fn->fnPtr = fnPtr;
    fn->rtype = rtype;
    fn->argTypes = argTypes;
    fn->argCount = numArgs;
    fn->rtypeName = strdup(rtypeStr);
    fn->argTypeNames = argTypeNames;

    if (ffi_prep_cif(&fn->cif, FFI_DEFAULT_ABI, numArgs, rtype, argTypes) != FFI_OK) {
        runtimeError("ffi_prep_cif failed for symbol '%s'.", symName);
        return NIL_VAL;
    }

    ObjNative* nativeFn = newNative(ffiNativeCallHandler);
    nativeFn->foreignData = (void*)fn;
    nativeFn->destructor = freeFFIFuncDestructor;
    return OBJ_VAL(nativeFn);
}

void lox_module_init(VM* vm) {
    // 1. create public ffi library class
    ObjString* ffiClassName = copyString("FFI", 3);
    push(OBJ_VAL(ffiClassName));
    ObjClass* ffiClass = newClass(ffiClassName);
    ffiClass->superclass = vm->objectClass;
    ffiClass->callHandler = ffiCallHandler;
    ffiClass->destructor = ffiLibDestructor;
    push(OBJ_VAL(ffiClass));
    tableSet(&vm->globals, ffiClassName, OBJ_VAL(ffiClass));

    ObjNative* bindFn = newNative(ffiBindMethod);
    push(OBJ_VAL(bindFn));
    ObjString* bindstr = copyString("bind", 4);
    push(OBJ_VAL(bindstr));
    tableSet(&ffiClass->methods, bindstr, OBJ_VAL(bindFn));
    //defineNativeMethod(ffiClass, "bind", ffiBindMethod);

    pop();
    pop();
    pop();
    pop();
}
