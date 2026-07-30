#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sqlite3.h>
#include "vm.h"

void sqliteDestructor(ObjInstance* inst) {
    if (inst->foreignPtr) {
        int rc = sqlite3_close_v2((sqlite3*)inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
}

Value sqliteCallHandler(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("Sqlite() requires a database name");
        return NIL_VAL;
    }

    ObjClass* klass = AS_CLASS(args[-1]);
    sqlite3 *db;

    int rc = sqlite3_open(AS_CSTRING(args[0]), &db);
    if (rc) {
        runtimeError("Can't open database %s", AS_CSTRING(args[0]));
        return NIL_VAL;
    }
    ObjInstance* inst = newInstance(klass);
    inst->foreignPtr = (void*)db;
    return OBJ_VAL(inst);
}

Value sqliteClose(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[-1]);
    if (inst->foreignPtr) {
        int rc = sqlite3_close_v2((sqlite3*)inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
    return NIL_VAL;
}

Value sqliteExec(int argCount, Value* args) {
}

void lox_module_init(VM* vm) {
    ObjString* str = copyString("Sqlite", 6);
    push(OBJ_VAL(str));
    ObjClass* sqliteClass = newClass(str);
    sqliteClass->superclass = vm->objectClass;
    sqliteClass->callHandler = sqliteCallHandler;
    push(OBJ_VAL(sqliteClass));
    tableSet(&vm->globals, str, OBJ_VAL(sqliteClass));
    sqliteClass->destructor = sqliteDestructor;

    ObjNative* sqliteExecFn = newNative(sqliteExec);
    push(OBJ_VAL(sqliteExecFn));
    tableSet(&sqliteClass->methods, copyString("exec", 4), OBJ_VAL(sqliteExecFn));
    pop();

    ObjNative* sqliteCloseFn = newNative(sqliteClose);
    push(OBJ_VAL(sqliteCloseFn));
    tableSet(&sqliteClass->methods, copyString("close", 5), OBJ_VAL(sqliteCloseFn));
    pop();

    pop();
    pop();
}
