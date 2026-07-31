#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include "vm.h"

#ifndef BOOLOID
#define BOOLOID 16
#define INT8OID 20
#define INT2OID 21
#define INT4OID 23
#define FLOAT4OID 700
#define FLOAT8OID 701
#define NUMERICOID 1700
#endif

void postgresDestructor(ObjInstance* inst) {
    if (inst->foreignPtr) {
        PQfinish((PGconn*)inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
}

Value postgresCallHandler(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("Postgres() requires a connection string or URI.");
        return NIL_VAL;
    }

    const char* conninfo = AS_CSTRING(args[0]);
    PGconn* conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        runtimeError("Postgres connection failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        return NIL_VAL;
    }

    ObjClass* klass = AS_CLASS(args[-1]);
    ObjInstance* inst = newInstance(klass);
    inst->foreignPtr = (void*)conn;
    return OBJ_VAL(inst);
}

Value postgresClose(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[-1]); if (inst->foreignPtr) {
        PQfinish((PGconn*)inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
    return NIL_VAL;
}

Value postgresQuery(int argCount, Value* args) {
    ObjInstance* inst = AS_INSTANCE(args[-1]);
    if (!inst->foreignPtr) {
        runtimeError("Cannot query on a closed Postgres connection.");
        return NIL_VAL;
    }

    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("query() requires a SQL string.");
        return NIL_VAL;
    }

    PGconn* conn = (PGconn*)inst->foreignPtr;
    const char* sql = AS_CSTRING(args[0]);

    int nParams = 0;
    char** paramValues = NULL;

    if (argCount > 1 && IS_ARRAY(args[1])) {
        ObjArray* arr = AS_ARRAY(args[1]);
        nParams = arr->count;
        if (nParams > 0) {
            paramValues = malloc(sizeof(char*) * nParams);
            for (int i = 0; i < nParams; i++) {
                Value val = arr->values[i];
                if (IS_NIL(val)) {
                    paramValues[i] = NULL;
                } else if (IS_STRING(val)) {
                    paramValues[i] = AS_CSTRING(val);
                } else {
                    char buf[64];
                    if (IS_NUMBER(val)) snprintf(buf, sizeof(buf), "%g", AS_NUMBER(val));
                    else if (IS_BOOL(val)) snprintf(buf, sizeof(buf), "%s", AS_BOOL(val) ? "true" : "false");
                    paramValues[i] = strdup(buf);
                }
            }
        }
    }

    PGresult* res = PQexecParams(
            conn, sql, nParams, NULL,
            (const char* const*)paramValues,
            NULL, NULL, 0
            );

    if (paramValues) {
        for (int i = 0; i < nParams; i++) {
            Value val = AS_ARRAY(args[1])->values[i];
            if (!IS_NIL(val) && !IS_STRING(val) && paramValues[i]) {
                free(paramValues[i]);
            }
        }
        free(paramValues);
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        runtimeError("Postgres QUery Error: %s", PQerrorMessage(conn));
        PQclear(res);
        return NIL_VAL;
    }

    if (status == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        int cols = PQnfields(res);

        ObjArray* results = newArray();
        push(OBJ_VAL(results));

        for (int r = 0; r < rows; r++) {
            ObjMap* rowMap = newMap();
            push(OBJ_VAL(rowMap));

            for (int c = 0; c < cols; c++) {
                char* colName = PQfname(res, c);
                ObjString* key = copyString(colName, (int)strlen(colName));
                push(OBJ_VAL(key));

                Value val = NIL_VAL;
                if (!PQgetisnull(res, r, c)) {
					Oid typeOid = PQftype(res, c);
                    char* valStr = PQgetvalue(res, r, c);
                    int valLen = PQgetlength(res, r, c);

                    switch (typeOid) {
                        case BOOLOID:
                            val = BOOL_VAL(valStr[0] == 't');
                            break;
                        case INT2OID:
                        case INT4OID:
                        case INT8OID:
                        case FLOAT4OID:
                        case FLOAT8OID:
                            {
                                char* endptr;
                                double num = strtod(valStr, &endptr);
                                if (endptr != valStr) {
                                    val = NUMBER_VAL(num);
                                } else {
                                    val = OBJ_VAL(copyString(valStr, valLen));
                                }
                            }
                            break;
                        default:
                            val = OBJ_VAL(copyString(valStr, valLen));
                            break;
                    }
                }

                push(val);
                mapSet(rowMap, OBJ_VAL(key), val);
                pop();
                pop();
            }
            arrayAppend(results, OBJ_VAL(rowMap));
            pop();
        }
        PQclear(res);
        return pop();
    }

    char* affectedStr = PQcmdTuples(res);
    double affected = 0.0;
    if (affectedStr != NULL && affectedStr[0] != '\0') {
        affected = strtod(affectedStr, NULL);
    }
    PQclear(res);

    return NUMBER_VAL(affected);
}

void lox_module_init(VM* vm) {
    ObjString* str = copyString("Postgres", 8);
    push(OBJ_VAL(str));
    ObjClass* pgClass = newClass(str);
    pgClass->superclass = vm->objectClass;
    pgClass->callHandler = postgresCallHandler;
    pgClass->destructor = postgresDestructor;
    push(OBJ_VAL(pgClass));
    tableSet(&vm->globals, str, OBJ_VAL(pgClass));

    ObjNative* queryFn = newNative(postgresQuery);
    push(OBJ_VAL(queryFn));
    tableSet(&pgClass->methods, copyString("query", 5), OBJ_VAL(queryFn));
    pop();

    ObjNative* closeFn = newNative(postgresClose);
    push(OBJ_VAL(closeFn));
    tableSet(&pgClass->methods, copyString("close", 5), OBJ_VAL(closeFn));
    pop();

    pop();
    pop();
}

