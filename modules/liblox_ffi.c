#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <dlfcn.h>
#include <ffi.h>
#include "vm.h"

typedef struct {
    void* handle;
    int refs;
} SloxFFILib;

typedef enum {
    FFI_KIND_VOID,
    FFI_KIND_SINT8,
    FFI_KIND_UINT8,
    FFI_KIND_SINT16,
    FFI_KIND_UINT16,
    FFI_KIND_SINT32,
    FFI_KIND_UINT32,
    FFI_KIND_SINT64,
    FFI_KIND_UINT64,
    FFI_KIND_FLOAT,
    FFI_KIND_DOUBLE,
    FFI_KIND_BOOL,
    FFI_KIND_STRING,
    FFI_KIND_BUFFER,
    FFI_KIND_POINTER
} FFIKind;

typedef struct {
    char* name;
    void* fnPtr;
    ffi_cif cif;
    ffi_type* rtype;
    FFIKind rtypeKind;
    ffi_type** argTypes;
    FFIKind* argKinds;
    int argCount;
    char* rtypeName;
    char** argTypeNames;
    SloxFFILib* owner;
} SloxFFIFunc;

typedef union {
    ffi_arg arg;
    int8_t i8;
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    float f;
    double d;
    void* p;
} FFIValueStorage;

static void releaseFFILib(SloxFFILib* lib) {
    if (lib == NULL) return;

    lib->refs--;
    if (lib->refs > 0) return;

    if (lib->handle != NULL) dlclose(lib->handle);
    free(lib);
}

static FFIKind signedKindForSize(size_t size) {
    if (size <= sizeof(int8_t)) return FFI_KIND_SINT8;
    if (size <= sizeof(int16_t)) return FFI_KIND_SINT16;
    if (size <= sizeof(int32_t)) return FFI_KIND_SINT32;
    return FFI_KIND_SINT64;
}

static FFIKind unsignedKindForSize(size_t size) {
    if (size <= sizeof(uint8_t)) return FFI_KIND_UINT8;
    if (size <= sizeof(uint16_t)) return FFI_KIND_UINT16;
    if (size <= sizeof(uint32_t)) return FFI_KIND_UINT32;
    return FFI_KIND_UINT64;
}

static ffi_type* unsignedTypeForSize(size_t size) {
    if (size <= sizeof(uint8_t)) return &ffi_type_uint8;
    if (size <= sizeof(uint16_t)) return &ffi_type_uint16;
    if (size <= sizeof(uint32_t)) return &ffi_type_uint32;
    return &ffi_type_uint64;
}

// Supported type names:
//   void
//   int8/uint8, int16/uint16, int32/uint32, int64/uint64 (and *_t forms)
//   int/uint, short/ushort, long/ulong, size_t, ssize_t, intptr/uintptr
//   float, double/number, bool/boolean
//   string/cstring/char*  immutable NUL-terminated Lox String bytes
//   buffer                writable Buffer bytes (arguments only)
//   pointer/ptr/void*     raw pointer: Buffer, integer address, instance, or
//                         pointer-sized bytes produced by Buffer.get_ptr()
//
// Native C types ("int", "long", "size_t", ...) keep their actual platform
// width. Do not assume they are 64-bit.
static bool parseFFIType(const char* typeStr, ffi_type** outType, FFIKind* outKind) {
    if (strcmp(typeStr, "void") == 0) {
        *outType = &ffi_type_void;
        *outKind = FFI_KIND_VOID;
        return true;
    }

    // Fixed-width integer types.
    if (strcmp(typeStr, "int8") == 0 || strcmp(typeStr, "int8_t") == 0) {
        *outType = &ffi_type_sint8;
        *outKind = FFI_KIND_SINT8;
        return true;
    }
    if (strcmp(typeStr, "uint8") == 0 || strcmp(typeStr, "uint8_t") == 0 ||
            strcmp(typeStr, "byte") == 0) {
        *outType = &ffi_type_uint8;
        *outKind = FFI_KIND_UINT8;
        return true;
    }
    if (strcmp(typeStr, "int16") == 0 || strcmp(typeStr, "int16_t") == 0) {
        *outType = &ffi_type_sint16;
        *outKind = FFI_KIND_SINT16;
        return true;
    }
    if (strcmp(typeStr, "uint16") == 0 || strcmp(typeStr, "uint16_t") == 0) {
        *outType = &ffi_type_uint16;
        *outKind = FFI_KIND_UINT16;
        return true;
    }
    if (strcmp(typeStr, "int32") == 0 || strcmp(typeStr, "int32_t") == 0) {
        *outType = &ffi_type_sint32;
        *outKind = FFI_KIND_SINT32;
        return true;
    }
    if (strcmp(typeStr, "uint32") == 0 || strcmp(typeStr, "uint32_t") == 0) {
        *outType = &ffi_type_uint32;
        *outKind = FFI_KIND_UINT32;
        return true;
    }
    if (strcmp(typeStr, "int64") == 0 || strcmp(typeStr, "int64_t") == 0 ||
            strcmp(typeStr, "long long") == 0 || strcmp(typeStr, "longlong") == 0 ||
            strcmp(typeStr, "llong") == 0) {
        *outType = &ffi_type_sint64;
        *outKind = FFI_KIND_SINT64;
        return true;
    }
    if (strcmp(typeStr, "uint64") == 0 || strcmp(typeStr, "uint64_t") == 0 ||
            strcmp(typeStr, "unsigned long long") == 0 ||
            strcmp(typeStr, "ulonglong") == 0 || strcmp(typeStr, "ullong") == 0) {
        *outType = &ffi_type_uint64;
        *outKind = FFI_KIND_UINT64;
        return true;
    }

    // Native C types. These vary by platform, so derive their width at compile time.
    if (strcmp(typeStr, "int") == 0 || strcmp(typeStr, "sint") == 0) {
        *outType = &ffi_type_sint;
        *outKind = signedKindForSize(sizeof(int));
        return true;
    }
    if (strcmp(typeStr, "uint") == 0 || strcmp(typeStr, "unsigned int") == 0) {
        *outType = &ffi_type_uint;
        *outKind = unsignedKindForSize(sizeof(unsigned int));
        return true;
    }
    if (strcmp(typeStr, "short") == 0 || strcmp(typeStr, "sshort") == 0) {
        *outType = &ffi_type_sshort;
        *outKind = signedKindForSize(sizeof(short));
        return true;
    }
    if (strcmp(typeStr, "ushort") == 0 || strcmp(typeStr, "unsigned short") == 0) {
        *outType = &ffi_type_ushort;
        *outKind = unsignedKindForSize(sizeof(unsigned short));
        return true;
    }
    if (strcmp(typeStr, "long") == 0 || strcmp(typeStr, "slong") == 0) {
        *outType = &ffi_type_slong;
        *outKind = signedKindForSize(sizeof(long));
        return true;
    }
    if (strcmp(typeStr, "ulong") == 0 || strcmp(typeStr, "unsigned long") == 0) {
        *outType = &ffi_type_ulong;
        *outKind = unsignedKindForSize(sizeof(unsigned long));
        return true;
    }
    if (strcmp(typeStr, "size_t") == 0) {
        *outType = unsignedTypeForSize(sizeof(size_t));
        *outKind = unsignedKindForSize(sizeof(size_t));
        return true;
    }
    if (strcmp(typeStr, "ssize_t") == 0 || strcmp(typeStr, "ptrdiff_t") == 0 ||
            strcmp(typeStr, "intptr") == 0 || strcmp(typeStr, "intptr_t") == 0) {
        *outType = sizeof(intptr_t) == 8 ? &ffi_type_sint64 : &ffi_type_sint32;
        *outKind = signedKindForSize(sizeof(intptr_t));
        return true;
    }
    if (strcmp(typeStr, "uintptr") == 0 || strcmp(typeStr, "uintptr_t") == 0) {
        *outType = unsignedTypeForSize(sizeof(uintptr_t));
        *outKind = unsignedKindForSize(sizeof(uintptr_t));
        return true;
    }

    if (strcmp(typeStr, "double") == 0 || strcmp(typeStr, "number") == 0) {
        *outType = &ffi_type_double;
        *outKind = FFI_KIND_DOUBLE;
        return true;
    }
    if (strcmp(typeStr, "float") == 0) {
        *outType = &ffi_type_float;
        *outKind = FFI_KIND_FLOAT;
        return true;
    }
    if (strcmp(typeStr, "string") == 0 || strcmp(typeStr, "cstring") == 0 ||
            strcmp(typeStr, "char*") == 0) {
        *outType = &ffi_type_pointer;
        *outKind = FFI_KIND_STRING;
        return true;
    }
    // A Buffer is not a String: it is writable memory that can safely be
    // passed to C functions which write into the pointer.
    if (strcmp(typeStr, "buffer") == 0) {
        *outType = &ffi_type_pointer;
        *outKind = FFI_KIND_BUFFER;
        return true;
    }
    if (strcmp(typeStr, "pointer") == 0 || strcmp(typeStr, "ptr") == 0 ||
            strcmp(typeStr, "void*") == 0) {
        *outType = &ffi_type_pointer;
        *outKind = FFI_KIND_POINTER;
        return true;
    }
    if (strcmp(typeStr, "bool") == 0 || strcmp(typeStr, "boolean") == 0) {
        *outType = &ffi_type_uchar;
        *outKind = FFI_KIND_BOOL;
        return true;
    }

    return false;
}

static bool valueToSigned(Value val, int64_t* out) {
    if (IS_INT(val)) {
        *out = AS_INT(val);
        return true;
    }
    if (IS_FLOAT(val)) {
        double d = AS_FLOAT(val);
        if (!isfinite(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0) {
            return false;
        }
        *out = (int64_t)d;
        return true;
    }
    if (IS_BOOL(val)) {
        *out = AS_BOOL(val) ? 1 : 0;
        return true;
    }
    return false;
}

static bool valueToUnsigned(Value val, uint64_t* out) {
    if (IS_INT(val)) {
        *out = (uint64_t)AS_INT(val);
        return true;
    }
    if (IS_FLOAT(val)) {
        double d = AS_FLOAT(val);
        if (!isfinite(d) || d < 0.0 || d >= 18446744073709551616.0) {
            return false;
        }
        *out = (uint64_t)d;
        return true;
    }
    if (IS_BOOL(val)) {
        *out = AS_BOOL(val) ? 1 : 0;
        return true;
    }
    return false;
}

static bool valueToFFIDouble(Value val, double* out) {
    if (IS_FLOAT(val)) {
        *out = AS_FLOAT(val);
        return true;
    }
    if (IS_INT(val)) {
        *out = (double)AS_INT(val);
        return true;
    }
    if (IS_BOOL(val)) {
        *out = AS_BOOL(val) ? 1.0 : 0.0;
        return true;
    }
    return false;
}

static bool valueToBool(Value val, uint8_t* out) {
    if (IS_BOOL(val)) {
        *out = AS_BOOL(val) ? 1 : 0;
        return true;
    }
    if (IS_INT(val)) {
        *out = AS_INT(val) != 0 ? 1 : 0;
        return true;
    }
    if (IS_FLOAT(val)) {
        *out = AS_FLOAT(val) != 0.0 ? 1 : 0;
        return true;
    }
    return false;
}

static bool valueToStringPointer(Value val, const char** out) {
    if (IS_NIL(val)) {
        *out = NULL;
        return true;
    }
    if (IS_STRING(val)) {
        *out = AS_CSTRING(val);
        return true;
    }
    return false;
}

static bool valueToBufferPointer(Value val, void** out) {
    if (IS_NIL(val)) {
        *out = NULL;
        return true;
    }
    if (IS_BUFFER(val)) {
        *out = (void*)AS_BUFFER(val)->bytes;
        return true;
    }
    return false;
}

static bool valueToPointer(Value val, void** out) {
    if (IS_NIL(val)) {
        *out = NULL;
        return true;
    }
    if (IS_BUFFER(val)) {
        *out = (void*)AS_BUFFER(val)->bytes;
        return true;
    }
    if (IS_INT(val)) {
        *out = (void*)(uintptr_t)AS_INT(val);
        return true;
    }
    if (IS_FLOAT(val)) {
        int64_t n;
        if (!valueToSigned(val, &n)) return false;
        *out = (void*)(uintptr_t)n;
        return true;
    }
    if (IS_STRING(val)) {
        ObjString* str = AS_STRING(val);
        // Buffer.get_ptr() returns the pointer as a string of pointer-sized bytes.
        if (str->length == (int)sizeof(void*)) {
            memcpy(out, str->chars, sizeof(void*));
        } else {
            *out = (void*)str->chars;
        }
        return true;
    }
    if (IS_INSTANCE(val) && AS_INSTANCE(val)->foreignPtr != NULL) {
        *out = AS_INSTANCE(val)->foreignPtr;
        return true;
    }
    return false;
}

static void freeFFIData(SloxFFIFunc* fn) {
    if (fn == NULL) return;

    if (fn->argTypeNames != NULL) {
        for (int i = 0; i < fn->argCount; i++) {
            free(fn->argTypeNames[i]);
        }
        free(fn->argTypeNames);
    }
    free(fn->name);
    free(fn->argTypes);
    free(fn->argKinds);
    free(fn->rtypeName);
    releaseFFILib(fn->owner);
    free(fn);
}

// memory cleanup destructors
void ffiLibDestructor(ObjInstance* inst) {
    if (inst->foreignPtr != NULL) {
        SloxFFILib* lib = (SloxFFILib*)inst->foreignPtr;
        inst->foreignPtr = NULL;
        releaseFFILib(lib);
    }
}

static void freeFFIFuncDestructor(ObjNative* native) {
    if (native->foreignData != NULL) {
        SloxFFIFunc* fn = (SloxFFIFunc*)native->foreignData;
        native->foreignData = NULL;
        freeFFIData(fn);
    }
}

// core marshalling engine
static Value executeFFICall(SloxFFIFunc* fn, int argCount, Value* args) {
    if (argCount != fn->argCount) {
        runtimeError("FFI call expected %d arguments, got %d.", fn->argCount, argCount);
        return NIL_VAL;
    }

    FFIValueStorage* storage = NULL;
    void** ffiArgs = NULL;

    if (fn->argCount > 0) {
        storage = (FFIValueStorage*)calloc((size_t)fn->argCount, sizeof(FFIValueStorage));
        ffiArgs = (void**)malloc(sizeof(void*) * (size_t)fn->argCount);
        if (storage == NULL || ffiArgs == NULL) {
            free(storage);
            free(ffiArgs);
            runtimeError("FFI call could not allocate argument storage.");
            return NIL_VAL;
        }

        for (int i = 0; i < fn->argCount; i++) {
            ffiArgs[i] = &storage[i];
        }
    }

    int i = 0;
    for (; i < fn->argCount; i++) {
        Value val = args[i];
        int64_t signedValue;
        uint64_t unsignedValue;
        double doubleValue;
        uint8_t boolValue;
        const char* stringValue;
        void* pointerValue;

        switch (fn->argKinds[i]) {
            case FFI_KIND_SINT8:
                if (!valueToSigned(val, &signedValue)) goto invalid_argument;
                storage[i].i8 = (int8_t)signedValue;
                ffiArgs[i] = &storage[i].i8;
                break;
            case FFI_KIND_UINT8:
                if (!valueToUnsigned(val, &unsignedValue)) goto invalid_argument;
                storage[i].u8 = (uint8_t)unsignedValue;
                ffiArgs[i] = &storage[i].u8;
                break;
            case FFI_KIND_SINT16:
                if (!valueToSigned(val, &signedValue)) goto invalid_argument;
                storage[i].i16 = (int16_t)signedValue;
                ffiArgs[i] = &storage[i].i16;
                break;
            case FFI_KIND_UINT16:
                if (!valueToUnsigned(val, &unsignedValue)) goto invalid_argument;
                storage[i].u16 = (uint16_t)unsignedValue;
                ffiArgs[i] = &storage[i].u16;
                break;
            case FFI_KIND_SINT32:
                if (!valueToSigned(val, &signedValue)) goto invalid_argument;
                storage[i].i32 = (int32_t)signedValue;
                ffiArgs[i] = &storage[i].i32;
                break;
            case FFI_KIND_UINT32:
                if (!valueToUnsigned(val, &unsignedValue)) goto invalid_argument;
                storage[i].u32 = (uint32_t)unsignedValue;
                ffiArgs[i] = &storage[i].u32;
                break;
            case FFI_KIND_SINT64:
                if (!valueToSigned(val, &signedValue)) goto invalid_argument;
                storage[i].i64 = signedValue;
                ffiArgs[i] = &storage[i].i64;
                break;
            case FFI_KIND_UINT64:
                if (!valueToUnsigned(val, &unsignedValue)) goto invalid_argument;
                storage[i].u64 = unsignedValue;
                ffiArgs[i] = &storage[i].u64;
                break;
            case FFI_KIND_FLOAT:
                if (!valueToFFIDouble(val, &doubleValue)) goto invalid_argument;
                storage[i].f = (float)doubleValue;
                ffiArgs[i] = &storage[i].f;
                break;
            case FFI_KIND_DOUBLE:
                if (!valueToFFIDouble(val, &doubleValue)) goto invalid_argument;
                storage[i].d = doubleValue;
                ffiArgs[i] = &storage[i].d;
                break;
            case FFI_KIND_BOOL:
                if (!valueToBool(val, &boolValue)) goto invalid_argument;
                storage[i].u8 = boolValue;
                ffiArgs[i] = &storage[i].u8;
                break;
            case FFI_KIND_STRING:
                if (!valueToStringPointer(val, &stringValue)) goto invalid_argument;
                storage[i].p = (void*)stringValue;
                ffiArgs[i] = &storage[i].p;
                break;
            case FFI_KIND_BUFFER:
                if (!valueToBufferPointer(val, &pointerValue)) goto invalid_argument;
                storage[i].p = pointerValue;
                ffiArgs[i] = &storage[i].p;
                break;
            case FFI_KIND_POINTER:
                if (!valueToPointer(val, &pointerValue)) goto invalid_argument;
                storage[i].p = pointerValue;
                ffiArgs[i] = &storage[i].p;
                break;
            case FFI_KIND_VOID:
            default:
                goto invalid_argument;
        }
    }

    if (fn->rtypeKind == FFI_KIND_VOID) {
        ffi_call(&fn->cif, FFI_FN(fn->fnPtr), NULL, ffiArgs);
        free(storage);
        free(ffiArgs);
        return NIL_VAL;
    }

    FFIValueStorage resultStorage;
    memset(&resultStorage, 0, sizeof(resultStorage));
    ffi_call(&fn->cif, FFI_FN(fn->fnPtr), &resultStorage, ffiArgs);

    free(storage);
    free(ffiArgs);

    switch (fn->rtypeKind) {
        case FFI_KIND_SINT8:
            return INT_VAL((int8_t)resultStorage.arg);
        case FFI_KIND_UINT8:
            return INT_VAL((uint8_t)resultStorage.arg);
        case FFI_KIND_SINT16:
            return INT_VAL((int16_t)resultStorage.arg);
        case FFI_KIND_UINT16:
            return INT_VAL((uint16_t)resultStorage.arg);
        case FFI_KIND_SINT32:
            return INT_VAL((int32_t)resultStorage.arg);
        case FFI_KIND_UINT32:
            return INT_VAL((uint32_t)resultStorage.arg);
        case FFI_KIND_SINT64:
            return INT_VAL(resultStorage.i64);
        case FFI_KIND_UINT64:
            return INT_VAL((int64_t)resultStorage.u64);
        case FFI_KIND_FLOAT:
            return FLOAT_VAL((double)resultStorage.f);
        case FFI_KIND_DOUBLE:
            return FLOAT_VAL(resultStorage.d);
        case FFI_KIND_BOOL:
            return BOOL_VAL(resultStorage.arg != 0);
        case FFI_KIND_STRING:
            {
                char* resVal = (char*)resultStorage.p;
                if (resVal == NULL) return NIL_VAL;

                size_t length = strlen(resVal);
                if (length > (size_t)INT_MAX) {
                    runtimeError("FFI string result is too large.");
                    return NIL_VAL;
                }
                return OBJ_VAL(copyString(resVal, (int)length));
            }
        case FFI_KIND_POINTER:
            {
                void* resVal = resultStorage.p;
                if (resVal == NULL) return NIL_VAL;
                return INT_VAL((int64_t)(uintptr_t)resVal);
            }
        case FFI_KIND_VOID:
        default:
            runtimeError("Unsupported FFI return type '%s'.", fn->rtypeName);
            return NIL_VAL;
    }

invalid_argument:
    runtimeError("FFI argument %d to '%s' is not valid for type '%s'.",
            i + 1, fn->name != NULL ? fn->name : "function", fn->argTypeNames[i]);
    free(storage);
    free(ffiArgs);
    return NIL_VAL;
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
    if (lib == NULL) {
        dlclose(handle);
        runtimeError("Could not allocate memory for FFI library handle.");
        return NIL_VAL;
    }
    lib->handle = handle;
    lib->refs = 1;

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

    if (argCount < 2 || argCount > 3) {
        runtimeError("bind() expects a symbol name, a return type, and an optional array of argument types.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("bind() requires symbol name and returntype strings.");
        return NIL_VAL;
    }

    if (argCount == 3 && !IS_ARRAY(args[2])) {
        runtimeError("bind() argument types must be an array of strings.");
        return NIL_VAL;
    }

    const char* symName = AS_CSTRING(args[0]);
    const char* rtypeStr = AS_CSTRING(args[1]);

    dlerror();
    void* fnPtr = dlsym(lib->handle, symName);
    const char* symError = dlerror();
    if (fnPtr == NULL || symError != NULL) {
        dlerror();
        fnPtr = dlsym(RTLD_DEFAULT, symName);
        symError = dlerror();
    }

    if (fnPtr == NULL || symError != NULL) {
        runtimeError("Symbol '%s' not found: %s", symName,
                symError != NULL ? symError : "unknown symbol");
        return NIL_VAL;
    }

    ffi_type* rtype = NULL;
    FFIKind rtypeKind;
    if (!parseFFIType(rtypeStr, &rtype, &rtypeKind)) {
        runtimeError("Unknown return type '%s'.", rtypeStr);
        return NIL_VAL;
    }
    if (rtypeKind == FFI_KIND_BUFFER) {
        runtimeError("Return type 'buffer' is not supported.");
        return NIL_VAL;
    }

    int numArgs = 0;
    ffi_type** argTypes = NULL;
    FFIKind* argKinds = NULL;
    char** argTypeNames = NULL;

    if (argCount == 3) {
        ObjArray* arr = AS_ARRAY(args[2]);
        numArgs = arr->count;

        if (numArgs > 0) {
            argTypes = (ffi_type**)malloc(sizeof(ffi_type*) * (size_t)numArgs);
            argKinds = (FFIKind*)malloc(sizeof(FFIKind) * (size_t)numArgs);
            argTypeNames = (char**)calloc((size_t)numArgs, sizeof(char*));

            if (argTypes == NULL || argKinds == NULL || argTypeNames == NULL) {
                runtimeError("Could not allocate memory for FFI argument types.");
                goto cleanup;
            }

            for (int i = 0; i < numArgs; i++) {
                if (!IS_STRING(arr->values[i])) {
                    runtimeError("Argument types must be strings.");
                    goto cleanup;
                }

                const char* typeName = AS_CSTRING(arr->values[i]);
                ffi_type* argType = NULL;
                FFIKind argKind;
                if (!parseFFIType(typeName, &argType, &argKind)) {
                    runtimeError("Unknown argument type '%s'.", typeName);
                    goto cleanup;
                }
                if (argKind == FFI_KIND_VOID) {
                    runtimeError("Argument type 'void' is not valid.");
                    goto cleanup;
                }

                argTypes[i] = argType;
                argKinds[i] = argKind;
                argTypeNames[i] = strdup(typeName);
                if (argTypeNames[i] == NULL) {
                    runtimeError("Could not allocate memory for FFI argument type names.");
                    goto cleanup;
                }
            }
        }
    }

    SloxFFIFunc* fn = (SloxFFIFunc*)calloc(1, sizeof(SloxFFIFunc));
    if (fn == NULL) {
        runtimeError("Could not allocate memory for FFI function.");
        goto cleanup;
    }

    // Move the argument metadata into the function object so one cleanup path owns it.
    fn->fnPtr = fnPtr;
    fn->rtype = rtype;
    fn->rtypeKind = rtypeKind;
    fn->argTypes = argTypes;
    fn->argKinds = argKinds;
    fn->argTypeNames = argTypeNames;
    fn->argCount = numArgs;
    argTypes = NULL;
    argKinds = NULL;
    argTypeNames = NULL;

    fn->name = strdup(symName);
    fn->rtypeName = strdup(rtypeStr);
    if (fn->name == NULL || fn->rtypeName == NULL) {
        runtimeError("Could not allocate memory for FFI function metadata.");
        freeFFIData(fn);
        return NIL_VAL;
    }

    if (ffi_prep_cif(&fn->cif, FFI_DEFAULT_ABI, numArgs, rtype, fn->argTypes) != FFI_OK) {
        runtimeError("ffi_prep_cif failed for symbol '%s'.", symName);
        freeFFIData(fn);
        return NIL_VAL;
    }

    // Keep the library loaded for as long as any function bound from it is reachable.
    lib->refs++;
    fn->owner = lib;

    ObjNative* nativeFn = newNative(ffiNativeCallHandler);
    nativeFn->foreignData = (void*)fn;
    nativeFn->destructor = freeFFIFuncDestructor;
    return OBJ_VAL(nativeFn);

cleanup:
    if (argTypeNames != NULL) {
        for (int i = 0; i < numArgs; i++) {
            free(argTypeNames[i]);
        }
        free(argTypeNames);
    }
    free(argTypes);
    free(argKinds);
    return NIL_VAL;
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

    pop();
    pop();
    pop();
    pop();
}
