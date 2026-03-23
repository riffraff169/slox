#include <stdio.h>
#include <girepository.h>
#include "../vm.h"


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
        default:
            printf("[WARNING]: Unsupported GI Type Tag: %d\n", tag);
            giArg->v_pointer =  NULL;
    }
}

static Value giClassCallHandler(int argCount, Value* args) {
    printf("[DEBUG] In giClassCallHandler\n");
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
        GIArgument retval = {.v_pointer = NULL };;
        GError* error = NULL;
        
        printf("[DEBUG] Calling constructor: %s\n", g_base_info_get_name((GIBaseInfo*)constructor));

        g_function_info_invoke(constructor, NULL, 0, NULL, 0, &retval, &error);

        if (error) {
            printf("[DEBUG] Constructor Error: %s\n", error->message);
            g_error_free(error);
        }

        printf("[DEBUG] Constructor returned pointer: %p\n", retval.v_pointer);

        ObjInstance* instance = newInstance(klass);
        instance->foreignPtr = retval.v_pointer;

        g_base_info_unref(constructor);

        gptr = g_object_new(gtype, NULL);

        //instance->foreignPtr = gptr;
        if (gptr == NULL) {
            runtimeError("Failed to allocate GObject for %s", klass->name->chars);
            return NIL_VAL;
        }
        return OBJ_VAL(instance);
    } else {
        printf("[DEBUG] No constructor found for %s, falling back to g_object_new\n", klass->name->chars);

        ObjInstance* instance = newInstance(klass);
        instance->foreignPtr = g_object_new(gtype, NULL);

        printf("[DEBUG] g_object_new_returned: %p\n", instance->foreignPtr);
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
            printf("[ERROR] args[0] is not an instance! Type tag: %d\n", AS_OBJ(args[0])->type);
            in_args[0].v_pointer = NULL;
        } else {
            ObjInstance* instance = AS_INSTANCE(args[0]);
            in_args[gi_idx++].v_pointer = instance->foreignPtr;

            printf("[DEBUG] Extracted pointer from Instance: %p\n", in_args[0].v_pointer);
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

    /*
    for (int i = 0; i < n_metadata_args; i++) {
        GIArgInfo* arg_info = g_callable_info_get_arg((GICallableInfo*)fn_info, i);
        GITypeInfo* type_info = g_arg_info_get_type(arg_info);
        GITypeTag tag = g_type_info_get_tag(type_info);

        int lox_idx = is_method ? i + 1 : i;

        if (lox_idx >= argCount) {
            in_args[gi_idx++].v_pointer = NULL;
            continue;
        }

        switch (tag) {
            case GI_TYPE_TAG_UTF8:
                in_args[gi_idx++].v_string = (char*)AS_CSTRING(args[lox_idx]);
                break;
            case GI_TYPE_TAG_INTERFACE:
                if (IS_INSTANCE(args[lox_idx])) {
                    in_args[gi_idx++].v_pointer = AS_INSTANCE(args[lox_idx])->foreignPtr;
                } else {
                    in_args[gi_idx++].v_pointer = NULL;
                }
                break;
            case GI_TYPE_TAG_INT32:
            case GI_TYPE_TAG_UINT32:
                in_args[gi_idx++].v_int32 = (int32_t)AS_NUMBER(args[lox_idx]);
                break;
            case GI_TYPE_TAG_BOOLEAN:
                in_args[gi_idx++].v_boolean = AS_BOOL(args[lox_idx]);
                break;
            default:
                in_args[gi_idx++].v_pointer = NULL;
                break;
        }

        g_base_info_unref((GIBaseInfo*)type_info);
        g_base_info_unref((GIBaseInfo*)arg_info);
    }
    */

    GIArgument return_val;
    GError* error = NULL;

    printf("[DEBUG] First GI Arg (this): %p\n", in_args[0].v_pointer);

    g_function_info_invoke(fn_info,
        in_args, total_gi_args,
        NULL, 0,
        &return_val, &error);

    free(in_args);

    if (error) {
        printf("[INVOKE]: GI Runtime Error: %s\n", error->message);
        g_error_free(error);
    } else {
        const char* name = g_base_info_get_name((GIBaseInfo*)fn_info);
        printf("[INVOKE]: Successfully called '%s' on %p\n", name, in_args[0].v_pointer);
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

            ObjClass* klass = newClass(className);
            push(OBJ_VAL(klass));

            klass->foreignData = g_base_info_ref(info);
            klass->callHandler = giClassCallHandler;

            initTable(&klass->methods);

            loadMethodsIntoClass(klass, info);

            tableSet(&module->fields, className, OBJ_VAL(klass));

            printf("[LOADER]: Registered Class '%s' | Info: %p\n", name, klass->foreignData);

            pop();
            pop();
        }
        g_base_info_unref(info);
    }
    return pop();
}

void lox_module_init(VM* vm) {
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
    pop(); // [3]

    pop(); // [2]
    pop(); // [1]
}
