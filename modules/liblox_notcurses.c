#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <notcurses/notcurses.h>
#include "vm.h"

static ObjClass* planeClass = NULL;

typedef struct {
    struct notcurses* nc;
    bool stopped;
    int refCount;
} SloxNCContext;

typedef struct {
    struct ncplane* plane;
    SloxNCContext* ctx;
} SloxPlaneWrapper;

void ncDestructor(ObjInstance* inst) {
    if (inst->foreignPtr) {
        SloxNCContext* ctx = (SloxNCContext*)inst->foreignPtr;
        if (!ctx->stopped) {
            if (ctx->nc) notcurses_stop(ctx->nc);
            ctx->nc = NULL;
            ctx->stopped = true;
        }
        ctx->refCount--;
        if (ctx->refCount <= 0) {
            free(ctx);
        }
        inst->foreignPtr = NULL;
    }
}

void planeDestructor(ObjInstance* inst) {
    if (inst->foreignPtr) {
        SloxPlaneWrapper* pw = (SloxPlaneWrapper*)inst->foreignPtr;
        // do not call ncplane_destroy here, notcurses handles tree teardown
        pw->ctx->refCount--;
        if (pw->ctx->refCount <= 0) {
            free(pw->ctx);
        }
        free(pw);
        inst->foreignPtr = NULL;
    }
}

Value ncStop(int argCount, Value* args) {
    ObjInstance* self = AS_INSTANCE(args[-1]);
    if (self->foreignPtr) {
        SloxNCContext* ctx = (SloxNCContext*)self->foreignPtr;
        if (!ctx->stopped) {
            if (ctx->nc) notcurses_stop(ctx->nc);
            ctx->nc = NULL;
            ctx->stopped = true;
        }
    }
    return NIL_VAL;
}


Value ncRender(int argCount, Value* args) {
    ObjInstance*self = AS_INSTANCE(args[-1]);
    SloxNCContext* ctx = (SloxNCContext*)self->foreignPtr;
    if (ctx && !ctx->stopped && ctx->nc) {
        notcurses_render(ctx->nc);
    }
    return NIL_VAL;
}

Value ncGetStdPlane(int argCount, Value* args) {
    ObjInstance*self = AS_INSTANCE(args[-1]);
    SloxNCContext* ctx = (SloxNCContext*)self->foreignPtr;
    if (!ctx || ctx->stopped) return NIL_VAL;

    struct ncplane* stdp = notcurses_stdplane(ctx->nc);

    SloxPlaneWrapper* pw = malloc(sizeof(SloxPlaneWrapper));
    pw->plane = stdp;
    pw->ctx = ctx;
    ctx->refCount++;

    ObjInstance* planeInst = newInstance(planeClass);
    planeInst->foreignPtr = (void*)pw;

    return OBJ_VAL(planeInst);
}

Value ncGetKey(int argCount, Value* args) {
    ObjInstance*self = AS_INSTANCE(args[-1]);
    SloxNCContext* ctx = (SloxNCContext*)self->foreignPtr;
    if (!ctx || ctx->stopped || !ctx->nc) return NIL_VAL;

    struct timespec ts = {0, 0};
    struct ncinput ni;
    uint32_t key = notcurses_get(ctx->nc, &ts, &ni);

    if (key == (uint32_t)-1 || key == 0) {
        return NIL_VAL;
    }

    if (key < 128) {
        char buf[2] = {(char)key, '\0'};
        return OBJ_VAL(copyString(buf, 1));
    }

    return NUMBER_VAL((double)key);
}

// Notcurses(...) constructor
Value ncCallhandler(int argCount, Value* args) {
    ObjClass* klass = AS_CLASS(args[-1]);
    ObjInstance* inst = newInstance(klass);

    struct notcurses_options opts = {
        .flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_ALTERNATE_SCREEN
    };

    struct notcurses* nc = notcurses_init(&opts, stdout);
    if (!nc) {
        return NIL_VAL;
    }

    SloxNCContext* ctx = malloc(sizeof(SloxNCContext));
    ctx->nc = nc;
    ctx->stopped = false;
    ctx->refCount = 1;

    inst->foreignPtr = (void*)ctx;
    return OBJ_VAL(inst);
}

// plane methods
//
// Plane.create_child(y, x, rows, cols)
Value planeCreateChild(int argCount, Value* args) {
    if (argCount < 4 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
            !IS_NUMBER(args[2]) || !IS_NUMBER(args[3])) {
        return NIL_VAL;
    }

    ObjInstance* self = AS_INSTANCE(args[-1]);
    SloxPlaneWrapper* parentPw = (SloxPlaneWrapper*)self->foreignPtr;
    if (!parentPw || parentPw->ctx->stopped || !parentPw->plane) return NIL_VAL;

    int y = (int)AS_NUMBER(args[0]);
    int x = (int)AS_NUMBER(args[1]);
    int rows = (int)AS_NUMBER(args[2]);
    int cols = (int)AS_NUMBER(args[3]);

    struct ncplane_options opts = {
        .y = y,
        .x = x,
        .rows = rows,
        .cols = cols,
    };

    struct ncplane* child = ncplane_create(parentPw->plane, &opts);
    if (!child) return NIL_VAL;

    SloxPlaneWrapper* childPw = malloc(sizeof(SloxPlaneWrapper));
    childPw->plane = child;
    childPw->ctx = parentPw->ctx;
    parentPw->ctx->refCount++;

    ObjInstance* childInst = newInstance(planeClass);
    childInst->foreignPtr = (void*)childPw;

    return OBJ_VAL(childInst);
}

// Plane.put_str(y, x, "text")
Value planePutStr(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
            !IS_STRING(args[2])) {
        return NIL_VAL;
    }

    ObjInstance* self = AS_INSTANCE(args[-1]);
    SloxPlaneWrapper* pw = (SloxPlaneWrapper*)self->foreignPtr;
    if (!pw || pw->ctx->stopped || !pw->plane) return NIL_VAL;

    int y = (int)AS_NUMBER(args[0]);
    int x = (int)AS_NUMBER(args[1]);
    const char* str = AS_CSTRING(args[2]);

    ncplane_putstr_yx(pw->plane, y, x, str);
    return NIL_VAL;
}

bool parseRgbArgs(int argCount, Value* args, uint32_t* r, uint32_t* g, uint32_t* b) {
    // 3 args (255, 128, 0)
    if (argCount == 3 && IS_NUMBER(args[0]) && IS_NUMBER(args[1]) && IS_NUMBER(args[2])) {
        *r = (uint32_t)AS_NUMBER(args[0]);
        *g = (uint32_t)AS_NUMBER(args[1]);
        *b = (uint32_t)AS_NUMBER(args[2]);
        return true;
    }

    if (argCount == 1 && IS_ARRAY(args[0])) {
        ObjArray* arr = AS_ARRAY(args[0]);
        if (arr->count == 3 &&
                IS_NUMBER(arr->values[0]) &&
                IS_NUMBER(arr->values[1]) &&
                IS_NUMBER(arr->values[2])) {
            *r = (uint32_t)AS_NUMBER(arr->values[0]);
            *g = (uint32_t)AS_NUMBER(arr->values[1]);
            *b = (uint32_t)AS_NUMBER(arr->values[2]);
            return true;
        }
    }
    return false;
}

// Plane.set_fgrgb(r, g, b) or Plane.set_fgrgb([r, g, b])
Value planeSetFgRgb(int argCount, Value* args) {
    ObjInstance* self = AS_INSTANCE(args[-1]);
    SloxPlaneWrapper* pw = (SloxPlaneWrapper*)self->foreignPtr;
    if (!pw || pw->ctx->stopped || !pw->plane) return NIL_VAL;

    uint32_t r, g, b;
    if (!parseRgbArgs(argCount, args, &r, &g, &b)) {
        return NIL_VAL;
    }

    ncplane_set_fg_rgb8(pw->plane, r, g, b);
    return NIL_VAL;
}

// Plane.set_bgrgb(r, g, b) or Plane.setBgRgb([r, g, b])
Value planeSetBgRgb(int argCount, Value* args) {
    ObjInstance* self = AS_INSTANCE(args[-1]);
    SloxPlaneWrapper* pw = (SloxPlaneWrapper*)self->foreignPtr;
    if (!pw || pw->ctx->stopped || !pw->plane) return NIL_VAL;

    uint32_t r, g, b;
    if (!parseRgbArgs(argCount, args, &r, &g, &b)) {
        return NIL_VAL;
    }


    ncplane_set_bg_rgb8(pw->plane, r, g, b);
    return NIL_VAL;
}

Value planeErase(int argCount, Value* args) {
    ObjInstance* self = AS_INSTANCE(args[-1]);
    SloxPlaneWrapper* pw = (SloxPlaneWrapper*)self->foreignPtr;
    if (pw && !pw->ctx->stopped && pw->plane)
        ncplane_erase(pw->plane);
    return NIL_VAL;
}

Value planeDestroy(int argCount, Value* args) {
    ObjInstance* self = AS_INSTANCE(args[-1]);
    if (self->foreignPtr) {
        SloxPlaneWrapper* pw = (SloxPlaneWrapper*)self->foreignPtr;
        if (!pw->ctx->stopped && pw->plane) {
            if (ncplane_parent(pw->plane) != NULL) {
                ncplane_destroy(pw->plane);
            }
            pw->plane = NULL;
        }
    }
    return NIL_VAL;
}

void lox_module_init(VM* vm) {
    // 1. Plane class definition
    ObjString* planeStr = copyString("Plane", 5);
    push(OBJ_VAL(planeStr));
    planeClass = newClass(planeStr);
    planeClass->superclass = vm->objectClass;
    planeClass->destructor = planeDestructor;
    push(OBJ_VAL(planeClass));
    tableSet(&vm->globals, planeStr, OBJ_VAL(planeClass));

    nativeBindFunction(planeClass, "create_child", planeCreateChild);
    nativeBindFunction(planeClass, "put_str", planePutStr);
    nativeBindFunction(planeClass, "set_fgrgb", planeSetFgRgb);
    nativeBindFunction(planeClass, "set_bgrgb", planeSetBgRgb);
    nativeBindFunction(planeClass, "erase", planeErase);
    nativeBindFunction(planeClass, "destroy", planeDestroy);

    pop();
    pop();

    ObjString* ncStr = copyString("Notcurses", 9);
    push(OBJ_VAL(ncStr));
    ObjClass* ncClass = newClass(ncStr);
    ncClass->superclass = vm->objectClass;
    ncClass->callHandler = ncCallhandler;
    ncClass->destructor = ncDestructor;
    push(OBJ_VAL(ncClass));
    tableSet(&vm->globals, ncStr, OBJ_VAL(ncClass));

    nativeBindFunction(ncClass, "render", ncRender);
    nativeBindFunction(ncClass, "get_stdplane", ncGetStdPlane);
    nativeBindFunction(ncClass, "get_key", ncGetKey);
    nativeBindFunction(ncClass, "stop", ncStop);

    pop();
    pop();
}
