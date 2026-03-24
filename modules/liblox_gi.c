#include <stdio.h>
#include <girepository.h>
#include "../vm.h"

//static Table keepAlive;
static ObjInstance* globalRegistry = NULL;

static void stub_callback(void) {
    printf("[DEBUG] In stub_callback\n");
}

void initGiModule() {
}

/*
ObjString* getClosureKey(GClosure* closure) {
    char addr[32];
    sprintf(addr, "%p", (void*)closure);
    return copyString(addr, strlen(addr));
}
*/

void LoxValueToGValue(Value loxVal, GValue* gval) {
    GType expectedType = G_VALUE_TYPE(gval);

    switch (G_TYPE_FUNDAMENTAL(expectedType)) {
        case G_TYPE_BOOLEAN:
            g_value_set_boolean(gval, AS_BOOL(loxVal));
            break;
        case G_TYPE_INT:
            g_value_set_int(gval, (int)AS_NUMBER(loxVal));
            break;
        case G_TYPE_DOUBLE:
            g_value_set_double(gval, AS_NUMBER(loxVal));
            break;
        case G_TYPE_STRING:
            g_value_set_string(gval, AS_CSTRING(loxVal));
            break;
        case G_TYPE_OBJECT:
            if (IS_NIL(loxVal)) {
                g_value_set_object(gval, NULL);
            } else {
                g_value_set_object(gval, G_OBJECT(AS_INSTANCE(loxVal)->foreignPtr));
            }
            break;
        default:
            printf("Warning: Unsupported return type GType %s\n", g_type_name(expectedType));
            break;
    }
}

Value wrap_gobject_to_lox(GObject* obj) {
    if (obj == NULL) return NIL_VAL;

    const char* typeName = G_OBJECT_TYPE_NAME(obj);
    ObjString* key = copyString(typeName, strlen(typeName));
    push(OBJ_VAL(key)); // push key onto stack

    Value klassValue;
    printf("[DEBUG] In wrap_gobject_to_lox\n");

    printf("[DEBUG] typeName: '%s'\n", key->chars);

    pop(); // remove key from stack

    printf("[DEBUG] Value: ");
    //printValue(klassValue);
    printf("\n");
    GType type = G_TYPE_FROM_INSTANCE(obj);

    while (type != 0) {
        const char* name = g_type_name(type);

        if (tableGet(&globalRegistry->fields, key, &klassValue)) {
            printValue(klassValue);
            ObjClass* klass = AS_CLASS(klassValue);
            ObjInstance* instance = newInstance(klass);
            instance->foreignPtr = obj;
            g_object_ref(obj);
            return OBJ_VAL(instance);
        }
        type = g_type_parent(type);
    }

    /*
    printf("--- Current Registry Contents ---\n");
    for (int i = 0; i < globalRegistry->fields.capacity; i++) {
        Entry* entry = &globalRegistry->fields.entries[i];
        if (entry->key != NULL) {
            printf("  Key: '%s' Ptr: %p\n", entry->key->chars, (void*)entry->key);
        }
    }
    */

    printf("Warning: No Lox class found for GType %s\n", typeName);
    return NIL_VAL;
}

static Value GValueToLoxValue(GValue gval) {
    Value result = NIL_VAL;

    //switch (G_TYPE_FUNDAMENTAL(spec->value_type)) {
    GType gtype = G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(&gval));

    switch (gtype) {
        case G_TYPE_STRING:
            const char* str = g_value_get_string(&gval);
            if (str == NULL) {
                return OBJ_VAL(copyString("", 0));
            } else {
                return OBJ_VAL(copyString(str, strlen(str)));
            } 
        case G_TYPE_INT:
        case G_TYPE_FLOAT:
        case G_TYPE_DOUBLE:
            return NUMBER_VAL(g_value_get_int(&gval));
        case G_TYPE_BOOLEAN:
            return BOOL_VAL(g_value_get_boolean(&gval));
        case G_TYPE_OBJECT:
            {
                GObject* obj = g_value_get_object(&gval);
                if (obj == NULL) return NIL_VAL;

                /*
                const char* type_name = G_OBJECT_TYPE_NAME(gptr);

                Value klass;
                Value giModuleValue;

                ObjString* giStr = copyString("gi", 2);
                push(OBJ_VAL(giStr)); // string
                tableGet(&vm.globals, giStr, &giModuleValue); // OBJ_VAL(giModule));

                ObjInstance* giModule = AS_INSTANCE(giModuleValue);
                if (tableGet(&giModule->fields, copyString(type_name, strlen(type_name)), &klass)) {
                    ObjInstance* instance = newInstance(AS_CLASS(klass));
                    instance->foreignPtr = gptr;
                    g_object_ref(gptr);
                    result = OBJ_VAL(instance);
                } else {
                    printf("Warning: No Lox class found for GType %s\n", type_name);
                    result = NIL_VAL;
                }
                pop(); // string
                */
                return wrap_gobject_to_lox(obj);
            }
            return result;
        case G_TYPE_BOXED:
            return NIL_VAL;
        default:
            printf("Unhandled gtype %d\n", gtype);
            return NIL_VAL;
            break;
    }
    return NIL_VAL;
}

void lox_marshal_generic(GClosure* closure,
        GValue* return_value,
        guint n_params,
        const GValue* params,
        gpointer hint,
        gpointer data) {
    ObjClosure* loxClosure = (ObjClosure*)closure->data;

    Value* stackStart = vm.stackTop;

    push(OBJ_VAL(loxClosure));

    printf("[DEBUG] Signal Marshaller triggered for closure %p\n", (void*)loxClosure);

    for (guint i = 0; i < n_params; i++) {
        push(GValueToLoxValue(params[i]));
    }

    vm.nativeExitDepth = vm.frameCount - 1;

    if (vmCall(loxClosure, n_params)) {
        run();
    }

    if (return_value != NULL && G_IS_VALUE(return_value)) {
        Value loxResult = peek(0);
        LoxValueToGValue(loxResult, return_value);
    }

    vm.stackTop = stackStart;
}

static void loxSignalProxy(GObject* widget, gpointer data) {
    ObjClosure* closure = (ObjClosure*)data;
    push(OBJ_VAL(closure));

    vm.nativeExitDepth = vm.frameCount - 1;

    if (vmCall(closure, 0)) {
        run();
    }
}

static void unpin_from_lox(gpointer data, GClosure* gclosure) {
    char keyBuf[32];
    sprintf(keyBuf, "%p", (void*)gclosure);
    ObjString* key = copyString(keyBuf, strlen(keyBuf));

    tableDelete(&globalRegistry->fields, key);
}

static Value giConnectNative(int argCount, Value* args) {
    if (globalRegistry == NULL) {
        printf("[CRITICAL] globalRegistry is NULL in gi.connect\n");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    ObjString* signal_name = AS_STRING(args[1]);
    ObjClosure* closure = AS_CLOSURE(args[2]);

    GClosure* gclosure = g_cclosure_new(G_CALLBACK(stub_callback), closure, NULL);
    g_closure_set_marshal(gclosure, lox_marshal_generic);

    char keyBuf[32];
    sprintf(keyBuf, "%p", (void*)gclosure);
    ObjString* key = copyString(keyBuf, strlen(keyBuf));

    tableSet(&globalRegistry->fields, key, OBJ_VAL(closure));

    g_closure_add_finalize_notifier(gclosure, closure, (GClosureNotify)unpin_from_lox);

    //g_signal_connect(instance->foreignPtr, signal_name->chars,
    //        G_CALLBACK(loxSignalProxy), closure);

    g_signal_connect_closure(instance->foreignPtr, signal_name->chars,
            gclosure, false);

    printf("[CONNECT] Closure connected\n");

    return NIL_VAL;
}

static Value giInspectNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[0])) return NIL_VAL;
    ObjInstance* instance = AS_INSTANCE(args[0]);

    printf("--- Inspecting Instance %p ---\n", instance);
    for (int i = 0; i < instance->fields.capacity; i++) {
        Entry* entry = &instance->fields.entries[i];
        if (entry->key != NULL) {
            printf(" Field: '%s' [Hash: %u] [Ptr: %p]\n",
                    entry->key->chars, entry->key->hash, (void*)entry->key);
        }
    }
    return NIL_VAL;
}


static bool giPropertySetter(ObjInstance* instance, ObjString* name, Value value) {
    GObjectClass* obj_class = G_OBJECT_GET_CLASS(instance->foreignPtr);
    GParamSpec* spec = g_object_class_find_property(obj_class, name->chars);

    if (spec == NULL) return false;

    if (!(spec->flags & G_PARAM_WRITABLE)) return false;

    GValue gval = G_VALUE_INIT;
    g_value_init(&gval, spec->value_type);

    switch (G_TYPE_FUNDAMENTAL(spec->value_type)) {
        case G_TYPE_STRING:
            if (!IS_STRING(value)) goto type_error;
            g_value_set_string(&gval, AS_CSTRING(value));
            break;
        case G_TYPE_INT:
            if (!IS_NUMBER(value)) goto type_error;
            g_value_set_int(&gval, (int)AS_NUMBER(value));
            break;
        case G_TYPE_DOUBLE:
            if (!IS_NUMBER(value)) goto type_error;
            g_value_set_double(&gval, AS_NUMBER(value));
            break;
        case G_TYPE_BOOLEAN:
            if (!IS_BOOL(value)) goto type_error;
            g_value_set_boolean(&gval, AS_BOOL(value));
            break;
        case G_TYPE_OBJECT:
            {
                if (IS_INSTANCE(value)) {
                    g_value_set_object(&gval, AS_INSTANCE(value)->foreignPtr);
                } else if (IS_NIL(value)) {
                    g_value_set_object(&gval, NULL);
                }
            }
            break;
        default:
            printf("Setter: Unhandled gtype %d\n", spec->value_type);
            g_value_unset(&gval);
            return false;
    }
    
    g_object_set_property(G_OBJECT(instance->foreignPtr), name->chars, &gval);
    g_value_unset(&gval);
    return true;

type_error:
    g_value_unset(&gval);
    return false;
}

static Value giPropertyGetter(ObjInstance* instance, ObjString* name) {
    if (instance->foreignPtr == NULL) return NIL_VAL;

    GObjectClass* obj_class = G_OBJECT_GET_CLASS(instance->foreignPtr);
    GParamSpec* spec = g_object_class_find_property(obj_class, name->chars);

    if (spec == NULL) return NIL_VAL;

    GValue gval = G_VALUE_INIT;
    g_value_init(&gval, spec->value_type);
    g_object_get_property(G_OBJECT(instance->foreignPtr), name->chars, &gval);

    Value result = NIL_VAL;
    result = GValueToLoxValue(gval);
    g_value_unset(&gval);
    return result;
}

static void convertLoxToGI(Value loxValue, GIArgument* giArg, GITypeInfo* type_info) {
    if (type_info == NULL) return;

    GITypeTag tag = g_type_info_get_tag(type_info);
    
    switch (tag) {
        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
            giArg->v_string = (char*)AS_CSTRING(loxValue);
            break;
        case GI_TYPE_TAG_INT32:
            giArg->v_int32 = (int32_t)AS_NUMBER(loxValue);
            break;
        case GI_TYPE_TAG_FLOAT:
            giArg->v_float = (float)AS_NUMBER(loxValue);
            break;
        case GI_TYPE_TAG_DOUBLE:
            giArg->v_double = AS_NUMBER(loxValue);
            break;
        case GI_TYPE_TAG_BOOLEAN:
            giArg->v_boolean = AS_BOOL(loxValue);
            break;
        case GI_TYPE_TAG_INTERFACE:
            if (IS_INSTANCE(loxValue)) {
                giArg->v_pointer = AS_INSTANCE(loxValue)->foreignPtr;
            } else if (IS_NUMBER(loxValue)) {
                giArg->v_int = (int)AS_NUMBER(loxValue);
            } else {
                giArg->v_pointer = NULL;
            }
            break;
        default:
            printf("[WARNING]: Unsupported GI Type Tag: %d\n", tag);
            giArg->v_pointer =  NULL;
    }
}

static Value giClassCallHandler(int argCount, Value* args) {
    //printf("[DEBUG] In giClassCallHandler\n");
    ObjClass* klass = AS_CLASS(args[-1]);

    if (klass->foreignData == NULL) {
        runtimeError("Class %s has no GI metadata.", klass->name->chars);
        return NIL_VAL;
    }

    GIBaseInfo* info = (GIBaseInfo*)klass->foreignData;
    const char* type_init = g_object_info_get_type_init(info);
    GType gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)info);

    if (gtype == G_TYPE_INVALID || gtype == 0) {
        runtimeError("Invalid GType for class %s.", klass->name->chars);
        return NIL_VAL;
    }

    GIFunctionInfo* constructor = NULL;
    GObject* gptr = NULL;

    int n_methods = (g_base_info_get_type(info) == GI_INFO_TYPE_OBJECT)
        ? g_object_info_get_n_methods((GIObjectInfo*)info)
        : g_struct_info_get_n_methods((GIStructInfo*)info);

    for (int i = 0; i < n_methods; i++) {
        GIFunctionInfo* fn = (g_base_info_get_type(info) == GI_INFO_TYPE_OBJECT)
            ? g_object_info_get_method((GIObjectInfo*)info, i)
            : g_struct_info_get_method((GIStructInfo*)info, i);
        const char* fn_name = g_base_info_get_name((GIBaseInfo*)fn);

        if (g_function_info_get_flags(fn) & GI_FUNCTION_IS_CONSTRUCTOR ||
                (strcmp(fn_name, "new") == 0)) {
            constructor = fn;
            break;
        }
        g_base_info_unref(fn);
    }

    if (constructor) {
        int n_metadata_args = g_callable_info_get_n_args((GICallableInfo*)constructor);

        int total_args = n_metadata_args;
        GIArgument* in_args = malloc(sizeof(GIArgument) * (total_args > 0 ? total_args : 1));

        GIArgument retval = {.v_pointer = NULL };;
        GError* error = NULL;
        
        for (int i = 0; i < n_metadata_args; i++) {
            GIArgInfo* arg_info = g_callable_info_get_arg((GICallableInfo*)constructor, i);
            GITypeInfo* type_info = g_arg_info_get_type(arg_info);

            if (i < argCount) {
                convertLoxToGI(args[i], &in_args[i], type_info);
            } else {
                in_args[i].v_pointer = NULL;
            }

            g_base_info_unref((GIBaseInfo*)type_info);
            g_base_info_unref((GIBaseInfo*)arg_info);
        }

        //GIArgument retval = {0};
        //GError* error = NULL;

        //printf("[DEBUG] Calling constructor: %s\n", g_base_info_get_name((GIBaseInfo*)constructor));

        g_function_info_invoke(constructor, in_args, total_args, NULL, 0, &retval, &error);


        free(in_args);

        if (error) {
            //printf("[DEBUG] Constructor Error: %s\n", error->message);
            g_error_free(error);
        }

        //printf("[DEBUG] Constructor returned pointer: %p\n", retval.v_pointer);

        ObjInstance* instance = newInstance(klass);
        instance->foreignPtr = retval.v_pointer;

        /*
        if (n_metadata_args == 0 && argCount > 0) {
            if (IS_STRING(args[0])) {
                ObjString* title_key = copyString("title", 5);
                giPropertySetter(instance, title_key, args[0]);
            }
        }
        */
        g_base_info_unref(constructor);
        return OBJ_VAL(instance);
        //instance->foreignPtr = gptr;
        /*
        if (gptr == NULL) {
            runtimeError("Failed to allocate GObject for %s", klass->name->chars);
            return NIL_VAL;
        }
        return OBJ_VAL(instance);
        */
    } else {
        //printf("[DEBUG] No constructor found for %s, falling back to g_object_new\n", klass->name->chars);

        gptr = g_object_new(gtype, NULL);

        ObjInstance* instance = newInstance(klass);
        instance->foreignPtr = gptr;

        //printf("[DEBUG] g_object_new_returned: %p\n", instance->foreignPtr);
        return OBJ_VAL(instance);
    }
}

static Value giInvokeNative(int argCount, Value* args) {
    ObjNative* native = AS_NATIVE_OBJ(args[-1]);
    GIFunctionInfo* fn_info = (GIFunctionInfo*)native->foreignData;

    bool is_method = (g_function_info_get_flags(fn_info) & GI_FUNCTION_IS_METHOD) != 0;

    // 1. get metadata count
    int n_metadata_args = g_callable_info_get_n_args((GICallableInfo*)fn_info);
    // 2. the total arguments we pass
    int total_gi_args = is_method ? n_metadata_args + 1 : n_metadata_args;

    // allocate the giargument array
    GIArgument* in_args = malloc(sizeof(GIArgument) * (total_gi_args > 0 ? total_gi_args : 1));
    int gi_idx = 0;

    // 3. handle instance (this)
    if (is_method) {
        if (!IS_INSTANCE(args[0])) {
            //printf("[ERROR] args[0] is not an instance! Type tag: %d\n", AS_OBJ(args[0])->type);
            in_args[0].v_pointer = NULL;
        } else {
            ObjInstance* instance = AS_INSTANCE(args[0]);
            in_args[gi_idx++].v_pointer = instance->foreignPtr;

            //printf("[DEBUG] Extracted pointer from Instance: %p\n", in_args[0].v_pointer);
        }
    }

    // 4. handle logical args
    for (int i = 0; i < argCount; i++) {
        GIArgInfo* arg_info = g_callable_info_get_arg((GICallableInfo*)fn_info, i);
        if (arg_info == NULL) continue;

        GITypeInfo* type_info = g_arg_info_get_type(arg_info);

        int lox_idx = is_method ? i + 1 : i;

        if (lox_idx < argCount) {
            convertLoxToGI(args[lox_idx], &in_args[gi_idx++], type_info);
        } else {
            in_args[gi_idx++].v_pointer = NULL;
        }

        g_base_info_unref((GIBaseInfo*)type_info);
        g_base_info_unref((GIBaseInfo*)arg_info);
    }

    GIArgument return_val;
    GError* error = NULL;

    //printf("[DEBUG] First GI Arg (this): %p\n", in_args[0].v_pointer);

    g_function_info_invoke(fn_info,
        in_args, total_gi_args,
        NULL, 0,
        &return_val, &error);

    free(in_args);

    if (error) {
        //printf("[INVOKE]: GI Runtime Error: %s\n", error->message);
        g_error_free(error);
    } else {
        const char* name = g_base_info_get_name((GIBaseInfo*)fn_info);
        //printf("[INVOKE]: Successfully called '%s' on %p\n", name, in_args[0].v_pointer);
    }

    return NIL_VAL;
}

static void registerGIMethod(ObjClass* klass, GIBaseInfo* info) {
    const char* fn_name = g_base_info_get_name(info);
    ObjString* method_name = copyString(fn_name, strlen(fn_name));
    push(OBJ_VAL(method_name));

    Value existing;
    if (!tableGet(&klass->methods, method_name, &existing)) {
        ObjNative* native = newNative(giInvokeNative);
        native->foreignData = g_base_info_ref(info);
        tableSet(&klass->methods, method_name, OBJ_VAL(native));
    }
    pop();
}

static void loadMethodsIntoClass(ObjClass* klass, GIBaseInfo* info) {
    GIInfoType type = g_base_info_get_type(info);

    if (type == GI_INFO_TYPE_OBJECT) {
        GIObjectInfo* current_info = (GIObjectInfo*)g_base_info_ref(info);
        while (current_info != NULL) {
            int n_methods = g_object_info_get_n_methods(current_info);
            for (int i = 0; i < n_methods; i++) {
                GIFunctionInfo* fn_info = g_object_info_get_method(current_info, i);
                registerGIMethod(klass, fn_info);
                g_base_info_unref(fn_info);
            }
            GIObjectInfo* parent = g_object_info_get_parent(current_info);
            g_base_info_unref((GIBaseInfo*)current_info);
            current_info = parent;
        }
    } else if (type == GI_INFO_TYPE_STRUCT) {
        int n_methods = g_struct_info_get_n_methods((GIStructInfo*)info);
        for (int i = 0; i < n_methods; i++) {
            GIFunctionInfo* fn_info = g_struct_info_get_method((GIStructInfo*)info, i);
            registerGIMethod(klass, fn_info);
            g_base_info_unref(fn_info);
        }
    }
}

static Value giLoopNative(int argcCount, Value* args) {
    GMainLoop* loop = g_main_loop_new(NULL, false);

    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    return NIL_VAL;
}

static Value giLoadNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("gi.load() expects 1 string argument (the namespace).");
        return NIL_VAL;
    }

    const char* namespace = AS_CSTRING(args[0]);
    GError* error = NULL;

    g_irepository_require(NULL, namespace, NULL, 0, &error);
    if (error) {
        runtimeError("GI Load Error: %s", error->message);
        g_error_free(error);
        return NIL_VAL;
    }


    ObjInstance* module = newInstance(vm.moduleClass);
    push(OBJ_VAL(module)); // [1] module
    initTable(&module->fields);

    /*
    ObjInstance* registry = newInstance(vm.moduleClass);
    tableSet(&module->fields, copyString("__registry", 10), OBJ_VAL(registry));
    globalRegistry = registry;
    */

    int n_infos = g_irepository_get_n_infos(NULL, namespace);

    for (int i = 0; i < n_infos; i++) {
        GIBaseInfo* info = g_irepository_get_info(NULL, namespace, i);
        const char* name = g_base_info_get_name(info);
        GIInfoType type = g_base_info_get_type(info);

        if (type == GI_INFO_TYPE_FUNCTION) {
            ObjNative* native = newNative(giInvokeNative);
            push(OBJ_VAL(native));

            native->foreignData = g_base_info_ref(info);

            tableSet(&module->fields, copyString(name, strlen(name)), OBJ_VAL(native));

            pop();
        } else if (type == GI_INFO_TYPE_OBJECT || type == GI_INFO_TYPE_STRUCT) {
            ObjString* className = copyString(name, strlen(name));
            push(OBJ_VAL(className));

            const char* namespace = g_base_info_get_namespace(info);

            int fullLen = strlen(namespace) + strlen(name) + 1;
            char* fullTypeName = malloc(fullLen);
            snprintf(fullTypeName, fullLen, "%s%s", namespace, name);


            ObjClass* klass = newClass(className);
            push(OBJ_VAL(klass));

            klass->foreignData = g_base_info_ref(info);
            klass->callHandler = giClassCallHandler;
            klass->getter = giPropertyGetter;
            klass->setter = giPropertySetter;

            const char* typeName = g_base_info_get_name(info);
            if (strcmp(namespace, "GObject") == 0) {
                snprintf(fullTypeName, strlen(name), "%s", name);
            }
            tableSet(&globalRegistry->fields, copyString(fullTypeName, strlen(fullTypeName)),
                    OBJ_VAL(klass));

            initTable(&klass->methods);

            loadMethodsIntoClass(klass, info);

            tableSet(&module->fields, className, OBJ_VAL(klass));

            printf("[LOADER]: Registered Class '%s' (Internal: %s)\n", name, fullTypeName);
            free(fullTypeName);

            pop();
            pop();
        }

        if (type == GI_INFO_TYPE_ENUM) {
            ObjClass* enumClass = newClass(copyString(name, strlen(name)));
            push(OBJ_VAL(enumClass));

            ObjInstance* enumInstance = newInstance(enumClass);
            push(OBJ_VAL(enumInstance));

            int n_values = g_enum_info_get_n_values((GIEnumInfo*)info);
            for (int i = 0; i < n_values; i++) {
                GIValueInfo* val_info = g_enum_info_get_value((GIEnumInfo*)info, i);
                const char* val_name = g_base_info_get_name((GIBaseInfo*)val_info);
                int64_t val = g_value_info_get_value(val_info);

                tableSet(&enumInstance->fields, copyString(val_name, strlen(val_name)), NUMBER_VAL((double)val));
                g_base_info_unref(val_info);
            }

            tableSet(&module->fields, enumClass->name, OBJ_VAL(enumInstance));
            pop(); // instance
            pop(); // class
        }
        g_base_info_unref(info);
    }
    return pop();
}

void lox_module_init(VM* vm) {
    initGiModule();

    ObjInstance* giModule = newInstance(vm->moduleClass);
    push(OBJ_VAL(giModule)); // [1] instance

    ObjNative* loadFn = newNative(giLoadNative);
    push(OBJ_VAL(loadFn)); // [2] native

    ObjString* loadStr = copyString("load", 4);
    push(OBJ_VAL(loadStr)); // [3] string

    tableSet(&giModule->fields, loadStr, OBJ_VAL(loadFn));
    pop(); // [3]

    ObjString* giStr = copyString("gi", 2);
    push(OBJ_VAL(giStr)); // [3] string
    tableSet(&vm->globals, giStr, OBJ_VAL(giModule));
    globalRegistry = newInstance(vm->moduleClass);
    tableSet(&giModule->fields, copyString("__registry", 16), OBJ_VAL(globalRegistry));

    /*
    static Table* registryTable = NULL;
    registryTable = &internalRegistry->fields;
    */

    ObjNative* loopFn = newNative(giLoopNative);
    push(OBJ_VAL(loopFn)); // [4] loop

    ObjString* loopStr = copyString("loop", 4);
    push(OBJ_VAL(loopStr)); // [5] loop

    tableSet(&giModule->fields, loopStr, OBJ_VAL(loopFn));
    pop(); // [5]
    pop(); // [4]

    pop(); // [3]

    ObjNative* inspectFn = newNative(giInspectNative);
    push(OBJ_VAL(inspectFn));

    ObjString* inspectStr = copyString("inspect", 7);
    push(OBJ_VAL(inspectStr));

    tableSet(&giModule->fields, inspectStr, OBJ_VAL(inspectFn));
    pop();
    pop();

    tableSet(&giModule->fields,
            copyString("connect", 7),
            OBJ_VAL(newNative(giConnectNative)));

    pop(); // [2]
    pop(); // [1]
}
