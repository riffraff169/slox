#include <stdio.h>
#include <girepository.h>
#include "../vm.h"

Value gArgumentToLox(GITypeInfo* type_info, GArgument* arg);
void loxToGArgument(GITypeInfo* type_info, Value loxValue, GArgument* arg);

void loxToGArgument(GITypeInfo* type_info, Value loxValue, GArgument* arg) {
    GITypeTag tag = g_type_info_get_tag(type_info);

    switch (tag) {
        case GI_TYPE_TAG_VOID:
            arg->v_pointer = NULL;
            break;
        case GI_TYPE_TAG_BOOLEAN:
            arg->v_boolean = AS_BOOL(loxValue);
            break;
        case GI_TYPE_TAG_INT32:
            arg->v_int32 = (int32_t)AS_NUMBER(loxValue);
            break;
        case GI_TYPE_TAG_DOUBLE:
            arg->v_double = AS_NUMBER(loxValue);
            break;
        case GI_TYPE_TAG_UTF8:
            {
                if (IS_STRING(loxValue)) {
                    arg->v_string = AS_CSTRING(loxValue);
                } else {
                    arg->v_string = NULL;
                }
            }
            break;
        case GI_TYPE_TAG_INTERFACE:
            {
                GIBaseInfo* interface_info = g_type_info_get_interface(type_info);
                GIInfoType interface_type = g_base_info_get_type(interface_info);

                if (interface_type == GI_INFO_TYPE_OBJECT || interface_type == GI_INFO_TYPE_STRUCT) {
                    if (IS_INSTANCE(loxValue)) {
                        arg->v_pointer = AS_INSTANCE(loxValue)->foreignPtr;
                    } else {
                        arg->v_pointer = NULL;
                    }
                }
                g_base_info_unref(interface_info);
            }
            break;
        default:
            fprintf(stderr, "Unhandled GI type tag: %d\n", tag);
            arg->v_pointer = NULL;
            break;
    }
}

Value gArgumentToLox(GITypeInfo* type_info, GArgument* arg) {
    GITypeTag tag = g_type_info_get_tag(type_info);

    switch (tag) {
        case GI_TYPE_TAG_VOID:
            return NIL_VAL;
        case GI_TYPE_TAG_BOOLEAN:
            return BOOL_VAL(arg->v_boolean);
        case GI_TYPE_TAG_INT32:
            return NUMBER_VAL((double)arg->v_int32);
        case GI_TYPE_TAG_UTF8:
            return OBJ_VAL(copyString(arg->v_string, strlen(arg->v_string)));
        case GI_TYPE_TAG_INTERFACE:
            /*
            {
                GIBaseInfo* interface = g_type_info_get_interface(type_info);
                GIInfoType type = g_base_info_get_type(interface);

                if (type == GI_INFO_TYPE_OBJECT || type == GI_INFO_TYPE_STRUCT) {
                    return OBJ_VAL(newForeign(arg->v_pointer, g_base_info_get_name(interface)));
                }
                return NIL_VAL;
            }
            */
            //return wrapForeignPointer(arg->v_pointer, type_info);
        default:
            return NIL_VAL;
    }
}

/*
static ObjNative* createGiNative(GIBaseInfo* info) {
    ObjNative* native = newNative(giInvokeNative);
    native->foreignData = g_base_info_ref(info);
    return native;
}
*/

static void convertLoxToGI(Value loxValue, GIArgument* giArg, GITypeInfo* type_info) {
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
            giArg->v_pointer = NULL;
    }
}

static Value giClassCallHandler(int argCount, Value* args) {
    // args[-1] is the class object (Window)
    ObjClass* klass = AS_CLASS(args[-1]);

    if (klass->foreignData == NULL) {
        runtimeError("Class %s has no GI metadata.", klass->name->chars);
        return NIL_VAL;
    }

    printf("[CONSTRUCTOR]: Creating instance of '%s'\n", klass->name->chars);
    printf("[CONSTRUCTOR]: Metadata at: %p\n", klass->foreignData);

    // 1. Get the GType from the metadata we saved in the loader
    GIObjectInfo* obj_info = (GIObjectInfo*)klass->foreignData;
    GType gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)obj_info);

    printf("[DEBUG]: Attemptingto instantiate %s. GType ID: %lu\n",
            klass->name->chars, (unsigned long)gtype);

    if (gtype == G_TYPE_INVALID || gtype == 0) {
        runtimeError("Invalid GType for class %s.", klass->name->chars);
        return NIL_VAL;
    }

    if (!G_TYPE_IS_INSTANTIATABLE(gtype)) {
        runtimeError("GType %s is not isntantiable.", g_type_name(gtype));
        return NIL_VAL;
    }

    printf("[CONSTRUCTOR]: Allocating GType '%s' (%lu)\n",
            g_type_name(gtype), (unsigned long)gtype);

    // 2. instantiate the gobject
    // for now, we pass NULL for properties to keep it simple
    GObject* gptr = g_object_new(gtype, NULL);

    if (gptr == NULL) {
        runtimeError("Failed to allocated GObject for %s", klass->name->chars);
        return NIL_VAL;
    }

    // 3. create the lox instance and wire it up
    ObjInstance* instance = newInstance(klass);
    instance->foreignPtr = gptr;

    printf("[CONSTRUCTOR]: Success! Instance %p wraps GObject %p\n",
            (void*)instance, (void*)gptr);

    return OBJ_VAL(instance);
}

/*
static Value giInvokeNative(int argCount, Value* args) {
    
    Value nativeValue = args[-1];
    printf("[DEBUG] -- Invoke Start ---\n");
    printf("[DEBUG] Native Obj (args[-1]): ");
    printValue(nativeValue);
    printf(" | Type Tag: %d\n", AS_OBJ(nativeValue)->type);

    printf("[DEBUG] args[-1]: "); printValue(args[-1]); printf("\n");
    printf("[DEBUG] args[-2]: "); printValue(args[-2]); printf("\n");

    if (IS_NATIVE(nativeValue)) {
        //ObjNative* native = AS_NATIVE_OBJ(args[-1]);
        ObjNative* native = AS_NATIVE_OBJ(nativeValue);
        printf("[DEBUG] Info Ptr: %p\n", native->foreignData);
    }

    for (int i = 0; i < argCount; i++) {
        printf("[DEBUG] Arg[%d]: ", i);
        printValue(args[i]);
        if (IS_INSTANCE(args[i])) printf(" (Instance)");
        if (IS_STRING(args[i])) printf(" (String: %s)", AS_CSTRING(args[i]));
        if (IS_NUMBER(args[i])) printf(" (Number: %g)", AS_NUMBER(args[i]));
        printf("\n");
    }
    printf("[DEBUG] ---------------------\n");
    

    ObjNative* native = AS_NATIVE_OBJ(args[-1]);
    GIFunctionInfo* fn_info = (GIFunctionInfo*)native->foreignData;

    //printf("[INVOKE]: Function called! Info Ptr: %p\n", native->foreignData);

    if (native->foreignData == NULL) {
        printf("[INVOKE]: ERROR - foreignData is NULL!\n");
    } 

    // 1. identify if this is a method call (needs an isntance)
    GIFunctionInfoFlags flags = g_function_info_get_flags(fn_info);
    bool is_method = (flags & GI_FUNCTION_IS_METHOD) != 0;

    // 2. setup arguments
    int n_params = g_callable_info_get_n_args((GICallableInfo*)fn_info);
    int total_gi_args = is_method ? n_params + 1: n_params;

    GIArgument* in_args = malloc(sizeof(GIArgument) * total_gi_args);
    int gi_idx = 0;

    // 3. handle the 'this' instance (eg the window pointer)
    if (is_method) {
        // in OP_INVOKE, the instance is at args[0]
        ObjInstance* instance = AS_INSTANCE(args[0]);
        in_args[gi_idx++].v_pointer = instance->foreignPtr;
    }

    // 4. convert lox args to GIArguments
    for (int i = 0; i < argCount; i++) {
        GIArgInfo* arg_info = g_callable_info_get_arg((GICallableInfo*)fn_info, i);
        GITypeInfo* type_info = g_arg_info_get_type(arg_info);

        int lox_idx = is_method ? i + 1 : i;

        convertLoxToGI(args[lox_idx], &in_args[gi_idx++], type_info);

        g_base_info_unref((GIBaseInfo*)type_info);
        g_base_info_unref((GIBaseInfo*)arg_info);
    }

    GIArgument return_val;
    GError* error = NULL;

    g_function_info_invoke(fn_info, in_args, total_gi_args, NULL, 0, &return_val, &error);

    free(in_args);

    if (error) {
        runtimeError("GI Runtime Error: %s", error->message);
        g_error_free(error);
    }

    printf("[INVOKE]: end\n");
    return NIL_VAL;
}
*/

static Value giInvokeNative(int argCount, Value* args) {
    ObjNative* native = AS_NATIVE_OBJ(args[-1]);
    GIFunctionInfo* fn_info = (GIFunctionInfo*)native->foreignData;

    // 1. identify if we are calling a method (requires instance at args[0])
    bool is_method = (g_function_info_get_flags(fn_info) & GI_FUNCTION_IS_METHOD) != 0;

    // total_gi_args is what we pass to c
    // n_metadata_args is what we read from the typelib
    int n_metadata_args = g_callable_info_get_n_args((GICallableInfo*)fn_info);
    int total_gi_args = is_method ? n_metadata_args + 1 : n_metadata_args;

    // 2. setup up gi argument array
    GIArgument* in_args = malloc(sizeof(GIArgument) * total_gi_args);
    int gi_idx = 0;

    if (is_method) {
        // map the lox instance to the c gobject pointer
        ObjInstance* instance = AS_INSTANCE(args[0]);
        in_args[gi_idx++].v_pointer = instance->foreignPtr;
    }

    /*
    for (int i = 0; i < argCount; i++) {
        printf("[DEBUG] Arg[%d]: ", i);
        printValue(args[i]);
        if (IS_INSTANCE(args[i])) printf(" (Instance)");
        if (IS_STRING(args[i])) printf(" (String: %s)", AS_CSTRING(args[i]));
        if (IS_NUMBER(args[i])) printf(" (Number: %g)", AS_NUMBER(args[i]));
        printf("\n");
    }
    printf("[DEBUG] ---------------------\n");
    */
    /*
    // convert lox args to GIArguments
    for (int i = 0; i < argCount; i++) {
        GIArgInfo* arg_info = g_callable_info_get_arg((GICallableInfo*)fn_info, i);
        GITypeInfo* type_info = g_arg_info_get_type(arg_info);

        int lox_idx = is_method ? i + 1 : i;
        convertLoxToGI(args[lox_idx], &in_args[gi_idx++], type_info);

        g_base_info_unref((GIBaseInfo*)type_info);
        g_base_info_unref((GIBaseInfo*)arg_info);
    }
    */

    if (is_method) {
        ObjInstance* instance = AS_INSTANCE(args[0]);
        in_args[gi_idx++].v_pointer = instance->foreignPtr;

        /*
        if (argCount > 1 && IS_STRING(args[1])) {
            in_args[1].v_string = (char*)AS_CSTRING(args[1]);
        }
        */
    } //else {

    for (int i = 0; i < n_metadata_args; i++) {
        GIArgInfo* arg_info = g_callable_info_get_arg((GICallableInfo*)fn_info, i);
        GITypeInfo* type_info = g_arg_info_get_type(arg_info);
        GITypeTag tag = g_type_info_get_tag(type_info);

        int lox_idx = is_method ? i + 1 : i;

        //convertLoxToGI(args[lox_idx], &in_args[gi_idx++], type_info);

        // we ran out of lox arguments, but gi wants more
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

    GIArgument return_val;
    GError* error = NULL;

    g_function_info_invoke(fn_info,
            in_args, total_gi_args,
            NULL, 0,
            &return_val, &error);


    free(in_args);

    if (error) {
        //runtimeError("GI Runtime Error: %s", error->message);
        printf("[INVOKE] GTK Error: %s\n", error->message);
        g_error_free(error);
    } else {
        const char* name = g_base_info_get_name((GIBaseInfo*)fn_info);
        printf("[INVOKE] Successfully called '%s' on %p\n", name, in_args[0].v_pointer);
    }

    return NIL_VAL;
}

static Value giLoadNative(int argCount, Value* args) {
    const char* namespace = AS_CSTRING(args[0]);
    printf("[LIB]: namespace: %s\n", namespace);

    GError* error = NULL;

    g_irepository_require(NULL, namespace, NULL, 0, &error);
    if (error) {
        runtimeError("GI Load Error: %s", error->message);
        g_error_free(error);
        return NIL_VAL;
    }

    printf("[LIB]: get namespace\n");

    ObjInstance* module = newInstance(vm.moduleClass);
    push(OBJ_VAL(module)); // [1] module
    initTable(&module->fields);

    // get the total number of symbols in the gtk namespace
    int n_infos = g_irepository_get_n_infos(NULL, namespace);
    printf("[LIB]: Found %d symbols in %s\n", n_infos, namespace);

    tableSet(&module->fields, copyString("count", 5), NUMBER_VAL(n_infos));
    tableSet(&module->fields, copyString("version", 7), NUMBER_VAL(1.0));

    for (int i = 0; i < n_infos; i++) {
        GIBaseInfo* info = g_irepository_get_info(NULL, namespace, i);
        GIInfoType type = g_base_info_get_type(info);
        const char* name = g_base_info_get_name(info);

        if (type == GI_INFO_TYPE_FUNCTION) {
            // 1. create the native wrapper pointing to our common invoker
            ObjNative* native = newNative(giInvokeNative);
            push(OBJ_VAL(native));

            // 2. the critical pointer storage
            // take a reference to the gi metadata so it stays alive
            native->foreignData = g_base_info_ref(info);

            // 3. attach it to the gtk module instance fields
            tableSet(&module->fields, copyString(name, strlen(name)), OBJ_VAL(native));

            pop();
        } else if (type == GI_INFO_TYPE_OBJECT) {
            ObjString* className = copyString(name, strlen(name));
            push(OBJ_VAL(className));

            // 1. create the lox class
            ObjClass* klass = newClass(className);
            klass->callHandler = giClassCallHandler;
            push(OBJ_VAL(klass));

            // 2. init the methods table (critical)
            initTable(&klass->methods);

            // 3. store the metadata in the class itself
            // we'll need this later to know which GType to instantiate
            klass->foreignData = g_base_info_ref(info);

            // 4. attach the class to the module
            tableSet(&module->fields, className, OBJ_VAL(klass));

            printf("[LOADER]: Registered Class '%s' | Info: %p\n", name, klass->foreignData);

            GIObjectInfo* current_info = (GIObjectInfo*)info;
            g_base_info_ref((GIBaseInfo*)current_info);

            
            while (current_info != NULL) {
                int n_methods = g_object_info_get_n_methods(current_info);
                for (int i = 0; i < n_methods; i++) {
                    //GIFunctionInfo* fn_info = g_object_info_get_method(current_info, i);
                    GIBaseInfo* fn_info = g_object_info_get_method(current_info, i);
                    const char* fn_name = g_base_info_get_name((GIBaseInfo*)fn_info);

                    // only add if the method doesn't already exist (respecting overrides)
                    ObjString* method_name = copyString(fn_name, strlen(fn_name));
                    //push(OBJ_VAL(method_name));

                    Value existing;

                    if (!tableGet(&klass->methods, method_name, &existing)) {
                        ObjNative* native = newNative(giInvokeNative);
                        push(OBJ_VAL(native));

                        native->foreignData = g_base_info_ref(fn_info);
                        //printf("[LIB] foreignData = %p\n", native->foreignData);
                        tableSet(&klass->methods, method_name, OBJ_VAL(native));
                        pop();
                    }
                    g_base_info_unref(fn_info);
                    //pop();
                }

                GIObjectInfo* parent = g_object_info_get_parent(current_info);
                g_base_info_unref((GIBaseInfo*)current_info);
                current_info = parent;
            }
            

            tableSet(&module->fields, className, OBJ_VAL(klass));
            pop(); // klass
            pop(); // className
        }
        g_base_info_unref(info);
    }

    return pop(); // [1] module
}

void lox_module_init(VM* vm) {
    //gtk_init();

    // 1. create module instance
    // we use the vm.moduleClass we discussed earlier
    ObjInstance* giModule = newInstance(vm->moduleClass);

    // 2. wrap it in a value so the gc sees it
    push(OBJ_VAL(giModule));

    // 3. create the native function object
    ObjNative* loadFn = newNative(giLoadNative);
    push(OBJ_VAL(loadFn));

    ObjString* loadStr = copyString("load", 4);
    push(OBJ_VAL(loadStr));
    // 4. attach the function to the 'gi' instance fields
    // this makes it accessible via gi.load
    tableSet(&giModule->fields, loadStr, OBJ_VAL(loadFn));
    pop();

    // 5. define the gi instnace as a global variable in the vm
    ObjString* giStr = copyString("gi", 2);
    push(OBJ_VAL(giStr));
    tableSet(&vm->globals, giStr, OBJ_VAL(giModule));
    pop();

    pop();
    pop();

    //defineNative("load", giLoadNative);
    //defineNative("gtkWindowNew", gtkWindowNewNative);
    //defineNative("gtkWindowSetTitle", gtkWindowSetTitleNative);
    //defineNative("gtkWindowPresent", gtkWindowPresentNative);
    //defineNative("gtkWindowIsVisible", gtkWindowIsVisibleNative);
}
