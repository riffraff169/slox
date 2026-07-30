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
    ObjInstance* inst = AS_INSTANCE(args[-1]);
    if (!inst->foreignPtr) {
        runtimeError("Cannot exec query on closed database connections.");
        return NIL_VAL;
    }

    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("exec() requires a SQL string as the first argument.");
        return NIL_VAL;
    }

    sqlite3* db = (sqlite3*)inst->foreignPtr;
    const char* sql = AS_CSTRING(args[0]);

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        runtimeError("Sqlite Error: %s", sqlite3_errmsg(db));
        return NIL_VAL;
    }

    if (argCount > 1 && IS_ARRAY(args[1])) {
        ObjArray* params = AS_ARRAY(args[1]);
        for (int i = 0; i < params->count; i++) {
            Value val = params->values[i];
            int paramIdx = i + 1;

            if (IS_NUMBER(val)) {
                sqlite3_bind_double(stmt, paramIdx, AS_NUMBER(val));
            } else if (IS_STRING(val)) {
                ObjString* str = AS_STRING(val);
                sqlite3_bind_text(stmt, paramIdx, str->chars, str->length, SQLITE_TRANSIENT);
            } else if (IS_BOOL(val)) {
                sqlite3_bind_int(stmt, paramIdx, AS_BOOL(val) ? 1 : 0);
            } else if (IS_NIL(val)) {
                sqlite3_bind_null(stmt, paramIdx);
            }
        }
    }

    ObjArray* results = newArray();
    push(OBJ_VAL(results));

    bool isSelect = false;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        isSelect = true;
        int cols = sqlite3_column_count(stmt);

        ObjMap* row = newMap();
        push(OBJ_VAL(row));

        for (int i = 0; i < cols; i++) {
            const char* colName = sqlite3_column_name(stmt, i);
            ObjString* key = copyString(colName, (int)strlen(colName));
            push(OBJ_VAL(key));

            Value val = NIL_VAL;
            switch (sqlite3_column_type(stmt, i)) {
                case SQLITE_INTEGER:
                    val = NUMBER_VAL((double)sqlite3_column_int64(stmt, i));
                    break;
                case SQLITE_FLOAT:
                    val = NUMBER_VAL(sqlite3_column_double(stmt, i));
                    break;
                case SQLITE_TEXT:
                    {
                        const char* text = (const char*)sqlite3_column_text(stmt, i);
                        int len = sqlite3_column_bytes(stmt, i);
                        val = OBJ_VAL(copyString(text, len));
                    }
                    break;
                case SQLITE_NULL:
                default:
                    val = NIL_VAL;
                    break;

            }

            push(val);
            mapSet(row, OBJ_VAL(key), val);
            pop();
            pop();
        }

        arrayAppend(results, OBJ_VAL(row));
        pop();
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        pop();
        runtimeError("Sqlite Execution Error: %s", sqlite3_errmsg(db));
        return NIL_VAL;
    }

    if (isSelect) {
        return pop();
    } else {
        pop();
        return NUMBER_VAL((double)sqlite3_changes(db));
    }
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
