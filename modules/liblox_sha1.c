#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../vm.h"

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA1_CTX;

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void sha1_compile(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t w[80];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i * 4] << 24) |
            ((uint32_t)buffer[i * 4 + 1] << 16) |
            ((uint32_t)buffer[i * 4 + 2] << 8) |
            (uint32_t)buffer[i * 4 + 3];
    }

    for (i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    // Round 1
    for (i = 0; i < 20; i++) {
        uint32_t t = rol(a, 5) + ((b & c) | (~b & d)) + e + w[i] + 0x5A827999;
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }

    // Round 2
    for (; i < 40; i++) {
        uint32_t t = rol(a, 5) + (b ^ c ^ d) + e + w[i] + 0x6ED9EBA1;
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }

    // Round 3
    for (; i < 60; i++) {
        uint32_t t = rol(a, 5) + ((b & c) | (b & d) | (c & d)) + e + w[i] + 0x8F1BBCDC;
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }

    // Round 4
    for (; i < 80; i++) {
        uint32_t t = rol(a, 5) + (b ^ c ^ d) + e + w[i] + 0xCA62C1D6;
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void sha1_init(SHA1_CTX* ctx) {
    ctx->state[0] = 0x67452301; ctx->state[1] = 0xEFCDAB89; ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476; ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

void sha1_update(SHA1_CTX* ctx, const uint8_t* data, uint32_t len) {
    uint32_t i, j;
    j = (ctx->count[0] >> 3) & 63;
    if ((ctx->count[0] += len << 3) < (len << 3)) ctx->count[1]++;
    ctx->count[1] += (len >> 29);
    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        sha1_compile(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64) sha1_compile(ctx->state, &data[i]);
        j = 0;
    } else i = 0;
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

void sha1_final(SHA1_CTX* ctx, uint8_t digest[20]) {
    uint32_t i; uint8_t finalcount[8];
    for (i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }
    sha1_update(ctx, (const uint8_t *)"\x80", 1);
    while ((ctx->count[0] & 504) != 448) sha1_update(ctx, (const uint8_t *)"\0", 1);
    sha1_update(ctx, finalcount, 8);
    for (i = 0; i < 20; i++) {
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

static Value build_return_array(const uint8_t* digest) {
    ObjArray* array = newArray();
    push(OBJ_VAL(array));

    for (int i = 0; i < 5; i++) {
        uint32_t h = ((uint32_t)digest[i * 4] << 24) |
            ((uint32_t)digest[i * 4 + 1] << 16) |
            ((uint32_t)digest[i * 4 + 2] << 8) |
            (uint32_t)digest[i * 4 + 3];

        arrayAppend(array, NUMBER_VAL((double)h));
    }
    pop();
    return OBJ_VAL(array);
}

static Value sha1NativeMethod(int argCount, Value* args) {
    if (IS_CLASS(args[-1])) {
        if (argCount < 1 || !IS_STRING(args[0])) {
            runtimeError("sha1() expects a string argument.");
            return NIL_VAL;
        }

        ObjString* str = AS_STRING(args[0]);
        SHA1_CTX* ctx = malloc(sizeof(SHA1_CTX));
        if (ctx == NULL) {
            runtimeError("Out of memory during SHA1 calculation.");
            return NIL_VAL;
        }

        uint8_t digest[20];

        sha1_init(ctx);
        sha1_update(ctx, (const uint8_t*)str->chars, str->length);
        sha1_final(ctx, digest);

        free(ctx);
        return build_return_array(digest);
    } else if (IS_INSTANCE(args[-1])) {
        ObjInstance* instance = AS_INSTANCE(args[-1]);
        SHA1_CTX* ctx = (SHA1_CTX*)instance->foreignPtr;
        if (ctx == NULL) {
            runtimeError("Cannot call methods on an uninitialized SHA1 instance.");
            return NIL_VAL;
        }

        // ...
        ObjString* str = AS_STRING(args[0]);
        uint8_t digest[20];

        //sha1_init(ctx);
        sha1_update(ctx, (const uint8_t*)str->chars, str->length);
        sha1_final(ctx, digest);

        return build_return_array(digest);
    }

    runtimeError("Invalid receiver for sha1 method.");
    return NIL_VAL;
}

static Value sha1InitMethod(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);

    if (argCount == 0) {
        // streaming object
        // var sha1 = SHA1();
        SHA1_CTX* ctx = malloc(sizeof(SHA1_CTX));
        if (ctx == NULL) {
            runtimeError("Out of memory during SHA1 calculation.");
            return NIL_VAL;
        }

        sha1_init(ctx);
        instance->foreignPtr = ctx;

        return args[-1];
    } else if (argCount == 1 && IS_STRING(args[0])) {
        // direct hash constructor
        // var x = SHA1("this is a string");
        ObjString* str = AS_STRING(args[0]);

        SHA1_CTX* ctx = malloc(sizeof(SHA1_CTX));
        if (ctx == NULL) {
            runtimeError("Out of memory during SHA1 calculation.");
            return NIL_VAL;
        }

        uint8_t digest[20];

        sha1_init(ctx);
        sha1_update(ctx, (const uint8_t*)str->chars, str->length);
        sha1_final(ctx, digest);

        free(ctx);

        return build_return_array(digest);
    }
    runtimeError("SHA1 constructor expects either 0 arguments or 1 string argument.");
    return NIL_VAL;
}

static Value sha1UpdateMethod(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("update() expects a string argument.");
        return NIL_VAL;
    }

    SHA1_CTX* ctx = (SHA1_CTX*)instance->foreignPtr;
    ObjString* str = AS_STRING(args[0]);

    sha1_update(ctx, (const uint8_t*)str->chars, str->length);

    return args[-1];
}

static Value sha1FinalMethod(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SHA1_CTX* ctx = (SHA1_CTX*)instance->foreignPtr;

    if (ctx == NULL) {
        runtimeError("Cannot call final() on an uninitialized or clased SHA1 instance.");
        return NIL_VAL;
    }

    uint8_t digest[20];

    sha1_final(ctx, digest);

    free(ctx);
    instance->foreignPtr = NULL;

    ObjArray* array = newArray();
    push(OBJ_VAL(array));

    for (int i = 0; i < 5; i++) {
        uint32_t h = ((uint32_t)digest[i * 4] << 24) |
            ((uint32_t)digest[i * 4 + 1] << 16) |
            ((uint32_t)digest[i * 4 + 2] << 8) |
            (uint32_t)digest[i * 4 + 3];

        arrayAppend(array, NUMBER_VAL((double)h));
    }

    pop();
    return OBJ_VAL(array);
}

void sha1Destructor(ObjInstance* inst) {
    if (inst->foreignPtr != NULL) {
        free(inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
}

void lox_module_init(VM* vm) {
    ObjString* str = copyString("SHA1", 4);
    push(OBJ_VAL(str));
    ObjClass* sha1Class = newClass(str);
    sha1Class->superclass = vm->moduleClass;
    push(OBJ_VAL(sha1Class));
    tableSet(&vm->globals, str, OBJ_VAL(sha1Class));
    sha1Class->destructor = sha1Destructor;

    ObjNative* sha1NativeFn = newNative(sha1NativeMethod);
    push(OBJ_VAL(sha1NativeFn));
    tableSet(&sha1Class->methods, copyString("sha1", 4), OBJ_VAL(sha1NativeFn));
    pop();

    ObjNative* sha1InitFn = newNative(sha1InitMethod);
    push(OBJ_VAL(sha1InitFn));
    tableSet(&sha1Class->methods, copyString("init", 4), OBJ_VAL(sha1InitFn));
    pop();

    ObjNative* sha1UpdateFn = newNative(sha1UpdateMethod);
    push(OBJ_VAL(sha1UpdateFn));
    tableSet(&sha1Class->methods, copyString("update", 6), OBJ_VAL(sha1UpdateFn));
    pop();

    ObjNative* sha1FinalFn = newNative(sha1FinalMethod);
    push(OBJ_VAL(sha1FinalFn));
    tableSet(&sha1Class->methods, copyString("final", 5), OBJ_VAL(sha1FinalFn));
    pop();

    pop();
    pop();
}
