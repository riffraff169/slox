#include <stdio.h>
#include <girepository.h>
#include <gdk/gdk.h>
#include "../vm.h"

//static Table keepAlive;
static ObjInstance* globalRegistry = NULL;
Value wrap_gobject_to_lox(GObject* obj);

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

Value scrape_glist_to_lox(GList* list, GType item_type) {
    ObjArray* array = newArray(0);
    push(OBJ_VAL(array));

    for (GList* l = list; l != NULL; l = l->next) {
        Value loxObj = wrap_gobject_to_lox(G_OBJECT(l->data));
        arrayAppend(array, loxObj);
    }

    pop();
    return OBJ_VAL(array);
}

/*
static void lox_builder_connect(GtkBuilder* builder, GObject* object,
                                const char* signal_name, const char* handler_name,
                                GObject* connect_object, GConnectFlags flags,
                                gpointer user_data) {
    // 1. Look up 'handler_name' (e.g., "on_button_clicked") in your Lox Global Table
    Value loxCallback;
    if (tableGet(&vm.globals, copyString(handler_name, strlen(handler_name)), &loxCallback)) {
        // 2. Create a closure and connect it using your generic marshaller
        GClosure* closure = g_closure_new_simple(sizeof(GClosure), (gpointer)AS_CLOSURE(loxCallback));
        g_closure_set_marshal(closure, lox_marshal_generic);
        g_signal_connect_closure(object, signal_name, closure, FALSE);
    }
}
*/

void LoxValueToGValue(Value loxVal, GValue* gval) {
    GType gtype = G_VALUE_TYPE(gval);

    if (g_type_is_a(gtype, G_TYPE_BOOLEAN)) {
        g_value_set_boolean(gval, AS_BOOL(loxVal));
    } else if (g_type_is_a(gtype, G_TYPE_INT)) {
        g_value_set_int(gval, (int)AS_NUMBER(loxVal));
    } else if (g_type_is_a(gtype, G_TYPE_UINT)) {
        g_value_set_uint(gval, (int)AS_NUMBER(loxVal));
    } else if (g_type_is_a(gtype, G_TYPE_DOUBLE)) {
        g_value_set_double(gval, AS_NUMBER(loxVal));
    } else if (g_type_is_a(gtype, G_TYPE_STRING)) {
        g_value_set_string(gval, AS_CSTRING(loxVal));
    } else if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
        if (IS_NIL(loxVal)) {
            g_value_set_object(gval, NULL);
        } else {
            g_value_set_object(gval, G_OBJECT(AS_INSTANCE(loxVal)->foreignPtr));
        }
    } else {
        printf("Warning: Unsupported return type GType %s\n", g_type_name(gtype));
    }
}

Value wrap_gobject_to_lox(GObject* obj) {
    if (obj == NULL) return NIL_VAL;
    if (obj && g_object_is_floating(obj)) {
        g_object_ref_sink(obj);
    }

    const char* typeName = G_OBJECT_TYPE_NAME(obj);
    ObjString* key = copyString(typeName, strlen(typeName));
    push(OBJ_VAL(key)); // push key onto stack

    Value klassValue;
    //printf("[DEBUG] In wrap_gobject_to_lox\n");

    //printf("[DEBUG] typeName: '%s'\n", key->chars);

    pop(); // remove key from stack

    //printf("[DEBUG] Value: ");
    //printValue(klassValue);
    //printf("\n");
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

/*
Value wrap_boxed_to_lox(gpointer boxed, GType type) {
    Value klassValue;
    const char* typeName = g_type_name(type);

    if (tableGet(&globalRegistry->fields, copyString(typeName, strlen(typeName)), &klassValue)) {
        ObjInstance* instance = newInstance(AS_CLASS(klassValue));

        instance->foreignPtr = g_boxed_copy(type, boxed);
        instance->isBoxed = true;

        return OBJ_VAL(instance);
    }
    return NIL_VAL;
}
*/

void giBoxedDestructor(ObjInstance* instance) {
    if (instance->foreignPtr != NULL) {
        g_boxed_free(GDK_TYPE_EVENT, instance->foreignPtr);
        instance->foreignPtr = NULL;
    }
}

Value scrape_boxed_to_lox(gpointer boxed, GType type) {
    Value eventKlassVal;
    if (!tableGet(&globalRegistry->fields, copyString("Event", 5), &eventKlassVal)) {
        return NIL_VAL;
    }

    ObjInstance* loxEvent = newInstance(AS_CLASS(eventKlassVal));
    push(OBJ_VAL(loxEvent));

    if (type == GDK_TYPE_EVENT) {
        GdkEvent* event = (GdkEvent*)boxed;

        GdkEventType evType = gdk_event_get_event_type(event);

        tableSet(&loxEvent->fields, copyString("type", 4), NUMBER_VAL(evType));

        if (evType == GDK_BUTTON_PRESS || evType == GDK_BUTTON_RELEASE ||
                evType == GDK_MOTION_NOTIFY) {
            double x, y;
            if (gdk_event_get_position(event, &x, &y)) {
                tableSet(&loxEvent->fields, copyString("x", 1), NUMBER_VAL(x));
                tableSet(&loxEvent->fields, copyString("y", 1), NUMBER_VAL(y));
            }
        }

        if (evType == GDK_KEY_PRESS || evType == GDK_KEY_RELEASE) {
            guint keyval = gdk_key_event_get_keyval(event);
            tableSet(&loxEvent->fields, copyString("keyval", 6), NUMBER_VAL((double)keyval));

            uint32_t unicode =  gdk_key_event_get_keycode(event);
            if  (unicode != 0) {
                char utf8[5] = {0};
                int len = g_unichar_to_utf8(unicode, utf8);
                tableSet(&loxEvent->fields, copyString("char", 4), OBJ_VAL(copyString(utf8, len)));
            } else {
                tableSet(&loxEvent->fields, copyString("char", 4), NIL_VAL);
            }
        }

        if (evType == GDK_BUTTON_PRESS) {
            guint button = gdk_button_event_get_button(event);
            tableSet(&loxEvent->fields, copyString("button", 6), NUMBER_VAL((double)button));
        }

        GdkModifierType state = gdk_event_get_modifier_state(event);
        tableSet(&loxEvent->fields, copyString("state", 5), NUMBER_VAL((double)state));
    } else if (type == GDK_TYPE_RGBA) {
        GdkRGBA* color = (GdkRGBA*)boxed;
        tableSet(&loxEvent->fields, copyString("red", 3), NUMBER_VAL(color->red));
        tableSet(&loxEvent->fields, copyString("green", 5), NUMBER_VAL(color->green));
        tableSet(&loxEvent->fields, copyString("blue", 4), NUMBER_VAL(color->blue));
        tableSet(&loxEvent->fields, copyString("alpha", 5), NUMBER_VAL(color->alpha));
    }

    pop();
    return OBJ_VAL(loxEvent);
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
            return NUMBER_VAL((double)g_value_get_int(&gval));
        case G_TYPE_FLOAT:
            return NUMBER_VAL((double)g_value_get_float(&gval));
        case G_TYPE_DOUBLE:
            return NUMBER_VAL(g_value_get_double(&gval));
        case G_TYPE_UINT:
            return NUMBER_VAL((double)g_value_get_uint(&gval));
        case G_TYPE_FLAGS:
            return NUMBER_VAL((double)g_value_get_flags(&gval));
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
            {
                gpointer boxed = g_value_get_boxed(&gval);
                if (boxed == NULL) return NIL_VAL;

                return scrape_boxed_to_lox(boxed, G_VALUE_TYPE(&gval));
            }
        case G_TYPE_ENUM:
            return NUMBER_VAL((double)g_value_get_enum(&gval));
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

    //printf("[DEBUG] Signal Marshaller triggered for closure %p\n", (void*)loxClosure);

    for (guint i = 0; i < n_params; i++) {
        push(GValueToLoxValue(params[i]));
    }

    vm.nativeExitDepth = vm.frameCount - 1;

    if (vmCall(loxClosure, n_params)) {
        run();
    }

    if (return_value != NULL && G_VALUE_TYPE(return_value) != G_TYPE_NONE) {
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

    //printf("[CONNECT] Closure connected\n");

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
    bool success = true;

    if (g_type_is_a(spec->value_type, G_TYPE_ENUM)) {
        if (IS_NUMBER(value))
            g_value_set_enum(&gval, (int)AS_NUMBER(value));
        else
            success = false;
    } else if (g_type_is_a(spec->value_type, G_TYPE_FLAGS)) {
        if (IS_NUMBER(value))
            g_value_set_flags(&gval, (unsigned int)AS_NUMBER(value));
        else
            success = false;
    } else if (g_type_is_a(spec->value_type, G_TYPE_STRING)) {
        if (IS_STRING(value))
            g_value_set_string(&gval, AS_CSTRING(value));
        else
            success = false;
    } else if (g_type_is_a(spec->value_type, G_TYPE_INT)) {
        if (IS_NUMBER(value))
            g_value_set_int(&gval, (int)AS_NUMBER(value));
        else
            success = false;
    } else if (g_type_is_a(spec->value_type, G_TYPE_DOUBLE)) {
        if (IS_NUMBER(value))
            g_value_set_double(&gval, AS_NUMBER(value));
        else
            success = false;
    } else if (g_type_is_a(spec->value_type, G_TYPE_BOOLEAN)) {
        if (IS_BOOL(value))
            g_value_set_boolean(&gval, AS_BOOL(value));
        else
            success = false;
    } else if (g_type_is_a(spec->value_type, G_TYPE_OBJECT)) {
        if (IS_INSTANCE(value)) {
            g_value_set_object(&gval, AS_INSTANCE(value)->foreignPtr);
        } else if (IS_NIL(value)) {
            g_value_set_object(&gval, NULL);
        } else
            success = false;
    } else {
        printf("Setter: Unhandled gtype %s, %lu\n", g_type_name(spec->value_type),
                (unsigned long)spec->value_type);
        success = false;
    }

    if (success) {
        g_object_set_property(G_OBJECT(instance->foreignPtr), name->chars, &gval);
    }

    g_value_unset(&gval);
    return success;
}

static Value giPropertyGetter(ObjInstance* instance, ObjString* name) {
    /*
    if (instance->isBoxed) {
        GdkEvent* event = (GdkEvent*)instance->foreignPtr;

        if (strcmp(name->chars, "x") == 0) {
            double x,y;
            gdk_event_get_coords(event, &x, &y);
            return NUMBER_VAL(x);
        }
        if (strcmp(name->chars, "keyval") == 0) {
            guint keyval;
            gdk_event_get_keyval(event, &keyval);
            return NUMBER_VAL(keyval);
        }
    }
    */

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

GObject* get_gobject_from_value(Value value) {
    if (!IS_INSTANCE(value)) return NULL;
    ObjInstance* instance = AS_INSTANCE(value);
    return instance->foreignPtr;
}

Value GIArgumentToLox(GIArgument* arg, GITypeInfo* type_info) {
    GITypeTag tag = g_type_info_get_tag(type_info);
    GValue gval = G_VALUE_INIT;

    switch (tag) {
        case GI_TYPE_TAG_INT32:
            g_value_init(&gval, G_TYPE_INT);
            g_value_set_int(&gval, arg->v_int32);
            break;
        case GI_TYPE_TAG_UINT32:
            g_value_init(&gval, G_TYPE_UINT);
            g_value_set_uint(&gval, arg->v_uint32);
            break;
        case GI_TYPE_TAG_BOOLEAN:
            g_value_init(&gval, G_TYPE_BOOLEAN);
            g_value_set_boolean(&gval, arg->v_boolean);
            break;
        case GI_TYPE_TAG_UTF8:
            g_value_init(&gval, G_TYPE_STRING);
            g_value_set_string(&gval, arg->v_string);
            break;
        case GI_TYPE_TAG_INTERFACE:
            {
                GIBaseInfo* interface = g_type_info_get_interface(type_info);
                GType gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface);

                if (g_type_is_a(gtype, G_TYPE_OBJECT)) {
                    g_value_init(&gval, G_TYPE_OBJECT);
                    g_value_set_object(&gval, arg->v_pointer);
                } else if (g_type_is_a(gtype, G_TYPE_ENUM) || g_type_is_a(gtype, G_TYPE_FLAGS)) {
                    g_value_init(&gval, gtype);
                    g_value_set_enum(&gval, arg->v_int32);
                }
                g_base_info_unref(interface);
            }
            break;
        case GI_TYPE_TAG_DOUBLE:
            g_value_init(&gval, G_TYPE_DOUBLE);
            g_value_set_double(&gval, arg->v_double);
            break;
        case GI_TYPE_TAG_FLOAT:
            g_value_init(&gval, G_TYPE_FLOAT);
            g_value_set_float(&gval, arg->v_float);
            break;
        default:
            return NIL_VAL;

    }

    Value result = GValueToLoxValue(gval);
    g_value_unset(&gval);
    return result;
}

void debugPrintGIType(GITypeInfo* type_info) {
    GITypeTag tag = g_type_info_get_tag(type_info);
    const char* tag_name = g_type_tag_to_string(tag);

    if (tag == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo* interface = g_type_info_get_interface(type_info);
        const char* name = g_base_info_get_name(interface);
        const char* ns = g_base_info_get_namespace(interface);
        printf("[DEBUG TYPE]: Interface (%s.%s)\n", ns, name);
    } else {
        printf("[DEBUG TYPE]: Basic Tag (%s)\n", tag_name);
    }
}

Value giInvokeNative(int argCount, Value* args) {
    ObjNative* native = AS_NATIVE_OBJ(args[-1]);
    GIFunctionInfo* info = (GIFunctionInfo*)native->foreignData;
    //const char* name = g_function_info_get_name(info);
    //printf("[INVOKE] function %s\n", name);
    GIFunctionInfoFlags flags = g_function_info_get_flags(info);

    bool is_method = (flags & GI_FUNCTION_IS_METHOD) != 0;
    bool is_constructor = (flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0;

    int lox_idx = 0;
    int gi_idx = 0;
    GObject* self = NULL;

    int n_args = g_callable_info_get_n_args((GICallableInfo*)info);
    GIArgument* in_args = alloca(sizeof(GIArgument) * n_args );

    printf("[INVOKE] n_args: %d\n", n_args);
    printf("[INVOKE] argCount: %d\n", argCount);

    if (is_method) {
        if (argCount > 0 && IS_INSTANCE(args[0])) {
            self = get_gobject_from_value(args[0]);
            printf("[DEBUG] Self pointer extracted: %p\n", self);
            in_args[gi_idx++].v_pointer = self;
            printValue(args[0]);
            printf("\n");
            //in_args[gi_idx++].v_pointer = get_gobject_from_value(args[0]);
            lox_idx = 1;
        }
    } else {
        if (argCount > n_args) {
            lox_idx = 1;
        }
    }

    if (argCount < n_args) {
        runtimeError("GI Error in %s: Wrong number of args\nWanted %d got %d\n", g_base_info_get_name(info), n_args, argCount);
        return NIL_VAL;
    }

    GIArgument return_value;
    GError* error = NULL;

    for (int i = 0; i < n_args; i++) {
        GIArgInfo* arg_info = g_callable_info_get_arg((GICallableInfo*)info, i);
        GITypeInfo* type_info = g_arg_info_get_type(arg_info);

        printf("Arg %d: ", i);
        debugPrintGIType(type_info);
        convertLoxToGI(args[lox_idx++], &in_args[gi_idx++], type_info);

        g_base_info_unref(type_info);
        g_base_info_unref(arg_info);
    }

    printf("[INVOKE] Required args: %d\n", n_args);
    printf("[INVOKE] Provided args: %d\n", argCount);
    printf("[INVOKE] gi_idx: %d\n", gi_idx);

    if (!g_function_info_invoke(info, in_args, gi_idx, NULL, 0, &return_value, &error)) {
        runtimeError("GI Error in %s: %s", g_base_info_get_name(info), error->message);
        g_error_free(error);
        return NIL_VAL;
    }

    printf("[INVOKE] after call\n");
    GITypeInfo* ret_type = g_callable_info_get_return_type((GICallableInfo*)info);
    Value result = GIArgumentToLox(&return_value, ret_type);
    //Value result = convertGIToLox(&return_value, ret_type);

    g_base_info_unref(ret_type);
    return result;
}

/*
static Value giInvokeNative(int argCount, Value* args) {
    ObjNative* native = AS_NATIVE_OBJ(args[-1]);
    GIFunctionInfo* fn_info = (GIFunctionInfo*)native->foreignData;

    GIFunctionInfoFlags flags = g_function_info_get_flags(info);
    bool is_method = (flags & GI_FUNCTION_IS_METHOD) != 0;

    // 1. get metadata count
    int n_metadata_args = g_callable_info_get_n_args((GICallableInfo*)fn_info);
    // 2. the total arguments we pass
    int total_gi_args = is_method ? n_metadata_args + 1 : n_metadata_args;

    int lox_arg_offset = 0;
    GObject* self = NULL;
    int lox_arg_start = is_method ? 1 : 0;
    int actual_arg_count = argCount - lox_arg_start;

    // allocate the giargument array
    GIArgument* in_args = malloc(sizeof(GIArgument) * (total_gi_args > 0 ? total_gi_args : 1));
    int gi_idx = 0;

    // 3. handle instance (this)
    if (is_method) {
        if (argCount > 0 && IS_INSTANCE(args[0])) {
            self = get_gobject_from_value(args[0]);
            lox_arg_offset = 1;
            //printf("[ERROR] args[0] is not an instance! Type tag: %d\n", AS_OBJ(args[0])->type);
        } else {
            runtimeError("Method %s requires an instance.", g_base_info_get_name(info));
            return NIL_VAL;
        }
    } else {
            //ObjInstance* instance = AS_INSTANCE(args[0]);
            //in_args[gi_idx++].v_pointer = instance->foreignPtr;

            //printf("[DEBUG] Extracted pointer from Instance: %p\n", in_args[0].v_pointer);
            //in_args[0].v_pointer = NULL;
        //}
        int gi_expected_args = g_callable_info_get_n_args((GICallableInfo*)info);
        if (argCount > gi_expected_args) {
            lox_arg_offset = 1;
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
*/

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

static void loadMethodsIntoNamespace(ObjInstance* nsInstance, ObjClass* klass, GIBaseInfo* info) {
    GIObjectInfo* current_info = (GIObjectInfo*)g_base_info_ref(info);

    int n_methods = g_object_info_get_n_methods(current_info);
    for (int i = 0; i < n_methods; i++) {
        GIFunctionInfo* fn_info = g_object_info_get_method(current_info, i);
        GIFunctionInfoFlags flags = g_function_info_get_flags(fn_info);
        const char* fn_name = g_base_info_get_name((GIBaseInfo*)fn_info);

        if (!(flags & GI_FUNCTION_IS_METHOD)) {
            ObjNative* native = newNative(giInvokeNative);
            native->foreignData = g_base_info_ref(fn_info);

            tableSet(&nsInstance->fields,
                    copyString(fn_name, strlen(fn_name)),
                    OBJ_VAL(native));
        } else {
            registerGIMethod(nsInstance->klass, (GIBaseInfo*)fn_info);
        }
        g_base_info_unref(fn_info);
    }

    GIObjectInfo* parent = g_object_info_get_parent(current_info);
    while (parent != NULL) {
        int pn_methods = g_object_info_get_n_methods(parent);
        for (int i = 0; i < pn_methods; i++) {
            GIFunctionInfo* fn_info = g_object_info_get_method(parent, i);
            if (g_function_info_get_flags(fn_info) & GI_FUNCTION_IS_METHOD) {
                registerGIMethod(klass, (GIBaseInfo*)fn_info);
            }
            g_base_info_unref(fn_info);
        }
        GIObjectInfo* next_parent = g_object_info_get_parent(parent);
        g_base_info_unref(parent);
        parent = next_parent;
    }
    g_base_info_unref(current_info);
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

typedef struct {
    const char* name;
    double value;
} GdkConstant;

static GdkConstant gdk_constants[] = {
    {"SHIFT_MASK", 1 << 0},
    {"LOCK_MASK", 1 << 1},
    {"CONTROL_MASK", 1 << 2},
    {"ALT_MASK", 1 << 3},
    {"BUTTON1_MASK", 1 << 8},

    {"KEY_Q", 113},
    {"KEY_Enter", 65293},
    {"KEY_Escape", 65307},
    {"KEY_Backspace", 65288},
    {"KEY_Tab", 65289},
    {NULL, 0}
};

void register_gdk_module(Table* registry) {
    ObjString* name = copyString("Gdk", 3);
    push(OBJ_VAL(name));

    ObjClass* gdkClass = newClass(name);
    push(OBJ_VAL(gdkClass));

    ObjInstance* gdkInstance = newInstance(gdkClass);
    push(OBJ_VAL(gdkInstance));

    for (int i = 0; gdk_constants[i].name != NULL; i++) {
        tableSet(&gdkInstance->fields,
                copyString(gdk_constants[i].name, strlen(gdk_constants[i].name)),
                NUMBER_VAL(gdk_constants[i].value));
    }

    tableSet(&vm.globals, name, OBJ_VAL(gdkInstance));

    pop();
    pop();
    pop();
}

void register_gdk_constants() {
    ObjClass* gdkClass = newClass(copyString("Gdk", 3));
    push(OBJ_VAL(gdkClass));

    tableSet(&gdkClass->methods, copyString("SHIFT_MASK", 10), NUMBER_VAL(1 << 0));
    tableSet(&gdkClass->methods, copyString("LOCK_MASK", 9), NUMBER_VAL(1 << 1));
    tableSet(&gdkClass->methods, copyString("CONTROL_MASK", 12), NUMBER_VAL(1 << 2));
    tableSet(&gdkClass->methods, copyString("ALT_MASK", 8), NUMBER_VAL(1 << 3));
    tableSet(&gdkClass->methods, copyString("BUTTON1_MASK", 8), NUMBER_VAL(1 << 4));

    tableSet(&gdkClass->methods, copyString("KEY_Q", 5), NUMBER_VAL(113));
    tableSet(&gdkClass->methods, copyString("KEY_Return", 10), NUMBER_VAL(65293));
    tableSet(&gdkClass->methods, copyString("KEY_Escape", 10), NUMBER_VAL(65307));

    tableSet(&globalRegistry->fields, copyString("Gdk", 3), OBJ_VAL(gdkClass));
    tableSet(&vm.globals, copyString("Gdk", 3), OBJ_VAL(gdkClass));
    pop();
}

static void loadMethods(ObjInstance* nsInstance, ObjClass* klass, GIBaseInfo* info) {
    GIInfoType type = g_base_info_get_type(info);
    int n_methods = 0;

    if (type == GI_INFO_TYPE_OBJECT) {
        n_methods = g_object_info_get_n_methods((GIObjectInfo*)info);
    } else if (type == GI_INFO_TYPE_STRUCT) {
        n_methods = g_struct_info_get_n_methods((GIObjectInfo*)info);
    }

    for (int i = 0; i < n_methods; i++) {
        GIFunctionInfo* fn_info = NULL;
        if (type == GI_INFO_TYPE_OBJECT) {
            fn_info = g_object_info_get_method((GIObjectInfo*)info, i);
        } else {
            fn_info = g_struct_info_get_method((GIStructInfo*)info, i);
        }

        GIFunctionInfoFlags flags = g_function_info_get_flags(fn_info);
        const char* fn_name = g_base_info_get_name((GIBaseInfo*)fn_info);

        if (!(flags & GI_FUNCTION_IS_METHOD)) {
            ObjNative* native = newNative(giInvokeNative);
            native->foreignData = g_base_info_ref(fn_info);
            tableSet(&nsInstance->fields, copyString(fn_name, strlen(fn_name)), OBJ_VAL(native));
        } else {
            registerGIMethod(klass, (GIBaseInfo*)fn_info);
        }
        g_base_info_unref(fn_info);
    }

    if (type == GI_INFO_TYPE_OBJECT) {
        GIObjectInfo* parent = g_object_info_get_parent((GIObjectInfo*)info);
        while (parent != NULL) {
            int pn = g_object_info_get_n_methods(parent);
            for (int i = 0; i < pn; i++) {
                GIFunctionInfo* p_fn = g_object_info_get_method(parent, i);
                if (g_function_info_get_flags(p_fn) & GI_FUNCTION_IS_METHOD) {
                    registerGIMethod(klass, (GIBaseInfo*)p_fn);
                }
                g_base_info_unref(p_fn);
            }
            GIObjectInfo* next = g_object_info_get_parent(parent);
            g_base_info_unref(parent);
            parent = next;
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

            ObjClass* klass = newClass(className);
            push(OBJ_VAL(klass));

            klass->foreignData = g_base_info_ref(info);
            klass->callHandler = giClassCallHandler;
            klass->getter = giPropertyGetter;
            klass->setter = giPropertySetter;

            ObjInstance* nsInstance = newInstance(klass);
            push(OBJ_VAL(nsInstance));

            //snprintf(fullTypeName, fullLen, "%s%s", namespace, name);

            const char* typeName = g_base_info_get_name(info);
            if (strcmp(namespace, "GObject") == 0) {
                snprintf(fullTypeName, strlen(name), "%s", name);
            } else {
                snprintf(fullTypeName, fullLen, "%s%s", namespace, name);
            }
            tableSet(&globalRegistry->fields, copyString(fullTypeName, strlen(fullTypeName)),
                    OBJ_VAL(klass));

            initTable(&klass->methods);

            loadMethods(nsInstance, klass, info);
            /*
            if (type == GI_INFO_TYPE_OBJECT) {
                loadMethodsForObject(nsInstance, klass, info);
            } else if (type == GI_INFO_TYPE_STRUCT) {
                loadMethodsForStruct(nsInstance, klass, info);
            }
            */
            //loadMethodsIntoNamespace(nsInstance, klass, info);
            //loadMethodsIntoClass(klass, info);

            tableSet(&module->fields, className, OBJ_VAL(klass));

            printf("[LOADER]: Registered Class '%s' (Internal: %s)\n", name, fullTypeName);
            free(fullTypeName);

            pop();
            pop();
            pop();
        }

        if (type == GI_INFO_TYPE_ENUM || type == GI_INFO_TYPE_FLAGS) {
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
                printf("[LOADER]: Registered Enum '%s' Val: %s)\n", name, val_name);
                g_base_info_unref(val_info);
            }

            tableSet(&module->fields, enumClass->name, OBJ_VAL(enumInstance));
            pop(); // instance
            pop(); // class
        }

        if (type == GI_INFO_TYPE_CONSTANT) {
            GIConstantInfo* const_info = (GIConstantInfo*)info;
            GITypeInfo* type_info = g_constant_info_get_type(const_info);

            GIArgument arg;
            g_constant_info_get_value(const_info, &arg);

            if (g_type_info_get_tag(type_info) == GI_TYPE_TAG_INT32 ||
                    g_type_info_get_tag(type_info) == GI_TYPE_TAG_UINT32) {
                tableSet(&module->fields, copyString(name, strlen(name)),
                        NUMBER_VAL((double)arg.v_int));
                printf("[LOADER]: Registered Constant '%s' Val: %d)\n", name, arg.v_int);
            }
            g_base_info_unref(type_info);
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


    ObjClass* klass = newClass(copyString("Event", 5));
    push(OBJ_VAL(klass));
    tableSet(&globalRegistry->fields, copyString("Event", 5),
            OBJ_VAL(klass));

    register_gdk_module(&globalRegistry->fields);

    pop(); // [2]
    pop(); // [1]
}
