/*
 * GObject-Introspection bindings for slox.
 *
 * The module deliberately keeps the C side small: objects, structs and
 * functions are discovered from the installed typelibs at runtime, and the
 * wrapper classes are ordinary slox classes anchored for the lifetime of the
 * VM.
 *
 * Lifetime model
 * --------------
 * A wrapper instance stores its native pointer in ObjInstance.foreignPtr (so
 * the rest of slox can keep treating it as an opaque handle) and a GiMeta in a
 * side table keyed by the instance.  GObjects are kept alive by a reference
 * owned by the wrapper; when the Lox wrapper is collected the destructor drops
 * that reference.  If the GObject dies first its weak notify clears the
 * wrapper's pointer and marks the metadata dead.
 *
 * Signal handlers have their Lox closure stored in a hidden instance field, so
 * the GC traces it exactly as long as the wrapper is alive.  The destructor
 * disconnects every handler created by gi.connect before the instance fields
 * (and therefore the closures) are released.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <girepository.h>
#include <gio/gio.h>

#include "memory.h"
#include "object.h"
#include "signals.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define GI_ERR_LEN 512
#define GI_WRAPPER_KEY "slox-gi-wrapper"

typedef enum {
    GI_WRAP_OBJECT,
    GI_WRAP_BOXED,
    GI_WRAP_RAW
} GiWrapKind;

typedef struct {
    GiWrapKind kind;
    GType type;
    gpointer ptr;
    GObject* gobj;
    bool alive;
    GArray* handlers; /* guint handler ids created by gi.connect */
    ObjInstance* instance;
} GiMeta;

typedef struct {
    bool set;
    char message[GI_ERR_LEN];
} GiError;

static GHashTable* g_instanceMeta = NULL; /* ObjInstance* -> GiMeta* */
static GHashTable* g_typeClasses = NULL;  /* GType -> ObjClass* */
static GHashTable* g_nameClasses = NULL;  /* "Namespace.Name" -> ObjClass* */
static GHashTable* g_modules = NULL;      /* namespace -> module instance */
static bool g_giLoopRunning = false;

/* ------------------------------------------------------------------------- */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------- */

static void giSetError(GiError* err, const char* format, ...) {
    if (err == NULL || err->set) return;
    va_list ap;
    va_start(ap, format);
    vsnprintf(err->message, sizeof(err->message), format, ap);
    va_end(ap);
    err->set = true;
}

static Value giFail(GiError* err) {
    runtimeError("%s", err->set ? err->message : "gi: unknown error");
    return NIL_VAL;
}

static bool giIsNumeric(Value value) {
    return IS_INT(value) || IS_FLOAT(value);
}

static int64_t giAsInt(Value value) {
    return IS_INT(value) ? AS_INT(value) : (int64_t)AS_FLOAT(value);
}

static double giAsDouble(Value value) {
    return IS_FLOAT(value) ? AS_FLOAT(value) : (double)AS_INT(value);
}

/* Appending can allocate storage and trigger a collection, so keep a newly
 * created element alive until it is reachable from the array. */
static void giArrayAppendValue(ObjArray* array, Value value) {
    pushTemp(value);
    arrayAppend(array, value);
    popTemp();
}

/* ------------------------------------------------------------------------- */
/* Allocation helper used while marshalling calls                            */
/* ------------------------------------------------------------------------- */

typedef struct {
    GPtrArray* freeList;
    GPtrArray* listNodes;
    GPtrArray* slistNodes;
    GPtrArray* variantTypes;
} GiCall;

static void giCallInit(GiCall* call) {
    memset(call, 0, sizeof(*call));
    call->freeList = g_ptr_array_new();
    call->listNodes = g_ptr_array_new();
    call->slistNodes = g_ptr_array_new();
    call->variantTypes = g_ptr_array_new();
}

static void giCallAddFree(GiCall* call, gpointer ptr) {
    if (ptr != NULL) g_ptr_array_add(call->freeList, ptr);
}

static void giCallAddList(GiCall* call, GList* list) {
    if (list != NULL) g_ptr_array_add(call->listNodes, list);
}

static void giCallAddSList(GiCall* call, GSList* list) {
    if (list != NULL) g_ptr_array_add(call->slistNodes, list);
}

static void giCallAddVariantType(GiCall* call, GVariantType* type) {
    if (type != NULL) g_ptr_array_add(call->variantTypes, type);
}

static void giCallClear(GiCall* call) {
    if (call->freeList != NULL) {
        for (guint i = 0; i < call->freeList->len; i++) {
            g_free(g_ptr_array_index(call->freeList, i));
        }
        g_ptr_array_free(call->freeList, TRUE);
    }
    if (call->listNodes != NULL) {
        for (guint i = 0; i < call->listNodes->len; i++) {
            g_list_free((GList*)g_ptr_array_index(call->listNodes, i));
        }
        g_ptr_array_free(call->listNodes, TRUE);
    }
    if (call->slistNodes != NULL) {
        for (guint i = 0; i < call->slistNodes->len; i++) {
            g_slist_free((GSList*)g_ptr_array_index(call->slistNodes, i));
        }
        g_ptr_array_free(call->slistNodes, TRUE);
    }
    if (call->variantTypes != NULL) {
        for (guint i = 0; i < call->variantTypes->len; i++) {
            g_variant_type_free((GVariantType*)g_ptr_array_index(call->variantTypes, i));
        }
        g_ptr_array_free(call->variantTypes, TRUE);
    }
    memset(call, 0, sizeof(*call));
}

/* ------------------------------------------------------------------------- */
/* Metadata and wrapper construction                                         */
/* ------------------------------------------------------------------------- */

static void giInitTables(void) {
    if (g_instanceMeta == NULL) {
        g_instanceMeta = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
    if (g_typeClasses == NULL) {
        g_typeClasses = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
    if (g_nameClasses == NULL) {
        g_nameClasses = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    if (g_modules == NULL) {
        g_modules = g_hash_table_new(g_str_hash, g_str_equal);
    }
}

static GIBaseInfo* giClassInfo(ObjClass* klass) {
    if (klass == NULL || klass->foreignData == NULL) return NULL;
    return (GIBaseInfo*)klass->foreignData;
}

static GiMeta* giMetaFor(ObjInstance* instance) {
    if (g_instanceMeta == NULL || instance == NULL) return NULL;
    return (GiMeta*)g_hash_table_lookup(g_instanceMeta, instance);
}

static GiMeta* giMetaForValue(Value value) {
    if (!IS_INSTANCE(value)) return NULL;
    return giMetaFor(AS_INSTANCE(value));
}

static gpointer giPointerOf(Value value) {
    GiMeta* meta = giMetaForValue(value);
    return meta != NULL ? meta->ptr : NULL;
}

static ObjClass* giRegisterClass(GIBaseInfo* info);
static Value giInvokeNative(int argCount, Value* args);

static const char* giTypeNameForGType(GType type) {
    return type == G_TYPE_INVALID ? "invalid" : g_type_name(type);
}

static ObjClass* giClassForGType(GType type) {
    if (type == G_TYPE_INVALID || type == 0) return NULL;

    ObjClass* klass = g_hash_table_lookup(g_typeClasses, GSIZE_TO_POINTER((gsize)type));
    if (klass != NULL) return klass;

    GIBaseInfo* info = g_irepository_find_by_gtype(NULL, type);
    if (info != NULL) {
        klass = giRegisterClass(info);
        g_base_info_unref(info);
    }

    if (klass == NULL) {
        GType parent = g_type_parent(type);
        if (parent != G_TYPE_INVALID && parent != 0) {
            klass = giClassForGType(parent);
        }
    }

    if (klass == NULL) {
        /* Concrete GObject types can be private while implementing an
         * introspected interface, e.g. GLocalFile implements Gio.File. */
        guint nInterfaces = 0;
        GType* interfaces = g_type_interfaces(type, &nInterfaces);
        for (guint i = 0; i < nInterfaces; i++) {
            GIBaseInfo* interface = g_irepository_find_by_gtype(NULL, interfaces[i]);
            if (interface != NULL) {
                if (g_base_info_get_type(interface) == GI_INFO_TYPE_INTERFACE) {
                    klass = giRegisterClass(interface);
                }
                g_base_info_unref(interface);
            }
            if (klass != NULL) break;
        }
        g_free(interfaces);
    }

    return klass;
}

static ObjClass* giClassForObject(GObject* obj) {
    if (obj == NULL) return NULL;
    return giClassForGType(G_OBJECT_TYPE(obj));
}

/* ------------------------------------------------------------------------- */
/* Field/property name handling                                              */
/* ------------------------------------------------------------------------- */

static bool giNameWithUnderscoresReplaced(const char* name, char* out, size_t outSize) {
    bool changed = false;
    size_t i = 0;
    for (; name[i] != '\0' && i + 1 < outSize; i++) {
        char c = name[i];
        if (c == '_') {
            c = '-';
            changed = true;
        }
        out[i] = c;
    }
    out[i] = '\0';
    return changed;
}

static GParamSpec* giFindProperty(GObjectClass* objClass, ObjString* name) {
    GParamSpec* spec = g_object_class_find_property(objClass, name->chars);
    if (spec != NULL) return spec;

    char translated[256];
    if (giNameWithUnderscoresReplaced(name->chars, translated, sizeof(translated))) {
        spec = g_object_class_find_property(objClass, translated);
    }
    return spec;
}

static char* giPropertyNameForGet(GObjectClass* objClass, ObjString* name) {
    if (g_object_class_find_property(objClass, name->chars) != NULL) {
        return g_strdup(name->chars);
    }
    char translated[256];
    if (giNameWithUnderscoresReplaced(name->chars, translated, sizeof(translated)) &&
            g_object_class_find_property(objClass, translated) != NULL) {
        return g_strdup(translated);
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* GIArgument <-> slox conversions                                           */
/* ------------------------------------------------------------------------- */

static size_t giTagSize(GITypeTag tag) {
    switch (tag) {
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8: return 1;
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16: return 2;
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_UNICHAR: return 4;
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_DOUBLE:
        case GI_TYPE_TAG_GTYPE: return 8;
        case GI_TYPE_TAG_BOOLEAN: return sizeof(gboolean);
        default: return 0;
    }
}

static void giArgumentInitScalar(GIArgument* out, GITypeTag tag, Value value) {
    memset(out, 0, sizeof(*out));
    switch (tag) {
        case GI_TYPE_TAG_BOOLEAN:
            out->v_boolean = IS_BOOL(value) ? AS_BOOL(value) : (giIsNumeric(value) && giAsInt(value) != 0);
            break;
        case GI_TYPE_TAG_INT8:
            out->v_int8 = (gint8)giAsInt(value);
            break;
        case GI_TYPE_TAG_UINT8:
            out->v_uint8 = (guint8)giAsInt(value);
            break;
        case GI_TYPE_TAG_INT16:
            out->v_int16 = (gint16)giAsInt(value);
            break;
        case GI_TYPE_TAG_UINT16:
            out->v_uint16 = (guint16)giAsInt(value);
            break;
        case GI_TYPE_TAG_INT32:
            out->v_int32 = (gint32)giAsInt(value);
            break;
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_UNICHAR:
            out->v_uint32 = (guint32)giAsInt(value);
            break;
        case GI_TYPE_TAG_INT64:
            out->v_int64 = (gint64)giAsInt(value);
            break;
        case GI_TYPE_TAG_UINT64:
            out->v_uint64 = (guint64)giAsInt(value);
            break;
        case GI_TYPE_TAG_FLOAT:
            out->v_float = (gfloat)giAsDouble(value);
            break;
        case GI_TYPE_TAG_DOUBLE:
            out->v_double = giAsDouble(value);
            break;
        case GI_TYPE_TAG_GTYPE:
            out->v_size = (gsize)giAsInt(value);
            break;
        default:
            break;
    }
}

static Value giScalarToLox(const GIArgument* arg, GITypeTag tag) {
    switch (tag) {
        case GI_TYPE_TAG_BOOLEAN: return BOOL_VAL(arg->v_boolean != 0);
        case GI_TYPE_TAG_INT8: return INT_VAL(arg->v_int8);
        case GI_TYPE_TAG_UINT8: return INT_VAL(arg->v_uint8);
        case GI_TYPE_TAG_INT16: return INT_VAL(arg->v_int16);
        case GI_TYPE_TAG_UINT16: return INT_VAL(arg->v_uint16);
        case GI_TYPE_TAG_INT32: return INT_VAL(arg->v_int32);
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_UNICHAR: return INT_VAL(arg->v_uint32);
        case GI_TYPE_TAG_INT64: return INT_VAL(arg->v_int64);
        case GI_TYPE_TAG_UINT64: return INT_VAL((int64_t)arg->v_uint64);
        case GI_TYPE_TAG_FLOAT: return FLOAT_VAL((double)arg->v_float);
        case GI_TYPE_TAG_DOUBLE: return FLOAT_VAL(arg->v_double);
        case GI_TYPE_TAG_GTYPE: return INT_VAL((int64_t)arg->v_size);
        default: return NIL_VAL;
    }
}

static Value giWrapGObject(GObject* obj, ObjClass* klassHint, bool ownsReference);
static Value giWrapBoxed(GType type, gpointer ptr, ObjClass* klassHint, bool ownsReference);
static Value giWrapRaw(GType type, gpointer ptr, ObjClass* klassHint, bool ownsReference);

static Value giWrapStructPointer(GIBaseInfo* info, gpointer ptr,
        ObjClass* klassHint, bool ownsReference) {
    if (ptr == NULL) return NIL_VAL;

    GType type = G_TYPE_INVALID;
    if (info != NULL && GI_IS_REGISTERED_TYPE_INFO(info)) {
        type = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)info);
    }

    if (type != G_TYPE_INVALID && type != 0 &&
            (type == G_TYPE_VARIANT || g_type_is_a(type, G_TYPE_BOXED))) {
        return giWrapBoxed(type, ptr, klassHint, ownsReference);
    }
    if (type != G_TYPE_INVALID && type != 0 && g_type_is_a(type, G_TYPE_OBJECT)) {
        return giWrapGObject((GObject*)ptr, klassHint, ownsReference);
    }

    /* Structs without a registered GType are still usable as opaque values. */
    return giWrapRaw(type, ptr, klassHint, ownsReference);
}

static Value giConvertArrayToLox(GiError* err, GIArgument* arg,
        GITypeInfo* typeInfo, GITransfer transfer, int lengthOverride);

static Value giConvertContainerToLox(GiError* err, GIArgument* arg,
        GITypeInfo* typeInfo, GITransfer transfer);

static Value giConvertInterfaceToLox(GiError* err, GIArgument* arg,
        GITypeInfo* typeInfo, GITransfer transfer) {
    GIBaseInfo* interface = g_type_info_get_interface(typeInfo);
    if (interface == NULL) {
        if (arg->v_pointer == NULL) return NIL_VAL;
        if (G_IS_OBJECT(arg->v_pointer)) {
            return giWrapGObject((GObject*)arg->v_pointer, NULL,
                    transfer == GI_TRANSFER_EVERYTHING);
        }
        return giWrapRaw(G_TYPE_INVALID, arg->v_pointer, NULL,
                transfer == GI_TRANSFER_EVERYTHING);
    }

    Value result = NIL_VAL;
    GIInfoType infoType = g_base_info_get_type(interface);

    if (infoType == GI_INFO_TYPE_ENUM || infoType == GI_INFO_TYPE_FLAGS) {
        result = INT_VAL(arg->v_int32);
        g_base_info_unref(interface);
        return result;
    }

    if (infoType == GI_INFO_TYPE_OBJECT || infoType == GI_INFO_TYPE_INTERFACE) {
        if (arg->v_pointer != NULL && G_IS_OBJECT(arg->v_pointer)) {
            /* A declared interface (such as Gio.File) is the useful wrapper
             * class even when the concrete implementation is private. */
            ObjClass* klassHint = infoType == GI_INFO_TYPE_INTERFACE
                ? giRegisterClass(interface)
                : NULL;
            result = giWrapGObject((GObject*)arg->v_pointer, klassHint,
                    transfer == GI_TRANSFER_EVERYTHING);
        }
        g_base_info_unref(interface);
        return result;
    }

    if (infoType == GI_INFO_TYPE_STRUCT || infoType == GI_INFO_TYPE_BOXED ||
            infoType == GI_INFO_TYPE_UNION) {
        const char* namespace = g_base_info_get_namespace(interface);
        const char* name = g_base_info_get_name(interface);

        if (strcmp(namespace, "GLib") == 0 &&
                (strcmp(name, "List") == 0 || strcmp(name, "SList") == 0)) {
            gpointer raw = arg->v_pointer;
            if (raw != NULL) {
                ObjArray* array = newArray();
                pushTemp(OBJ_VAL(array));
                GITypeInfo* elementType = g_type_info_get_param_type(typeInfo, 0);
                if (strcmp(name, "List") == 0) {
                    for (GList* node = (GList*)raw; node != NULL; node = node->next) {
                        Value item;
                        if (elementType != NULL && g_type_info_get_tag(elementType) == GI_TYPE_TAG_UTF8) {
                            item = node->data ? OBJ_VAL(copyString((const char*)node->data,
                                        (int)strlen((const char*)node->data))) : NIL_VAL;
                        } else if (node->data != NULL && G_IS_OBJECT(node->data)) {
                            item = giWrapGObject((GObject*)node->data, NULL, false);
                        } else {
                            item = NIL_VAL;
                        }
                        giArrayAppendValue(array, item);
                    }
                } else {
                    for (GSList* node = (GSList*)raw; node != NULL; node = node->next) {
                        Value item;
                        if (elementType != NULL && g_type_info_get_tag(elementType) == GI_TYPE_TAG_UTF8) {
                            item = node->data ? OBJ_VAL(copyString((const char*)node->data,
                                        (int)strlen((const char*)node->data))) : NIL_VAL;
                        } else if (node->data != NULL && G_IS_OBJECT(node->data)) {
                            item = giWrapGObject((GObject*)node->data, NULL, false);
                        } else {
                            item = NIL_VAL;
                        }
                        giArrayAppendValue(array, item);
                    }
                }
                if (elementType != NULL) g_base_info_unref(elementType);
                popTemp();

                if (transfer != GI_TRANSFER_NOTHING) {
                    if (strcmp(name, "List") == 0) g_list_free((GList*)raw);
                    else g_slist_free((GSList*)raw);
                }
                result = OBJ_VAL(array);
            }
            g_base_info_unref(interface);
            return result;
        }

        if (strcmp(namespace, "GLib") == 0 && strcmp(name, "HashTable") == 0) {
            /* Hash tables are exposed as opaque wrappers. */
            g_base_info_unref(interface);
            return giWrapRaw(G_TYPE_INVALID, arg->v_pointer, NULL, false);
        }

        ObjClass* klass = giRegisterClass(interface);
        if (arg->v_pointer == NULL) {
            result = NIL_VAL;
        } else {
            bool copy = transfer != GI_TRANSFER_EVERYTHING;
            gpointer ptr = arg->v_pointer;
            if (copy) {
                GType gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface);
                if (gtype != G_TYPE_INVALID && gtype != 0 && g_type_is_a(gtype, G_TYPE_BOXED)) {
                    ptr = g_boxed_copy(gtype, ptr);
                } else if (infoType == GI_INFO_TYPE_STRUCT) {
                    gsize size = g_struct_info_get_size((GIStructInfo*)interface);
                    ptr = g_memdup2(ptr, size);
                } else if (infoType == GI_INFO_TYPE_UNION) {
                    gsize size = g_union_info_get_size((GIUnionInfo*)interface);
                    ptr = g_memdup2(ptr, size);
                } else {
                    ptr = NULL;
                }
            }
            if (ptr != NULL) {
                result = giWrapStructPointer(interface, ptr, klass, true);
            }
        }
        g_base_info_unref(interface);
        return result;
    }

    g_base_info_unref(interface);
    return result;
}

static Value giContainerElementToLox(GiError* err, gpointer data,
        GITypeInfo* elementType, GITransfer transfer) {
    if (data == NULL) return NIL_VAL;
    if (elementType == NULL) {
        if (G_IS_OBJECT(data)) return giWrapGObject((GObject*)data, NULL,
                transfer == GI_TRANSFER_EVERYTHING);
        return NIL_VAL;
    }

    GITypeTag tag = g_type_info_get_tag(elementType);
    if (tag == GI_TYPE_TAG_UTF8 || tag == GI_TYPE_TAG_FILENAME) {
        return OBJ_VAL(copyString((const char*)data, (int)strlen((const char*)data)));
    }
    if (tag == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo* interface = g_type_info_get_interface(elementType);
        if (interface != NULL) {
            GIInfoType infoType = g_base_info_get_type(interface);
            if (infoType == GI_INFO_TYPE_OBJECT || infoType == GI_INFO_TYPE_INTERFACE) {
                Value result = G_IS_OBJECT(data)
                    ? giWrapGObject((GObject*)data, NULL,
                            transfer == GI_TRANSFER_EVERYTHING)
                    : NIL_VAL;
                g_base_info_unref(interface);
                return result;
            }
            if (infoType == GI_INFO_TYPE_STRUCT || infoType == GI_INFO_TYPE_UNION ||
                    infoType == GI_INFO_TYPE_BOXED) {
                GType gtype = GI_IS_REGISTERED_TYPE_INFO(interface)
                    ? g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface)
                    : G_TYPE_INVALID;
                gpointer copy = data;
                bool owns = transfer == GI_TRANSFER_EVERYTHING;
                if (!owns) {
                    if (gtype != G_TYPE_INVALID && gtype != 0 && g_type_is_a(gtype, G_TYPE_BOXED)) {
                        copy = g_boxed_copy(gtype, data);
                    } else if (infoType == GI_INFO_TYPE_STRUCT) {
                        copy = g_memdup2(data, g_struct_info_get_size((GIStructInfo*)interface));
                    } else if (infoType == GI_INFO_TYPE_UNION) {
                        copy = g_memdup2(data, g_union_info_get_size((GIUnionInfo*)interface));
                    } else {
                        copy = NULL;
                    }
                }
                Value result = copy != NULL
                    ? giWrapStructPointer(interface, copy, giRegisterClass(interface), true)
                    : NIL_VAL;
                g_base_info_unref(interface);
                return result;
            }
            g_base_info_unref(interface);
        }
    }

    if (giTagSize(tag) > 0) {
        GIArgument value;
        memset(&value, 0, sizeof(value));
        memcpy(&value, data, giTagSize(tag));
        return giScalarToLox(&value, tag);
    }
    return NIL_VAL;
}

static Value giConvertContainerToLox(GiError* err, GIArgument* arg,
        GITypeInfo* typeInfo, GITransfer transfer) {
    gpointer raw = arg->v_pointer;
    if (raw == NULL) return NIL_VAL;

    GITypeTag tag = g_type_info_get_tag(typeInfo);
    if (tag != GI_TYPE_TAG_GLIST && tag != GI_TYPE_TAG_GSLIST) {
        giSetError(err, "gi: unsupported container return");
        return NIL_VAL;
    }

    GITypeInfo* elementType = g_type_info_get_param_type(typeInfo, 0);
    ObjArray* array = newArray();
    pushTemp(OBJ_VAL(array));

    if (tag == GI_TYPE_TAG_GLIST) {
        for (GList* node = (GList*)raw; node != NULL; node = node->next) {
            giArrayAppendValue(array, giContainerElementToLox(err, node->data, elementType, transfer));
        }
    } else {
        for (GSList* node = (GSList*)raw; node != NULL; node = node->next) {
            giArrayAppendValue(array, giContainerElementToLox(err, node->data, elementType, transfer));
        }
    }

    if (elementType != NULL) g_base_info_unref((GIBaseInfo*)elementType);

    if (transfer != GI_TRANSFER_NOTHING) {
        if (tag == GI_TYPE_TAG_GLIST) g_list_free((GList*)raw);
        else g_slist_free((GSList*)raw);
    }

    popTemp();
    return OBJ_VAL(array);
}

static Value giConvertArrayToLox(GiError* err, GIArgument* arg,
        GITypeInfo* typeInfo, GITransfer transfer, int lengthOverride) {
    gpointer raw = arg->v_pointer;
    if (raw == NULL) return NIL_VAL;

    GITypeInfo* elementType = g_type_info_get_param_type(typeInfo, 0);
    if (elementType == NULL) {
        giSetError(err, "gi: array return type has no element type");
        return NIL_VAL;
    }

    GIArrayType arrayType = g_type_info_get_array_type(typeInfo);
    bool pointerElement =
        g_type_info_is_pointer(elementType) ||
        g_type_info_get_tag(elementType) == GI_TYPE_TAG_UTF8 ||
        g_type_info_get_tag(elementType) == GI_TYPE_TAG_FILENAME ||
        g_type_info_get_tag(elementType) == GI_TYPE_TAG_ARRAY ||
        g_type_info_get_tag(elementType) == GI_TYPE_TAG_GLIST ||
        g_type_info_get_tag(elementType) == GI_TYPE_TAG_GSLIST ||
        g_type_info_get_tag(elementType) == GI_TYPE_TAG_GHASH ||
        g_type_info_get_tag(elementType) == GI_TYPE_TAG_ERROR;

    size_t elementSize = pointerElement ? sizeof(gpointer) : giTagSize(
            g_type_info_get_tag(elementType));

    if (!pointerElement && elementSize == 0 &&
            g_type_info_get_tag(elementType) == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo* elementInterface = g_type_info_get_interface(elementType);
        if (elementInterface != NULL) {
            if (GI_IS_STRUCT_INFO(elementInterface)) {
                elementSize = g_struct_info_get_size((GIStructInfo*)elementInterface);
            } else if (GI_IS_UNION_INFO(elementInterface)) {
                elementSize = g_union_info_get_size((GIUnionInfo*)elementInterface);
            }
            g_base_info_unref(elementInterface);
        }
    }

    gpointer dataStart = raw;
    int count = -1;

    if (arrayType == GI_ARRAY_TYPE_ARRAY) {
        GArray* array = (GArray*)raw;
        dataStart = array->data;
        count = (int)array->len;
        pointerElement = false;
        if (elementSize == 0) {
            elementSize = g_array_get_element_size(array);
        }
    } else if (arrayType == GI_ARRAY_TYPE_PTR_ARRAY) {
        GPtrArray* array = (GPtrArray*)raw;
        dataStart = array->pdata;
        count = (int)array->len;
        pointerElement = true;
        elementSize = sizeof(gpointer);
    } else if (arrayType == GI_ARRAY_TYPE_BYTE_ARRAY) {
        GByteArray* array = (GByteArray*)raw;
        dataStart = array->data;
        count = (int)array->len;
        pointerElement = false;
        elementSize = 1;
    } else {
        int fixed = g_type_info_get_array_fixed_size(typeInfo);
        if (fixed >= 0) {
            count = fixed;
        } else if (g_type_info_is_zero_terminated(typeInfo) && pointerElement) {
            gpointer* items = (gpointer*)raw;
            count = 0;
            while (items[count] != NULL) count++;
        } else {
            /* The real length is normally a sibling out argument; without it
             * the only safe thing is to return an empty array. */
            count = 0;
        }
    }

    if (lengthOverride >= 0 && arrayType == GI_ARRAY_TYPE_C) {
        count = lengthOverride;
    }

    ObjArray* result = newArray();
    pushTemp(OBJ_VAL(result));

    if (elementSize == 0 && count > 0) {
        giSetError(err, "gi: unsupported array element type %s",
                g_type_tag_to_string(g_type_info_get_tag(elementType)));
        count = 0;
    }

    for (int i = 0; i < count; i++) {
        gpointer item = pointerElement
            ? ((gpointer*)dataStart)[i]
            : (gpointer)((char*)dataStart + (size_t)i * elementSize);
        giArrayAppendValue(result, giContainerElementToLox(err, item, elementType, transfer));
    }

    g_base_info_unref((GIBaseInfo*)elementType);

    if (transfer != GI_TRANSFER_NOTHING) {
        switch (arrayType) {
            case GI_ARRAY_TYPE_ARRAY:
                g_array_unref((GArray*)raw);
                break;
            case GI_ARRAY_TYPE_PTR_ARRAY:
                g_ptr_array_unref((GPtrArray*)raw);
                break;
            case GI_ARRAY_TYPE_BYTE_ARRAY:
                g_byte_array_unref((GByteArray*)raw);
                break;
            default:
                g_free(raw);
                break;
        }
        arg->v_pointer = NULL;
    }

    popTemp();
    return OBJ_VAL(result);
}

static Value giArgumentToLoxEx(GiError* err, GIArgument* arg,
        GITypeInfo* typeInfo, GITransfer transfer, int lengthOverride) {
    if (typeInfo == NULL) return NIL_VAL;

    GITypeTag tag = g_type_info_get_tag(typeInfo);

    switch (tag) {
        case GI_TYPE_TAG_VOID:
            return NIL_VAL;
        case GI_TYPE_TAG_BOOLEAN:
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_DOUBLE:
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UNICHAR:
            return giScalarToLox(arg, tag);

        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
            {
                const char* text = arg->v_string;
                Value result = text == NULL ? NIL_VAL
                    : OBJ_VAL(copyString(text, (int)strlen(text)));
                if (transfer == GI_TRANSFER_EVERYTHING) {
                    g_free(arg->v_string);
                    arg->v_string = NULL;
                }
                return result;
            }

        case GI_TYPE_TAG_INTERFACE:
            return giConvertInterfaceToLox(err, arg, typeInfo, transfer);

        case GI_TYPE_TAG_ARRAY:
            return giConvertArrayToLox(err, arg, typeInfo, transfer, lengthOverride);

        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
            return giConvertContainerToLox(err, arg, typeInfo, transfer);
        case GI_TYPE_TAG_GHASH:
        case GI_TYPE_TAG_ERROR:
            if (arg->v_pointer == NULL) return NIL_VAL;
            return INT_VAL((int64_t)(intptr_t)arg->v_pointer);

        default:
            giSetError(err, "gi: unsupported return type tag %s",
                    g_type_tag_to_string(tag));
            return NIL_VAL;
    }
}

static Value giArgumentToLox(GiError* err, GIArgument* arg,
        GITypeInfo* typeInfo, GITransfer transfer) {
    return giArgumentToLoxEx(err, arg, typeInfo, transfer, -1);
}

/* ------------------------------------------------------------------------- */
/* Array input conversion                                                    */
/* ------------------------------------------------------------------------- */

static bool giWriteArrayElement(GiCall* call, char* base, size_t elementSize,
        Value value, GITypeInfo* elementType, bool pointerElement, GiError* err) {
    GITypeTag tag = g_type_info_get_tag(elementType);

    if (pointerElement) {
        gpointer ptr = NULL;
        if (tag == GI_TYPE_TAG_UTF8 || tag == GI_TYPE_TAG_FILENAME) {
            if (IS_STRING(value)) ptr = AS_CSTRING(value);
            else if (!IS_NIL(value)) {
                giSetError(err, "gi: array element must be a string");
                return false;
            }
        } else if (tag == GI_TYPE_TAG_INTERFACE) {
            if (IS_NIL(value)) {
                ptr = NULL;
            } else {
                GiMeta* meta = giMetaForValue(value);
                if (meta == NULL) {
                    giSetError(err, "gi: array element must be a native value");
                    return false;
                }
                ptr = meta->ptr;
            }
        } else if (tag == GI_TYPE_TAG_ARRAY) {
            giSetError(err, "gi: nested arrays are not supported");
            return false;
        } else if (giIsNumeric(value)) {
            /* An array of pointers to scalars is uncommon; allocate a cell. */
            size_t size = giTagSize(tag);
            gpointer cell = g_malloc0(size);
            GIArgument scalar;
            giArgumentInitScalar(&scalar, tag, value);
            memcpy(cell, &scalar, size);
            giCallAddFree(call, cell);
            ptr = cell;
        } else {
            giSetError(err, "gi: unsupported array element value");
            return false;
        }
        *(gpointer*)base = ptr;
        return true;
    }

    GIArgument scalar;
    giArgumentInitScalar(&scalar, tag, value);
    memcpy(base, &scalar, elementSize);
    return true;
}

static gpointer giBuildCArray(GiCall* call, Value value, GITypeInfo* typeInfo,
        GITypeInfo* elementType, int* outLength, GiError* err) {
    GITypeTag elementTag = g_type_info_get_tag(elementType);
    bool pointerElement =
        g_type_info_is_pointer(elementType) ||
        elementTag == GI_TYPE_TAG_UTF8 ||
        elementTag == GI_TYPE_TAG_FILENAME ||
        elementTag == GI_TYPE_TAG_ARRAY ||
        elementTag == GI_TYPE_TAG_GLIST ||
        elementTag == GI_TYPE_TAG_GSLIST ||
        elementTag == GI_TYPE_TAG_GHASH;

    GIBaseInfo* elementInterface = NULL;
    if (elementTag == GI_TYPE_TAG_INTERFACE) {
        elementInterface = g_type_info_get_interface(elementType);
        if (elementInterface != NULL) {
            GType gtype = GI_IS_REGISTERED_TYPE_INFO(elementInterface)
                ? g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)elementInterface)
                : G_TYPE_INVALID;
            if (gtype != G_TYPE_INVALID && gtype != 0 &&
                    !g_type_is_a(gtype, G_TYPE_BOXED) &&
                    !g_type_is_a(gtype, G_TYPE_OBJECT) &&
                    !g_type_is_a(gtype, G_TYPE_INTERFACE)) {
                pointerElement = false;
            }
            if (!g_type_info_is_pointer(elementType) &&
                    (GI_IS_STRUCT_INFO(elementInterface) || GI_IS_UNION_INFO(elementInterface))) {
                pointerElement = false;
            }
        }
    }

    /* Byte arrays are conveniently represented by slox strings. */
    if (!pointerElement && elementTag == GI_TYPE_TAG_UINT8 && IS_STRING(value)) {
        ObjString* str = AS_STRING(value);
        if (outLength != NULL) *outLength = str->length;
        return str->chars;
    }

    if (!IS_ARRAY(value)) {
        giSetError(err, "gi: expected an array");
        if (elementInterface != NULL) g_base_info_unref(elementInterface);
        return NULL;
    }

    ObjArray* array = AS_ARRAY(value);
    int count = array->count;
    size_t elementSize = 0;

    if (pointerElement) {
        elementSize = sizeof(gpointer);
    } else if (elementTag == GI_TYPE_TAG_INTERFACE &&
            (GI_IS_STRUCT_INFO(elementInterface) || GI_IS_UNION_INFO(elementInterface))) {
        elementSize = GI_IS_STRUCT_INFO(elementInterface)
            ? g_struct_info_get_size((GIStructInfo*)elementInterface)
            : g_union_info_get_size((GIUnionInfo*)elementInterface);
    } else {
        elementSize = giTagSize(elementTag);
    }

    if (elementSize == 0) {
        giSetError(err, "gi: unsupported array element type %s",
                g_type_tag_to_string(elementTag));
        if (elementInterface != NULL) g_base_info_unref(elementInterface);
        return NULL;
    }

    char* data = g_malloc0(elementSize * (size_t)(count + 1));
    giCallAddFree(call, data);

    for (int i = 0; i < count; i++) {
        Value item = array->values[i];

        if (!pointerElement && elementTag == GI_TYPE_TAG_INTERFACE &&
                (GI_IS_STRUCT_INFO(elementInterface) || GI_IS_UNION_INFO(elementInterface))) {
            GiMeta* meta = giMetaForValue(item);
            if (meta == NULL || meta->ptr == NULL) {
                giSetError(err, "gi: array element %d is not a native struct", i);
                if (elementInterface != NULL) g_base_info_unref(elementInterface);
                return NULL;
            }
            memcpy(data + (size_t)i * elementSize, meta->ptr, elementSize);
        } else if (!giWriteArrayElement(call, data + (size_t)i * elementSize,
                    elementSize, item, elementType, pointerElement, err)) {
            if (elementInterface != NULL) g_base_info_unref(elementInterface);
            return NULL;
        }
    }

    if (outLength != NULL) *outLength = count;
    if (elementInterface != NULL) g_base_info_unref(elementInterface);
    return data;
}

static GList* giBuildGList(GiCall* call, Value value, GITypeInfo* typeInfo,
        bool isSList, GiError* err) {
    if (!IS_ARRAY(value)) {
        giSetError(err, "gi: expected an array for a GLib list");
        return NULL;
    }

    ObjArray* array = AS_ARRAY(value);
    GITypeInfo* elementType = g_type_info_get_param_type(typeInfo, 0);
    GList* list = NULL;
    GSList* slist = NULL;

    for (int i = 0; i < array->count; i++) {
        Value item = array->values[i];
        gpointer ptr = NULL;
        if (elementType != NULL && g_type_info_get_tag(elementType) == GI_TYPE_TAG_UTF8) {
            if (!IS_STRING(item)) {
                giSetError(err, "gi: GLib list element %d must be a string", i);
                break;
            }
            ptr = AS_CSTRING(item);
        } else if (elementType != NULL && g_type_info_get_tag(elementType) == GI_TYPE_TAG_INTERFACE) {
            GiMeta* meta = giMetaForValue(item);
            if (meta == NULL) {
                giSetError(err, "gi: GLib list element %d is not a native value", i);
                break;
            }
            ptr = meta->ptr;
        } else {
            giSetError(err, "gi: unsupported GLib list element type");
            break;
        }

        if (isSList) {
            slist = g_slist_append(slist, ptr);
        } else {
            list = g_list_append(list, ptr);
        }
    }

    if (elementType != NULL) g_base_info_unref(elementType);

    if (err->set) {
        if (list != NULL) g_list_free(list);
        if (slist != NULL) g_slist_free(slist);
        return NULL;
    }

    if (isSList) {
        giCallAddSList(call, slist);
        return (GList*)slist;
    }
    giCallAddList(call, list);
    return list;
}

static GHashTable* giBuildHashTable(GiCall* call, Value value,
        GITypeInfo* typeInfo, GiError* err) {
    if (!IS_MAP(value)) {
        giSetError(err, "gi: expected a map for a GLib hash table");
        return NULL;
    }

    (void)typeInfo;
    ObjMap* map = AS_MAP(value);
    GHashTable* table = g_hash_table_new(g_str_hash, g_str_equal);
    giCallAddFree(call, table);

    for (int i = 0; i < map->items.capacity; i++) {
        Entry2* entry = &map->items.entries[i];
        if (IS_NIL(entry->key)) continue;
        if (!IS_STRING(entry->key)) {
            giSetError(err, "gi: GLib hash table keys must be strings");
            return NULL;
        }

        GiMeta* meta = giMetaForValue(entry->value);
        gpointer val = NULL;
        if (meta != NULL) val = meta->ptr;
        else if (IS_STRING(entry->value)) val = AS_CSTRING(entry->value);
        else if (giIsNumeric(entry->value)) {
            int64_t n = giAsInt(entry->value);
            val = GSIZE_TO_POINTER((gsize)n);
        }

        g_hash_table_insert(table, AS_STRING(entry->key)->chars, val);
    }

    return table;
}

/* ------------------------------------------------------------------------- */
/* slox value -> GIArgument for input arguments                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    Value value;
    bool present;
    bool skip;
} GiArgSlot;

static bool giConvertInputValue(GiCall* call, Value value, GITypeInfo* typeInfo,
        GIArgInfo* argInfo, GiArgSlot* slots, GIArgument* out, GiError* err) {
    memset(out, 0, sizeof(*out));
    GITypeTag tag = g_type_info_get_tag(typeInfo);

    switch (tag) {
        case GI_TYPE_TAG_VOID:
            if (g_type_info_is_pointer(typeInfo)) {
                if (IS_NIL(value)) {
                    out->v_pointer = NULL;
                } else if (IS_INSTANCE(value)) {
                    out->v_pointer = giPointerOf(value);
                } else if (IS_STRING(value)) {
                    out->v_pointer = AS_CSTRING(value);
                } else if (giIsNumeric(value)) {
                    out->v_pointer = (gpointer)(intptr_t)giAsInt(value);
                }
            }
            return true;

        case GI_TYPE_TAG_BOOLEAN:
        case GI_TYPE_TAG_INT8:
        case GI_TYPE_TAG_UINT8:
        case GI_TYPE_TAG_INT16:
        case GI_TYPE_TAG_UINT16:
        case GI_TYPE_TAG_INT32:
        case GI_TYPE_TAG_UINT32:
        case GI_TYPE_TAG_INT64:
        case GI_TYPE_TAG_UINT64:
        case GI_TYPE_TAG_FLOAT:
        case GI_TYPE_TAG_DOUBLE:
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UNICHAR:
            if (!giIsNumeric(value) && !(tag == GI_TYPE_TAG_BOOLEAN && IS_BOOL(value))) {
                giSetError(err, "gi: argument '%s' expects a number",
                        argInfo ? g_base_info_get_name((GIBaseInfo*)argInfo) : "?");
                return false;
            }
            giArgumentInitScalar(out, tag, value);
            return true;

        case GI_TYPE_TAG_UTF8:
        case GI_TYPE_TAG_FILENAME:
            if (IS_STRING(value)) {
                out->v_string = AS_CSTRING(value);
                return true;
            }
            if (IS_NIL(value)) {
                out->v_string = NULL;
                return true;
            }
            giSetError(err, "gi: argument '%s' expects a string",
                    argInfo ? g_base_info_get_name((GIBaseInfo*)argInfo) : "?");
            return false;

        case GI_TYPE_TAG_INTERFACE:
            {
                GIBaseInfo* interface = g_type_info_get_interface(typeInfo);
                if (interface == NULL) {
                    out->v_pointer = giPointerOf(value);
                    return true;
                }

                GIInfoType infoType = g_base_info_get_type(interface);
                GType gtype = GI_IS_REGISTERED_TYPE_INFO(interface)
                    ? g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface)
                    : G_TYPE_INVALID;

                if (gtype == G_TYPE_VARIANT_TYPE && IS_STRING(value)) {
                    GVariantType* variantType = g_variant_type_new(AS_CSTRING(value));
                    if (variantType == NULL) {
                        giSetError(err, "gi: argument '%s' is not a valid variant type",
                                argInfo ? g_base_info_get_name((GIBaseInfo*)argInfo) : "?");
                        g_base_info_unref(interface);
                        return false;
                    }
                    giCallAddVariantType(call, variantType);
                    out->v_pointer = variantType;
                    g_base_info_unref(interface);
                    return true;
                }

                if (infoType == GI_INFO_TYPE_ENUM || infoType == GI_INFO_TYPE_FLAGS) {
                    if (!giIsNumeric(value)) {
                        giSetError(err, "gi: argument '%s' expects an enum value",
                                argInfo ? g_base_info_get_name((GIBaseInfo*)argInfo) : "?");
                        g_base_info_unref(interface);
                        return false;
                    }
                    out->v_int = (int)giAsInt(value);
                    g_base_info_unref(interface);
                    return true;
                }

                if (!g_type_info_is_pointer(typeInfo)) {
                    GiMeta* meta = giMetaForValue(value);
                    if (meta == NULL) {
                        giSetError(err, "gi: argument '%s' expects a native struct",
                                argInfo ? g_base_info_get_name((GIBaseInfo*)argInfo) : "?");
                        g_base_info_unref(interface);
                        return false;
                    }
                    out->v_pointer = meta->ptr;
                    g_base_info_unref(interface);
                    return true;
                }

                if (IS_NIL(value)) {
                    out->v_pointer = NULL;
                    g_base_info_unref(interface);
                    return true;
                }

                GiMeta* meta = giMetaForValue(value);
                if (meta == NULL) {
                    giSetError(err, "gi: argument '%s' expects a native object",
                            argInfo ? g_base_info_get_name((GIBaseInfo*)argInfo) : "?");
                    g_base_info_unref(interface);
                    return false;
                }

                out->v_pointer = meta->ptr;
                g_base_info_unref(interface);
                return true;
            }

        case GI_TYPE_TAG_ARRAY:
            {
                GITypeInfo* elementType = g_type_info_get_param_type(typeInfo, 0);
                if (elementType == NULL) {
                    giSetError(err, "gi: array argument has no element type");
                    return false;
                }
                int length = -1;
                out->v_pointer = giBuildCArray(call, value, typeInfo, elementType,
                        &length, err);
                g_base_info_unref(elementType);
                if (err->set) return false;

                int lengthIndex = g_type_info_get_array_length(typeInfo);
                if (lengthIndex >= 0 && slots != NULL && slots[lengthIndex].present &&
                        !giIsNumeric(slots[lengthIndex].value)) {
                    giSetError(err, "gi: array length argument must be a number");
                    return false;
                }
                return true;
            }

        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
            if (IS_NIL(value)) {
                out->v_pointer = NULL;
                return true;
            }
            out->v_pointer = giBuildGList(call, value, typeInfo,
                    tag == GI_TYPE_TAG_GSLIST, err);
            return !err->set;

        case GI_TYPE_TAG_GHASH:
            if (IS_NIL(value)) {
                out->v_pointer = NULL;
                return true;
            }
            out->v_pointer = giBuildHashTable(call, value, typeInfo, err);
            return !err->set;

        default:
            giSetError(err, "gi: unsupported argument type %s",
                    g_type_tag_to_string(tag));
            return false;
    }
}

/* ------------------------------------------------------------------------- */
/* Out argument storage                                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    GITypeInfo* typeInfo;
    GIArgInfo* argInfo;
    int argIndex;
    GIArgument input;
    gpointer storage;
    size_t size;
    bool pointerStorage;
    bool structStorage;
    bool canRead;
} GiOutValue;

static void giInitOutValue(GiOutValue* out, int argIndex, GIArgInfo* argInfo,
        GITypeInfo* typeInfo) {
    memset(out, 0, sizeof(*out));
    out->argInfo = argInfo;
    out->argIndex = argIndex;
    out->typeInfo = typeInfo;
    if (typeInfo != NULL) {
        out->typeInfo = (GITypeInfo*)g_base_info_ref((GIBaseInfo*)typeInfo);
    }
}

static void giClearOutValue(GiOutValue* out) {
    if (out->typeInfo != NULL) {
        g_base_info_unref((GIBaseInfo*)out->typeInfo);
        out->typeInfo = NULL;
    }
    g_free(out->storage);
    out->storage = NULL;
}

static bool giAllocateOutStorage(GiOutValue* out, GiError* err) {
    GITypeTag tag = g_type_info_get_tag(out->typeInfo);
    bool isPointer = g_type_info_is_pointer(out->typeInfo);

    if (tag == GI_TYPE_TAG_VOID) {
        out->size = 0;
        out->canRead = false;
        return true;
    }

    if (tag == GI_TYPE_TAG_INTERFACE && !isPointer) {
        GIBaseInfo* interface = g_type_info_get_interface(out->typeInfo);
        if (interface != NULL) {
            if (GI_IS_STRUCT_INFO(interface)) {
                out->size = g_struct_info_get_size((GIStructInfo*)interface);
                out->structStorage = true;
            } else if (GI_IS_UNION_INFO(interface)) {
                out->size = g_union_info_get_size((GIUnionInfo*)interface);
                out->structStorage = true;
            }
            g_base_info_unref(interface);
        }
        if (out->size == 0) {
            out->size = sizeof(gpointer);
            out->pointerStorage = true;
        }
    } else if (isPointer || tag == GI_TYPE_TAG_UTF8 || tag == GI_TYPE_TAG_FILENAME ||
            tag == GI_TYPE_TAG_ARRAY || tag == GI_TYPE_TAG_ERROR ||
            tag == GI_TYPE_TAG_GLIST || tag == GI_TYPE_TAG_GSLIST ||
            tag == GI_TYPE_TAG_GHASH) {
        out->size = sizeof(gpointer);
        out->pointerStorage = true;
    } else {
        out->size = giTagSize(tag);
        if (out->size == 0) {
            giSetError(err, "gi: unsupported out argument type %s",
                    g_type_tag_to_string(tag));
            return false;
        }
    }

    out->storage = g_malloc0(out->size);
    out->canRead = true;
    return true;
}

static void giStoreInputInOut(GiOutValue* out, const GIArgument* input) {
    if (out->structStorage) {
        if (input->v_pointer != NULL) {
            memcpy(out->storage, input->v_pointer, out->size);
        }
    } else if (out->pointerStorage) {
        *(gpointer*)out->storage = input->v_pointer;
    } else {
        memcpy(out->storage, input, out->size);
    }
}

static bool giOutValueAsInt(GiOutValue* out, int64_t* result) {
    if (out == NULL || !out->canRead || out->storage == NULL || out->structStorage) {
        return false;
    }

    GIArgument value;
    memset(&value, 0, sizeof(value));
    if (out->pointerStorage) {
        value.v_pointer = *(gpointer*)out->storage;
    } else {
        memcpy(&value, out->storage, out->size);
    }

    switch (g_type_info_get_tag(out->typeInfo)) {
        case GI_TYPE_TAG_BOOLEAN: *result = value.v_boolean; return true;
        case GI_TYPE_TAG_INT8: *result = value.v_int8; return true;
        case GI_TYPE_TAG_UINT8: *result = value.v_uint8; return true;
        case GI_TYPE_TAG_INT16: *result = value.v_int16; return true;
        case GI_TYPE_TAG_UINT16: *result = value.v_uint16; return true;
        case GI_TYPE_TAG_INT32: *result = value.v_int32; return true;
        case GI_TYPE_TAG_UINT32: *result = value.v_uint32; return true;
        case GI_TYPE_TAG_INT64: *result = value.v_int64; return true;
        case GI_TYPE_TAG_UINT64: *result = (int64_t)value.v_uint64; return true;
        case GI_TYPE_TAG_GTYPE:
        case GI_TYPE_TAG_UNICHAR: *result = (int64_t)value.v_size; return true;
        default: return false;
    }
}

static Value giReadOutValue(GiError* err, GiOutValue* out, GITransfer transfer,
        GiOutValue* allOuts, int outCount) {
    if (!out->canRead || out->storage == NULL) return NIL_VAL;

    GIArgument value;
    memset(&value, 0, sizeof(value));

    if (out->structStorage) {
        value.v_pointer = out->storage;
        /* The callee wrote into memory we own. */
        return giConvertInterfaceToLox(err, &value, out->typeInfo, GI_TRANSFER_NOTHING);
    }
    if (out->pointerStorage) {
        value.v_pointer = *(gpointer*)out->storage;
    } else {
        memcpy(&value, out->storage, out->size);
    }

    int lengthOverride = -1;
    if (g_type_info_get_tag(out->typeInfo) == GI_TYPE_TAG_ARRAY) {
        int lengthIndex = g_type_info_get_array_length(out->typeInfo);
        if (lengthIndex >= 0) {
            for (int i = 0; i < outCount; i++) {
                if (allOuts[i].argIndex == lengthIndex) {
                    int64_t len = 0;
                    if (giOutValueAsInt(&allOuts[i], &len) && len >= 0) {
                        lengthOverride = (int)len;
                    }
                    break;
                }
            }
        }
    }

    return giArgumentToLoxEx(err, &value, out->typeInfo, transfer, lengthOverride);
}

/* ------------------------------------------------------------------------- */
/* Visit helpers for class methods                                           */
/* ------------------------------------------------------------------------- */

static void giNativeDestructor(ObjNative* native) {
    if (native->foreignData != NULL) {
        g_base_info_unref((GIBaseInfo*)native->foreignData);
        native->foreignData = NULL;
    }
}

static ObjNative* giNewInfoNative(GIFunctionInfo* info) {
    ObjNative* native = newNative(giInvokeNative);
    native->foreignData = g_base_info_ref((GIBaseInfo*)info);
    native->destructor = giNativeDestructor;
    return native;
}

static void giRegisterFunction(Table* table, GIFunctionInfo* info) {
    const char* name = g_base_info_get_name((GIBaseInfo*)info);
    ObjString* key = copyString(name, (int)strlen(name));
    pushTemp(OBJ_VAL(key));

    Value existing;
    if (!tableGet(table, key, &existing)) {
        ObjNative* native = giNewInfoNative(info);
        pushTemp(OBJ_VAL(native));
        tableSet(table, key, OBJ_VAL(native));
        popTemp();
    }
    popTemp();
}

static void giRegisterInterfaceMethods(ObjClass* klass, GIInterfaceInfo* iface, int depth);

static void giRegisterInfoMethods(ObjClass* klass, GIBaseInfo* info, int depth) {
    if (info == NULL || depth > 8) return;

    GIInfoType type = g_base_info_get_type(info);

    if (type == GI_INFO_TYPE_OBJECT) {
        GIObjectInfo* objectInfo = (GIObjectInfo*)info;
        int nMethods = g_object_info_get_n_methods(objectInfo);
        for (int i = 0; i < nMethods; i++) {
            GIFunctionInfo* method = g_object_info_get_method(objectInfo, i);
            GIFunctionInfoFlags flags = g_function_info_get_flags(method);
            if (flags & GI_FUNCTION_IS_METHOD) {
                giRegisterFunction(&klass->methods, method);
            }
            g_base_info_unref((GIBaseInfo*)method);
        }

        int nInterfaces = g_object_info_get_n_interfaces(objectInfo);
        for (int i = 0; i < nInterfaces; i++) {
            GIInterfaceInfo* iface = g_object_info_get_interface(objectInfo, i);
            giRegisterInterfaceMethods(klass, iface, depth + 1);
            g_base_info_unref((GIBaseInfo*)iface);
        }

        GIObjectInfo* parent = g_object_info_get_parent(objectInfo);
        if (parent != NULL) {
            giRegisterInfoMethods(klass, (GIBaseInfo*)parent, depth + 1);
            g_base_info_unref((GIBaseInfo*)parent);
        }
    } else if (type == GI_INFO_TYPE_INTERFACE) {
        giRegisterInterfaceMethods(klass, (GIInterfaceInfo*)info, depth);
    } else if (type == GI_INFO_TYPE_STRUCT) {
        int nMethods = g_struct_info_get_n_methods((GIStructInfo*)info);
        for (int i = 0; i < nMethods; i++) {
            GIFunctionInfo* method = g_struct_info_get_method((GIStructInfo*)info, i);
            if (g_function_info_get_flags(method) & GI_FUNCTION_IS_METHOD) {
                giRegisterFunction(&klass->methods, method);
            }
            g_base_info_unref((GIBaseInfo*)method);
        }
    } else if (type == GI_INFO_TYPE_UNION) {
        int nMethods = g_union_info_get_n_methods((GIUnionInfo*)info);
        for (int i = 0; i < nMethods; i++) {
            GIFunctionInfo* method = g_union_info_get_method((GIUnionInfo*)info, i);
            if (g_function_info_get_flags(method) & GI_FUNCTION_IS_METHOD) {
                giRegisterFunction(&klass->methods, method);
            }
            g_base_info_unref((GIBaseInfo*)method);
        }
    }
}

static void giRegisterInterfaceMethods(ObjClass* klass, GIInterfaceInfo* iface, int depth) {
    if (iface == NULL || depth > 8) return;

    int nMethods = g_interface_info_get_n_methods(iface);
    for (int i = 0; i < nMethods; i++) {
        GIFunctionInfo* method = g_interface_info_get_method(iface, i);
        if (g_function_info_get_flags(method) & GI_FUNCTION_IS_METHOD) {
            giRegisterFunction(&klass->methods, method);
        }
        g_base_info_unref((GIBaseInfo*)method);
    }

    int nPrereq = g_interface_info_get_n_prerequisites(iface);
    for (int i = 0; i < nPrereq; i++) {
        GIBaseInfo* prereq = g_interface_info_get_prerequisite(iface, i);
        if (prereq != NULL) {
            if (GI_IS_INTERFACE_INFO(prereq)) {
                giRegisterInterfaceMethods(klass, (GIInterfaceInfo*)prereq, depth + 1);
            }
            g_base_info_unref(prereq);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Constructors and method invocations                                       */
/* ------------------------------------------------------------------------- */

static bool giCallInvoke(GIFunctionInfo* info, GIArgument* inArgs, int nInArgs,
        GIArgument* outArgs, int nOutArgs, GIArgument* returnValue,
        GError** gerror) {
    return g_function_info_invoke(info, inArgs, nInArgs, outArgs, nOutArgs,
            returnValue, gerror);
}

static bool giCallableHasSelf(GIFunctionInfo* info) {
    return (g_function_info_get_flags(info) & GI_FUNCTION_IS_METHOD) != 0;
}

typedef struct {
    GIFunctionInfo* info;
    int total;
    int required;
    bool isNew;
} GiConstructorCandidate;

static bool giCountCallableArgs(GICallableInfo* callable, int* total, int* required) {
    int n = g_callable_info_get_n_args(callable);
    int totalCount = 0;
    int requiredCount = 0;

    for (int i = 0; i < n; i++) {
        GIArgInfo* arg = g_callable_info_get_arg(callable, i);

        if (!g_arg_info_is_skip(arg)) {
            GIDirection dir = g_arg_info_get_direction(arg);
            if (dir == GI_DIRECTION_IN || dir == GI_DIRECTION_INOUT) {
                totalCount++;
                bool optional = g_arg_info_is_optional(arg) ||
                    (g_arg_info_get_closure(arg) >= 0) ||
                    (g_arg_info_get_destroy(arg) >= 0);
                if (!optional) requiredCount++;
            }
        }
        g_base_info_unref((GIBaseInfo*)arg);
    }
    if (total != NULL) *total = totalCount;
    if (required != NULL) *required = requiredCount;
    return true;
}

static GIFunctionInfo* giFindConstructor(GIBaseInfo* classInfo, int argCount) {
    GIInfoType type = g_base_info_get_type(classInfo);
    int nMethods = 0;
    if (type == GI_INFO_TYPE_OBJECT) nMethods = g_object_info_get_n_methods((GIObjectInfo*)classInfo);
    else if (type == GI_INFO_TYPE_STRUCT) nMethods = g_struct_info_get_n_methods((GIStructInfo*)classInfo);
    else if (type == GI_INFO_TYPE_UNION) nMethods = g_union_info_get_n_methods((GIUnionInfo*)classInfo);
    else return NULL;

    GIFunctionInfo* best = NULL;
    int bestScore = 0x7fffffff;

    for (int i = 0; i < nMethods; i++) {
        GIFunctionInfo* method = NULL;
        if (type == GI_INFO_TYPE_OBJECT) method = g_object_info_get_method((GIObjectInfo*)classInfo, i);
        else if (type == GI_INFO_TYPE_STRUCT) method = g_struct_info_get_method((GIStructInfo*)classInfo, i);
        else method = g_union_info_get_method((GIUnionInfo*)classInfo, i);

        const char* name = g_base_info_get_name((GIBaseInfo*)method);
        GIFunctionInfoFlags flags = g_function_info_get_flags(method);
        bool isConstructor = (flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0 ||
            strcmp(name, "new") == 0;

        if (isConstructor) {
            int total = 0;
            int required = 0;
            giCountCallableArgs((GICallableInfo*)method, &total, &required);

            int score;
            if (argCount > total) {
                score = 1000 + (argCount - total);
            } else if (argCount < required) {
                score = 500 + (required - argCount);
            } else {
                score = total - argCount;
            }
            if (strcmp(name, "new") == 0) score -= 1;

            if (score < bestScore) {
                if (best != NULL) g_base_info_unref((GIBaseInfo*)best);
                best = (GIFunctionInfo*)g_base_info_ref((GIBaseInfo*)method);
                bestScore = score;
            }
        }
        g_base_info_unref((GIBaseInfo*)method);
    }
    return best;
}

static Value giWrapConstructorResult(GIArgument* retval, GITypeInfo* returnType,
        ObjClass* klassHint, GiError* err) {
    GITransfer transfer = GI_TRANSFER_EVERYTHING;
    GITypeTag tag = g_type_info_get_tag(returnType);

    if (tag == GI_TYPE_TAG_INTERFACE) {
        GIBaseInfo* interface = g_type_info_get_interface(returnType);
        if (interface != NULL) {
            GType gtype = GI_IS_REGISTERED_TYPE_INFO(interface)
                ? g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)interface)
                : G_TYPE_INVALID;

            if (gtype != G_TYPE_INVALID && gtype != 0 && g_type_is_a(gtype, G_TYPE_OBJECT)) {
                Value result = giWrapGObject((GObject*)retval->v_pointer, klassHint,
                        transfer == GI_TRANSFER_EVERYTHING);
                g_base_info_unref(interface);
                return result;
            }
            if (gtype != G_TYPE_INVALID && gtype != 0 &&
                    (gtype == G_TYPE_VARIANT || g_type_is_a(gtype, G_TYPE_BOXED))) {
                Value result = giWrapBoxed(gtype, retval->v_pointer, klassHint,
                        transfer == GI_TRANSFER_EVERYTHING);
                g_base_info_unref(interface);
                return result;
            }

            Value result = giWrapStructPointer(interface, retval->v_pointer, klassHint,
                    transfer != GI_TRANSFER_NOTHING);
            g_base_info_unref(interface);
            return result;
        }
    }

    if (retval->v_pointer != NULL && G_IS_OBJECT(retval->v_pointer)) {
        return giWrapGObject((GObject*)retval->v_pointer, klassHint, true);
    }

    return giArgumentToLox(err, retval, returnType, transfer);
}

static Value giInvokeConstructorInfo(ObjClass* klass, GIFunctionInfo* found,
        int argCount, Value* args, GiError* err) {
    if (klass == NULL) {
        giSetError(err, "gi: constructor has no wrapper class");
        return NIL_VAL;
    }

    GIBaseInfo* classInfo = giClassInfo(klass);
    if (classInfo == NULL) {
        giSetError(err, "gi: class %s has no introspection metadata", klass->name->chars);
        return NIL_VAL;
    }

    GType gtype = G_TYPE_INVALID;
    if (GI_IS_REGISTERED_TYPE_INFO(classInfo)) {
        gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)classInfo);
    }

    /* Zero-argument class calls are shorthand for the default construction of
     * a GObject, e.g. Gtk.Box(), even when the introspected constructor lists
     * required arguments whose defaults are not available in the old GI API. */
    if (found == NULL && argCount == 0 && gtype != G_TYPE_INVALID && gtype != 0 &&
            g_type_is_a(gtype, G_TYPE_OBJECT) && !G_TYPE_IS_ABSTRACT(gtype)) {
        GObject* object = g_object_new(gtype, NULL);
        if (object != NULL) {
            return giWrapGObject(object, klass, true);
        }
    }

    GIFunctionInfo* constructor = found;
    bool ownsConstructor = false;
    if (constructor == NULL) {
        constructor = giFindConstructor(classInfo, argCount);
        ownsConstructor = true;
    }

    if (constructor == NULL) {
        if (gtype != G_TYPE_INVALID && gtype != 0 &&
                g_type_is_a(gtype, G_TYPE_OBJECT)) {
            if (argCount != 0) {
                giSetError(err, "gi: %s has no constructor taking %d arguments",
                        klass->name->chars, argCount);
                return NIL_VAL;
            }
            GObject* object = g_object_new(gtype, NULL);
            if (object == NULL) {
                giSetError(err, "gi: failed to create %s", klass->name->chars);
                return NIL_VAL;
            }
            return giWrapGObject(object, klass, true);
        }
        giSetError(err, "gi: %s has no usable constructor", klass->name->chars);
        return NIL_VAL;
    }

    GICallableInfo* callable = (GICallableInfo*)constructor;
    int nArgs = g_callable_info_get_n_args(callable);
    GiArgSlot* slots = g_new0(GiArgSlot, (gsize)(nArgs > 0 ? nArgs : 1));
    GiCall call;
    giCallInit(&call);

    int loxIndex = 0;
    int totalIn = 0;
    int totalOut = 0;

    for (int i = 0; i < nArgs; i++) {
        GIArgInfo* argInfo = g_callable_info_get_arg(callable, i);
        slots[i].skip = g_arg_info_is_skip(argInfo);
        GIDirection dir = g_arg_info_get_direction(argInfo);
        if (slots[i].skip) {
            /* Allow the hidden argument to be omitted. */
        } else if (dir != GI_DIRECTION_OUT) {
            if (loxIndex < argCount) {
                slots[i].value = args[loxIndex];
                slots[i].present = true;
                loxIndex++;
            }
            totalIn++;
        }
        if (!slots[i].skip && dir != GI_DIRECTION_IN) totalOut++;
        g_base_info_unref((GIBaseInfo*)argInfo);
    }

    if (loxIndex < argCount) {
        giSetError(err, "gi: constructor for %s takes at most %d arguments",
                klass->name->chars, loxIndex);
        g_free(slots);
        giCallClear(&call);
        if (ownsConstructor) g_base_info_unref((GIBaseInfo*)constructor);
        return NIL_VAL;
    }

    GIArgument* inArgs = g_new0(GIArgument, (gsize)(totalIn > 0 ? totalIn : 1));
    GIArgument* outArgs = g_new0(GIArgument, (gsize)(totalOut > 0 ? totalOut : 1));
    GiOutValue* outValues = g_new0(GiOutValue, (gsize)(totalOut > 0 ? totalOut : 1));

    bool failed = false;
    int inIndex = 0;
    int outIndex = 0;

    for (int i = 0; i < nArgs && !failed; i++) {
        GIArgInfo* argInfo = g_callable_info_get_arg(callable, i);
        if (slots[i].skip) {
            g_base_info_unref((GIBaseInfo*)argInfo);
            continue;
        }
        GIDirection dir = g_arg_info_get_direction(argInfo);
        GITypeInfo* typeInfo = g_arg_info_get_type(argInfo);

        if (dir == GI_DIRECTION_IN || dir == GI_DIRECTION_INOUT) {
            GIArgument converted;
            memset(&converted, 0, sizeof(converted));
            if (slots[i].present) {
                if (!giConvertInputValue(&call, slots[i].value, typeInfo, argInfo,
                            slots, &converted, err)) {
                    failed = true;
                }
            } else if (!g_arg_info_is_optional(argInfo)) {
                giSetError(err, "gi: constructor for %s is missing required argument '%s'",
                        klass->name->chars, g_base_info_get_name((GIBaseInfo*)argInfo));
                failed = true;
            }
            if (!failed && dir == GI_DIRECTION_IN) {
                inArgs[inIndex++] = converted;
            } else if (!failed) {
                giInitOutValue(&outValues[outIndex], i, argInfo, typeInfo);
                if (!giAllocateOutStorage(&outValues[outIndex], err)) failed = true;
                else {
                    giStoreInputInOut(&outValues[outIndex], &converted);
                    inArgs[inIndex++].v_pointer = outValues[outIndex].storage;
                    outArgs[outIndex].v_pointer = outValues[outIndex].storage;
                    outIndex++;
                }
            }
        } else if (dir == GI_DIRECTION_OUT) {
            giInitOutValue(&outValues[outIndex], i, argInfo, typeInfo);
            if (!giAllocateOutStorage(&outValues[outIndex], err)) failed = true;
            else {
                outArgs[outIndex].v_pointer = outValues[outIndex].storage;
                outIndex++;
            }
        }

        g_base_info_unref((GIBaseInfo*)typeInfo);
        g_base_info_unref((GIBaseInfo*)argInfo);
    }

    GIArgument retval;
    memset(&retval, 0, sizeof(retval));
    GError* gerror = NULL;

    if (!failed) {
        if (!giCallInvoke(constructor, inArgs, inIndex, outArgs, outIndex,
                    &retval, &gerror)) {
            giSetError(err, "gi: constructor for %s failed: %s",
                    klass->name->chars, gerror ? gerror->message : "unknown error");
            failed = true;
        }
    }

    Value result = NIL_VAL;
    if (!failed) {
        GITypeInfo* returnType = g_callable_info_get_return_type(callable);
        if (g_type_info_get_tag(returnType) == GI_TYPE_TAG_VOID) {
            result = NIL_VAL;
        } else {
            result = giWrapConstructorResult(&retval, returnType, klass, err);
        }
        g_base_info_unref((GIBaseInfo*)returnType);
    }

    if (!failed && outIndex > 0) {
        /* Building the result array can collect the return value before it
         * has been stored, so root it across the allocation. */
        pushTemp(result);
        ObjArray* outs = newArray();
        pushTemp(OBJ_VAL(outs));
        giArrayAppendValue(outs, result);
        for (int i = 0; i < outIndex; i++) {
            GIArgInfo* argInfo = outValues[i].argInfo;
            GITransfer transfer = argInfo != NULL
                ? g_arg_info_get_ownership_transfer(argInfo)
                : GI_TRANSFER_NOTHING;
            giArrayAppendValue(outs, giReadOutValue(err, &outValues[i], transfer, outValues, outIndex));
        }
        popTemp();
        popTemp();
        result = OBJ_VAL(outs);
    }

    for (int i = 0; i < outIndex; i++) giClearOutValue(&outValues[i]);
    g_free(outValues);
    g_free(inArgs);
    g_free(outArgs);
    g_free(slots);
    if (gerror != NULL) g_error_free(gerror);
    if (ownsConstructor) g_base_info_unref((GIBaseInfo*)constructor);
    giCallClear(&call);
    return result;
}

static Value giInvokeConstructor(ObjClass* klass, int argCount, Value* args,
        GiError* err) {
    return giInvokeConstructorInfo(klass, NULL, argCount, args, err);
}

static ObjClass* giClassFromConstructor(GIFunctionInfo* info) {
    GITypeInfo* returnType = g_callable_info_get_return_type((GICallableInfo*)info);
    ObjClass* klass = NULL;

    if (returnType != NULL) {
        if (g_type_info_get_tag(returnType) == GI_TYPE_TAG_INTERFACE) {
            GIBaseInfo* interface = g_type_info_get_interface(returnType);
            if (interface != NULL) {
                klass = giRegisterClass(interface);
                g_base_info_unref(interface);
            }
        }
        g_base_info_unref((GIBaseInfo*)returnType);
    }
    return klass;
}

static Value giInvokeFunction(GIFunctionInfo* info, int argCount, Value* args,
        Value receiver, bool hasReceiver, GiError* err) {
    GICallableInfo* callable = (GICallableInfo*)info;
    bool isMethod = giCallableHasSelf(info);
    bool isConstructor = (g_function_info_get_flags(info) & GI_FUNCTION_IS_CONSTRUCTOR) != 0;

    const char* functionName = g_base_info_get_name((GIBaseInfo*)info);
    if (functionName == NULL) functionName = "function";

    if (isConstructor) {
        /* Invoked explicitly as Class.new(...) or Class.new_with_foo(...).
         * Class fields are called with the native itself in the receiver slot,
         * so recover the wrapper class from the constructor's return type. */
        ObjClass* klass = IS_CLASS(receiver) ? AS_CLASS(receiver)
            : giClassFromConstructor(info);
        return giInvokeConstructorInfo(klass, info, argCount, args, err);
    }

    Value self = NIL_VAL;
    Value* loxArgs = args;
    int loxArgCount = argCount;

    if (isMethod) {
        if (hasReceiver && IS_INSTANCE(receiver)) {
            self = receiver;
        } else if (hasReceiver && IS_CLASS(receiver) && argCount > 0 && IS_INSTANCE(args[0])) {
            self = args[0];
            loxArgs = args + 1;
            loxArgCount = argCount - 1;
        } else if (argCount > 0 && IS_INSTANCE(args[0])) {
            self = args[0];
            loxArgs = args + 1;
            loxArgCount = argCount - 1;
        } else {
            giSetError(err, "gi: %s expects a receiver object", functionName);
            return NIL_VAL;
        }

        if (giMetaForValue(self) == NULL) {
            giSetError(err, "gi: %s expects a native receiver", functionName);
            return NIL_VAL;
        }
    }

    int nArgs = g_callable_info_get_n_args(callable);
    GiArgSlot* slots = g_new0(GiArgSlot, (gsize)(nArgs > 0 ? nArgs : 1));
    GiCall call;
    giCallInit(&call);

    int loxIndex = 0;
    int totalIn = isMethod ? 1 : 0;
    int totalOut = 0;

    for (int i = 0; i < nArgs; i++) {
        GIArgInfo* argInfo = g_callable_info_get_arg(callable, i);
        bool skip = g_arg_info_is_skip(argInfo);
        GIDirection dir = g_arg_info_get_direction(argInfo);
        slots[i].skip = skip;

        if (!skip && dir != GI_DIRECTION_OUT) {
            if (loxIndex < loxArgCount) {
                slots[i].value = loxArgs[loxIndex];
                slots[i].present = true;
                loxIndex++;
            }
            totalIn++;
        }
        if (!skip && dir != GI_DIRECTION_IN) totalOut++;
        g_base_info_unref((GIBaseInfo*)argInfo);
    }

    if (loxIndex < loxArgCount) {
        giSetError(err, "gi: %s takes at most %d arguments", functionName, loxIndex);
        g_free(slots);
        giCallClear(&call);
        return NIL_VAL;
    }

    /* Array lengths supplied by the caller are overridden with the real
     * length of the converted Lox array. */
    int* forcedLengths = g_new0(int, (gsize)(nArgs > 0 ? nArgs : 1));
    bool* hasForcedLength = g_new0(bool, (gsize)(nArgs > 0 ? nArgs : 1));
    for (int i = 0; i < nArgs; i++) {
        if (slots[i].skip || !slots[i].present) continue;
        GIArgInfo* argInfo = g_callable_info_get_arg(callable, i);
        GITypeInfo* typeInfo = g_arg_info_get_type(argInfo);
        if (g_type_info_get_tag(typeInfo) == GI_TYPE_TAG_ARRAY) {
            int lengthIndex = g_type_info_get_array_length(typeInfo);
            if (lengthIndex >= 0) {
                GITypeInfo* elementType = g_type_info_get_param_type(typeInfo, 0);
                if (elementType != NULL && IS_STRING(slots[i].value) &&
                        g_type_info_get_tag(elementType) == GI_TYPE_TAG_UINT8) {
                    forcedLengths[lengthIndex] = AS_STRING(slots[i].value)->length;
                } else if (IS_ARRAY(slots[i].value)) {
                    forcedLengths[lengthIndex] = AS_ARRAY(slots[i].value)->count;
                }
                if (elementType != NULL) g_base_info_unref((GIBaseInfo*)elementType);
                hasForcedLength[lengthIndex] = true;
            }
        }
        g_base_info_unref((GIBaseInfo*)typeInfo);
        g_base_info_unref((GIBaseInfo*)argInfo);
    }

    GIArgument* inArgs = g_new0(GIArgument, (gsize)(totalIn > 0 ? totalIn : 1));
    GIArgument* outArgs = g_new0(GIArgument, (gsize)(totalOut > 0 ? totalOut : 1));
    GiOutValue* outValues = g_new0(GiOutValue, (gsize)(totalOut > 0 ? totalOut : 1));

    bool failed = false;
    int inIndex = 0;
    int outIndex = 0;

    if (isMethod) {
        inArgs[inIndex++].v_pointer = giPointerOf(self);
    }

    for (int i = 0; i < nArgs && !failed; i++) {
        GIArgInfo* argInfo = g_callable_info_get_arg(callable, i);
        if (slots[i].skip) {
            g_base_info_unref((GIBaseInfo*)argInfo);
            continue;
        }

        GIDirection dir = g_arg_info_get_direction(argInfo);
        GITypeInfo* typeInfo = g_arg_info_get_type(argInfo);
        Value input = slots[i].value;
        if (hasForcedLength[i]) input = INT_VAL(forcedLengths[i]);

        if (dir == GI_DIRECTION_IN || dir == GI_DIRECTION_INOUT) {
            GIArgument converted;
            memset(&converted, 0, sizeof(converted));

            if (slots[i].present || hasForcedLength[i]) {
                if (!giConvertInputValue(&call, input, typeInfo, argInfo,
                            slots, &converted, err)) {
                    failed = true;
                }
            } else if (!g_arg_info_is_optional(argInfo)) {
                giSetError(err, "gi: %s is missing required argument '%s'",
                        functionName, g_base_info_get_name((GIBaseInfo*)argInfo));
                failed = true;
            }

            if (!failed && dir == GI_DIRECTION_IN) {
                inArgs[inIndex++] = converted;
            } else if (!failed) {
                giInitOutValue(&outValues[outIndex], i, argInfo, typeInfo);
                if (!giAllocateOutStorage(&outValues[outIndex], err)) {
                    failed = true;
                } else {
                    giStoreInputInOut(&outValues[outIndex], &converted);
                    inArgs[inIndex++].v_pointer = outValues[outIndex].storage;
                    outArgs[outIndex].v_pointer = outValues[outIndex].storage;
                    outIndex++;
                }
            }
        } else if (dir == GI_DIRECTION_OUT) {
            giInitOutValue(&outValues[outIndex], i, argInfo, typeInfo);
            if (!giAllocateOutStorage(&outValues[outIndex], err)) {
                failed = true;
            } else {
                outArgs[outIndex].v_pointer = outValues[outIndex].storage;
                outIndex++;
            }
        }

        g_base_info_unref((GIBaseInfo*)typeInfo);
        g_base_info_unref((GIBaseInfo*)argInfo);
    }

    GIArgument retval;
    memset(&retval, 0, sizeof(retval));
    GError* gerror = NULL;

    if (!failed) {
        if (!giCallInvoke(info, inArgs, inIndex, outArgs, outIndex, &retval, &gerror)) {
            giSetError(err, "gi: %s failed: %s", functionName,
                    gerror ? gerror->message : "unknown error");
            failed = true;
        }
    }

    Value result = NIL_VAL;
    if (!failed) {
        GITypeInfo* returnType = g_callable_info_get_return_type(callable);
        GITransfer returnTransfer = g_callable_info_get_caller_owns(callable);
        if (g_type_info_get_tag(returnType) != GI_TYPE_TAG_VOID) {
            result = giArgumentToLox(err, &retval, returnType, returnTransfer);
        }
        g_base_info_unref((GIBaseInfo*)returnType);
    }

    if (!failed && outIndex > 0) {
        /* Building the result array can collect the return value before it
         * has been stored, so root it across the allocation. */
        pushTemp(result);
        ObjArray* outs = newArray();
        pushTemp(OBJ_VAL(outs));
        giArrayAppendValue(outs, result);
        for (int i = 0; i < outIndex; i++) {
            GIArgInfo* argInfo = outValues[i].argInfo;
            GITransfer transfer = argInfo != NULL
                ? g_arg_info_get_ownership_transfer(argInfo)
                : GI_TRANSFER_NOTHING;
            giArrayAppendValue(outs, giReadOutValue(err, &outValues[i], transfer, outValues, outIndex));
        }
        popTemp();
        popTemp();
        result = OBJ_VAL(outs);
    }

    for (int i = 0; i < outIndex; i++) giClearOutValue(&outValues[i]);
    g_free(outValues);
    g_free(inArgs);
    g_free(outArgs);
    g_free(forcedLengths);
    g_free(hasForcedLength);
    g_free(slots);
    if (gerror != NULL) g_error_free(gerror);
    giCallClear(&call);
    return result;
}

static Value giInvokeNative(int argCount, Value* args) {
    ObjNative* native = vm.currentNative;

    /* When a method was fetched as a class value (Class.method(obj, ...)),
     * currentNative is still the right metadata holder. */
    if (native == NULL || native->foreignData == NULL) {
        runtimeError("gi: undefined GI function or method.");
        return NIL_VAL;
    }

    GIFunctionInfo* info = (GIFunctionInfo*)native->foreignData;
    bool hasReceiver = argCount >= 0;
    Value receiver = argCount >= 0 ? args[-1] : NIL_VAL;

    GiError err;
    memset(&err, 0, sizeof(err));
    Value result = giInvokeFunction(info, argCount, args, receiver, hasReceiver, &err);
    if (err.set) return giFail(&err);
    return result;
}

static Value giClassCallHandler(int argCount, Value* args) {
    ObjClass* klass = AS_CLASS(args[-1]);
    GiError err;
    memset(&err, 0, sizeof(err));
    Value result = giInvokeConstructor(klass, argCount, args, &err);
    if (err.set) return giFail(&err);
    return result;
}

/* ------------------------------------------------------------------------- */
/* Wrapping native values                                                    */
/* ------------------------------------------------------------------------- */

static void giObjectWeakNotify(gpointer data, GObject* where) {
    (void)where;
    GiMeta* meta = (GiMeta*)data;
    if (meta == NULL) return;

    meta->alive = false;
    meta->gobj = NULL;
    meta->ptr = NULL;
    if (meta->instance != NULL) {
        meta->instance->foreignPtr = NULL;
    }
}

static Value giWrapGObject(GObject* obj, ObjClass* klassHint, bool ownsReference) {
    if (obj == NULL) return NIL_VAL;
    giInitTables();

    GiMeta* existing = (GiMeta*)g_object_get_data(obj, GI_WRAPPER_KEY);
    if (existing != NULL && existing->instance != NULL && existing->alive) {
        if (ownsReference) {
            if (g_object_is_floating(obj)) g_object_ref_sink(obj);
            g_object_unref(obj);
        }
        return OBJ_VAL(existing->instance);
    }

    ObjClass* klass = klassHint != NULL ? klassHint : giClassForObject(obj);
    if (klass == NULL) klass = vm.objectClass;

    ObjInstance* instance = newInstance(klass);
    pushTemp(OBJ_VAL(instance));

    if (g_object_is_floating(obj)) {
        g_object_ref_sink(obj);
    } else if (!ownsReference) {
        g_object_ref(obj);
    }

    GiMeta* meta = g_new0(GiMeta, 1);
    meta->kind = GI_WRAP_OBJECT;
    meta->type = G_OBJECT_TYPE(obj);
    meta->ptr = obj;
    meta->gobj = obj;
    meta->alive = true;
    meta->instance = instance;
    meta->handlers = g_array_new(FALSE, FALSE, sizeof(guint));

    instance->foreignPtr = obj;
    g_hash_table_insert(g_instanceMeta, instance, meta);
    g_object_set_data(obj, GI_WRAPPER_KEY, meta);
    g_object_weak_ref(obj, giObjectWeakNotify, meta);

    popTemp();
    return OBJ_VAL(instance);
}

static Value giWrapBoxed(GType type, gpointer ptr, ObjClass* klassHint, bool ownsReference) {
    if (ptr == NULL) return NIL_VAL;
    giInitTables();

    gpointer owned = ptr;
    if (type == G_TYPE_VARIANT) {
        /* GVariant is a separate fundamental type, so g_boxed_copy/free do not
         * accept it even though it is reference counted like a boxed value. */
        if (!ownsReference) {
            if (g_variant_is_floating((GVariant*)ptr)) g_variant_ref_sink((GVariant*)ptr);
            else g_variant_ref((GVariant*)ptr);
        } else if (g_variant_is_floating((GVariant*)ptr)) {
            g_variant_ref_sink((GVariant*)ptr);
        }
    } else if (!ownsReference) {
        if (type == G_TYPE_INVALID || type == 0 || !g_type_is_a(type, G_TYPE_BOXED)) {
            return NIL_VAL;
        }
        owned = g_boxed_copy(type, ptr);
        if (owned == NULL) return NIL_VAL;
    }

    ObjClass* klass = klassHint != NULL ? klassHint : giClassForGType(type);
    if (klass == NULL) klass = vm.objectClass;

    ObjInstance* instance = newInstance(klass);
    pushTemp(OBJ_VAL(instance));

    GiMeta* meta = g_new0(GiMeta, 1);
    meta->kind = GI_WRAP_BOXED;
    meta->type = type;
    meta->ptr = owned;
    meta->alive = true;
    meta->instance = instance;
    meta->handlers = g_array_new(FALSE, FALSE, sizeof(guint));

    instance->foreignPtr = owned;
    g_hash_table_insert(g_instanceMeta, instance, meta);

    popTemp();
    return OBJ_VAL(instance);
}

static Value giWrapRaw(GType type, gpointer ptr, ObjClass* klassHint, bool ownsReference) {
    if (ptr == NULL) return NIL_VAL;
    giInitTables();

    gpointer owned = ptr;
    if (!ownsReference) {
        /* Raw pointers cannot be copied safely without a boxed type. */
        return NIL_VAL;
    }

    ObjClass* klass = klassHint != NULL ? klassHint : giClassForGType(type);
    if (klass == NULL) klass = vm.objectClass;

    ObjInstance* instance = newInstance(klass);
    pushTemp(OBJ_VAL(instance));

    GiMeta* meta = g_new0(GiMeta, 1);
    meta->kind = GI_WRAP_RAW;
    meta->type = type;
    meta->ptr = owned;
    meta->alive = true;
    meta->instance = instance;
    meta->handlers = g_array_new(FALSE, FALSE, sizeof(guint));

    instance->foreignPtr = owned;
    g_hash_table_insert(g_instanceMeta, instance, meta);

    popTemp();
    return OBJ_VAL(instance);
}

static void giInstanceDestructor(ObjInstance* instance) {
    GiMeta* meta = giMetaFor(instance);
    if (meta == NULL) return;

    if (meta->kind == GI_WRAP_OBJECT && meta->gobj != NULL && meta->alive) {
        GObject* obj = meta->gobj;
        if (meta->handlers != NULL) {
            for (guint i = 0; i < meta->handlers->len; i++) {
                guint id = g_array_index(meta->handlers, guint, i);
                if (g_signal_handler_is_connected(obj, id)) {
                    g_signal_handler_disconnect(obj, id);
                }
            }
        }
        g_object_set_data(obj, GI_WRAPPER_KEY, NULL);
        g_object_weak_unref(obj, giObjectWeakNotify, meta);
        g_object_unref(obj);
    } else if (meta->kind == GI_WRAP_BOXED) {
        if (meta->ptr != NULL && meta->type == G_TYPE_VARIANT) {
            g_variant_unref((GVariant*)meta->ptr);
        } else if (meta->ptr != NULL && meta->type != G_TYPE_INVALID && meta->type != 0) {
            g_boxed_free(meta->type, meta->ptr);
        }
    } else if (meta->kind == GI_WRAP_RAW) {
        g_free(meta->ptr);
    }

    if (meta->handlers != NULL) g_array_free(meta->handlers, TRUE);
    g_hash_table_remove(g_instanceMeta, instance);
    g_free(meta);
    instance->foreignPtr = NULL;
}

/* ------------------------------------------------------------------------- */
/* GValue conversions                                                        */
/* ------------------------------------------------------------------------- */

static Value giGValueToLox(GValue* value);

static bool giLoxToGValue(Value input, GValue* value, GiError* err) {
    GType type = G_VALUE_TYPE(value);

    if (type == G_TYPE_BOOLEAN) {
        if (!IS_BOOL(input) && !giIsNumeric(input)) {
            giSetError(err, "gi: expected a boolean");
            return false;
        }
        g_value_set_boolean(value, IS_BOOL(input) ? AS_BOOL(input) : giAsInt(input) != 0);
        return true;
    }
    if (type == G_TYPE_INT) {
        if (!giIsNumeric(input)) { giSetError(err, "gi: expected a number"); return false; }
        g_value_set_int(value, (gint)giAsInt(input));
        return true;
    }
    if (type == G_TYPE_UINT) {
        if (!giIsNumeric(input)) { giSetError(err, "gi: expected a number"); return false; }
        g_value_set_uint(value, (guint)giAsInt(input));
        return true;
    }
    if (type == G_TYPE_INT64) {
        if (!giIsNumeric(input)) { giSetError(err, "gi: expected a number"); return false; }
        g_value_set_int64(value, (gint64)giAsInt(input));
        return true;
    }
    if (type == G_TYPE_UINT64) {
        if (!giIsNumeric(input)) { giSetError(err, "gi: expected a number"); return false; }
        g_value_set_uint64(value, (guint64)giAsInt(input));
        return true;
    }
    if (type == G_TYPE_FLOAT) {
        if (!giIsNumeric(input)) { giSetError(err, "gi: expected a number"); return false; }
        g_value_set_float(value, (gfloat)giAsDouble(input));
        return true;
    }
    if (type == G_TYPE_DOUBLE) {
        if (!giIsNumeric(input)) { giSetError(err, "gi: expected a number"); return false; }
        g_value_set_double(value, giAsDouble(input));
        return true;
    }
    if (type == G_TYPE_STRING) {
        if (IS_NIL(input)) { g_value_set_string(value, NULL); return true; }
        if (!IS_STRING(input)) { giSetError(err, "gi: expected a string"); return false; }
        g_value_set_string(value, AS_CSTRING(input));
        return true;
    }
    if (G_TYPE_IS_ENUM(type) || G_TYPE_IS_FLAGS(type)) {
        if (!giIsNumeric(input)) { giSetError(err, "gi: expected an enum value"); return false; }
        g_value_set_enum(value, (gint)giAsInt(input));
        return true;
    }
    if (G_TYPE_IS_OBJECT(type)) {
        if (IS_NIL(input)) { g_value_set_object(value, NULL); return true; }
        GiMeta* meta = giMetaForValue(input);
        if (meta == NULL || meta->kind != GI_WRAP_OBJECT) {
            giSetError(err, "gi: expected an object");
            return false;
        }
        g_value_set_object(value, meta->gobj);
        return true;
    }
    if (G_TYPE_IS_BOXED(type)) {
        if (IS_NIL(input)) { g_value_set_boxed(value, NULL); return true; }
        GiMeta* meta = giMetaForValue(input);
        if (meta == NULL) {
            giSetError(err, "gi: expected a native value");
            return false;
        }
        g_value_set_boxed(value, meta->ptr);
        return true;
    }
    if (type == G_TYPE_POINTER) {
        if (IS_NIL(input)) { g_value_set_pointer(value, NULL); return true; }
        GiMeta* meta = giMetaForValue(input);
        g_value_set_pointer(value, meta != NULL ? meta->ptr : NULL);
        return true;
    }
    if (type == G_TYPE_VARIANT) {
        if (IS_NIL(input)) { g_value_set_variant(value, NULL); return true; }
        if (IS_STRING(input)) {
            g_value_take_variant(value, g_variant_new_string(AS_CSTRING(input)));
            return true;
        }
        GiMeta* meta = giMetaForValue(input);
        if (meta != NULL && meta->type == G_TYPE_VARIANT) {
            g_value_set_variant(value, (GVariant*)meta->ptr);
            return true;
        }
        giSetError(err, "gi: expected a variant or string");
        return false;
    }

    giSetError(err, "gi: unsupported GValue type %s", g_type_name(type));
    return false;
}

static Value giGValueToLox(GValue* value) {
    GType type = G_VALUE_TYPE(value);

    if (type == G_TYPE_BOOLEAN) return BOOL_VAL(g_value_get_boolean(value));
    if (type == G_TYPE_INT) return INT_VAL(g_value_get_int(value));
    if (type == G_TYPE_UINT) return INT_VAL(g_value_get_uint(value));
    if (type == G_TYPE_LONG) return INT_VAL(g_value_get_long(value));
    if (type == G_TYPE_ULONG) return INT_VAL((int64_t)g_value_get_ulong(value));
    if (type == G_TYPE_INT64) return INT_VAL(g_value_get_int64(value));
    if (type == G_TYPE_UINT64) return INT_VAL((int64_t)g_value_get_uint64(value));
    if (type == G_TYPE_FLOAT) return FLOAT_VAL((double)g_value_get_float(value));
    if (type == G_TYPE_DOUBLE) return FLOAT_VAL(g_value_get_double(value));
    if (type == G_TYPE_STRING) {
        const char* text = g_value_get_string(value);
        return text == NULL ? NIL_VAL : OBJ_VAL(copyString(text, (int)strlen(text)));
    }
    if (G_TYPE_IS_ENUM(type)) return INT_VAL(g_value_get_enum(value));
    if (G_TYPE_IS_FLAGS(type)) return INT_VAL((int64_t)g_value_get_flags(value));
    if (G_TYPE_IS_OBJECT(type)) {
        GObject* obj = g_value_get_object(value);
        return obj == NULL ? NIL_VAL : giWrapGObject(obj, NULL, false);
    }
    if (type == G_TYPE_STRV) {
        char** strings = (char**)g_value_get_boxed(value);
        if (strings == NULL) return NIL_VAL;
        ObjArray* array = newArray();
        pushTemp(OBJ_VAL(array));
        for (int i = 0; strings[i] != NULL; i++) {
            giArrayAppendValue(array, OBJ_VAL(copyString(strings[i], (int)strlen(strings[i]))));
        }
        popTemp();
        return OBJ_VAL(array);
    }
    if (type == G_TYPE_VARIANT_TYPE) {
        const GVariantType* variantType = g_value_get_boxed(value);
        if (variantType == NULL) return NIL_VAL;
        char* text = g_variant_type_dup_string(variantType);
        Value result = OBJ_VAL(copyString(text, (int)strlen(text)));
        g_free(text);
        return result;
    }
    if (G_TYPE_IS_BOXED(type)) {
        gpointer boxed = g_value_get_boxed(value);
        if (boxed == NULL) return NIL_VAL;
        return giWrapBoxed(type, boxed, NULL, false);
    }
    if (type == G_TYPE_POINTER) {
        gpointer ptr = g_value_get_pointer(value);
        return ptr == NULL ? NIL_VAL : INT_VAL((int64_t)(intptr_t)ptr);
    }
    if (type == G_TYPE_VARIANT) {
        GVariant* variant = g_value_get_variant(value);
        if (variant == NULL) return NIL_VAL;
        if (g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING)) {
            gsize length = 0;
            const char* text = g_variant_get_string(variant, &length);
            return OBJ_VAL(copyString(text, (int)length));
        }
        char* text = g_variant_print(variant, TRUE);
        Value result = OBJ_VAL(copyString(text, (int)strlen(text)));
        g_free(text);
        return result;
    }
    return NIL_VAL;
}

/* ------------------------------------------------------------------------- */
/* Property and struct-field access                                          */
/* ------------------------------------------------------------------------- */

static bool giTryStructField(ObjClass* klass, GiMeta* meta, ObjString* name,
        Value* result, bool* found) {
    GIBaseInfo* info = giClassInfo(klass);
    if (info == NULL || meta == NULL || meta->ptr == NULL) return false;

    GIInfoType type = g_base_info_get_type(info);
    GIFieldInfo* field = NULL;

    if (type == GI_INFO_TYPE_STRUCT) {
        field = g_struct_info_find_field((GIStructInfo*)info, name->chars);
    } else if (type == GI_INFO_TYPE_UNION) {
        int nFields = g_union_info_get_n_fields((GIUnionInfo*)info);
        for (int i = 0; i < nFields; i++) {
            GIFieldInfo* candidate = g_union_info_get_field((GIUnionInfo*)info, i);
            const char* fieldName = g_base_info_get_name((GIBaseInfo*)candidate);
            if (fieldName != NULL && strcmp(fieldName, name->chars) == 0) {
                field = candidate;
                break;
            }
            g_base_info_unref((GIBaseInfo*)candidate);
        }
    }

    if (field == NULL) return false;

    GITypeInfo* fieldType = g_field_info_get_type(field);
    GIArgument value;
    memset(&value, 0, sizeof(value));

    if (g_field_info_get_field(field, meta->ptr, &value)) {
        *result = giArgumentToLox(NULL, &value, fieldType, GI_TRANSFER_NOTHING);
        *found = true;
    }

    g_base_info_unref((GIBaseInfo*)fieldType);
    g_base_info_unref((GIBaseInfo*)field);
    return *found;
}

static Value giPropertyGetter(Value receiver, ObjString* name) {
    if (!IS_INSTANCE(receiver)) return NIL_VAL;
    ObjInstance* instance = AS_INSTANCE(receiver);
    GiMeta* meta = giMetaFor(instance);
    if (meta == NULL || meta->ptr == NULL) return NIL_VAL;

    if (meta->kind == GI_WRAP_OBJECT) {
        GObject* obj = meta->gobj;
        if (obj == NULL) return NIL_VAL;

        GObjectClass* objClass = G_OBJECT_GET_CLASS(obj);
        GParamSpec* spec = giFindProperty(objClass, name);
        if (spec == NULL || !(spec->flags & G_PARAM_READABLE)) return NIL_VAL;

        char* propertyName = giPropertyNameForGet(objClass, name);
        if (propertyName == NULL) propertyName = g_strdup(name->chars);

        GValue value = G_VALUE_INIT;
        g_value_init(&value, spec->value_type);
        g_object_get_property(obj, propertyName, &value);
        Value result = giGValueToLox(&value);
        g_value_unset(&value);
        g_free(propertyName);
        return result;
    }

    Value result = NIL_VAL;
    bool found = false;
    if (giTryStructField(instance->obj.klass, meta, name, &result, &found)) {
        return result;
    }
    return NIL_VAL;
}

static Value giPropertySetter(Value receiver, ObjString* name, Value input) {
    if (!IS_INSTANCE(receiver)) return BOOL_VAL(false);
    ObjInstance* instance = AS_INSTANCE(receiver);
    GiMeta* meta = giMetaFor(instance);
    if (meta == NULL || meta->ptr == NULL) return BOOL_VAL(false);

    if (meta->kind == GI_WRAP_OBJECT) {
        GObject* obj = meta->gobj;
        if (obj == NULL) return BOOL_VAL(false);

        GObjectClass* objClass = G_OBJECT_GET_CLASS(obj);
        GParamSpec* spec = giFindProperty(objClass, name);
        if (spec == NULL || !(spec->flags & G_PARAM_WRITABLE)) return BOOL_VAL(false);

        char* propertyName = giPropertyNameForGet(objClass, name);
        if (propertyName == NULL) propertyName = g_strdup(name->chars);

        GValue value = G_VALUE_INIT;
        g_value_init(&value, spec->value_type);
        GiError err;
        memset(&err, 0, sizeof(err));
        bool ok = giLoxToGValue(input, &value, &err);
        if (ok) {
            g_object_set_property(obj, propertyName, &value);
        } else {
            runtimeError("%s", err.message);
        }
        g_value_unset(&value);
        g_free(propertyName);
        return BOOL_VAL(ok);
    }

    GIBaseInfo* info = giClassInfo(instance->obj.klass);
    if (info != NULL && GI_IS_STRUCT_INFO(info)) {
        GIFieldInfo* field = g_struct_info_find_field((GIStructInfo*)info, name->chars);
        if (field != NULL) {
            GITypeInfo* fieldType = g_field_info_get_type(field);
            GITypeTag tag = g_type_info_get_tag(fieldType);
            GIArgument value;
            memset(&value, 0, sizeof(value));
            bool ok = true;

            if (tag == GI_TYPE_TAG_BOOLEAN || giTagSize(tag) > 0) {
                if (!giIsNumeric(input) && !(tag == GI_TYPE_TAG_BOOLEAN && IS_BOOL(input))) {
                    ok = false;
                } else {
                    giArgumentInitScalar(&value, tag, input);
                }
            } else if (tag == GI_TYPE_TAG_UTF8 || tag == GI_TYPE_TAG_FILENAME) {
                ok = false; /* the struct would keep a pointer into Lox memory */
            } else if (tag == GI_TYPE_TAG_INTERFACE && g_type_info_is_pointer(fieldType)) {
                GiMeta* valueMeta = giMetaForValue(input);
                value.v_pointer = valueMeta != NULL ? valueMeta->ptr : NULL;
            } else {
                ok = false;
            }

            if (ok) g_field_info_set_field(field, meta->ptr, &value);
            g_base_info_unref((GIBaseInfo*)fieldType);
            g_base_info_unref((GIBaseInfo*)field);
            return BOOL_VAL(ok);
        }
    }

    return BOOL_VAL(false);
}

/* ------------------------------------------------------------------------- */
/* Class registration and namespace loading                                  */
/* ------------------------------------------------------------------------- */

static ObjClass* giRegisterClass(GIBaseInfo* info) {
    giInitTables();

    const char* namespace = g_base_info_get_namespace(info);
    const char* name = g_base_info_get_name(info);
    if (namespace == NULL) namespace = "";
    if (name == NULL) name = "Anonymous";

    char qualified[512];
    snprintf(qualified, sizeof(qualified), "%s.%s", namespace, name);

    ObjClass* klass = g_hash_table_lookup(g_nameClasses, qualified);
    if (klass != NULL) return klass;

    GType gtype = G_TYPE_INVALID;
    if (GI_IS_REGISTERED_TYPE_INFO(info)) {
        gtype = g_registered_type_info_get_g_type((GIRegisteredTypeInfo*)info);
    }
    if (gtype != G_TYPE_INVALID && gtype != 0) {
        klass = g_hash_table_lookup(g_typeClasses, GSIZE_TO_POINTER((gsize)gtype));
        if (klass != NULL) {
            g_hash_table_insert(g_nameClasses, g_strdup(qualified), klass);
            return klass;
        }
    }

    ObjString* className = copyString(name, (int)strlen(name));
    pushTemp(OBJ_VAL(className));
    klass = newClass(className);
    pushTemp(OBJ_VAL(klass));

    g_hash_table_insert(g_nameClasses, g_strdup(qualified), klass);
    if (gtype != G_TYPE_INVALID && gtype != 0) {
        g_hash_table_insert(g_typeClasses, GSIZE_TO_POINTER((gsize)gtype), klass);
    }

    klass->foreignData = g_base_info_ref(info);
    klass->getter = giPropertyGetter;
    klass->setter = giPropertySetter;
    klass->destructor = giInstanceDestructor;

    GIInfoType type = g_base_info_get_type(info);

    if (type == GI_INFO_TYPE_OBJECT) {
        klass->callHandler = giClassCallHandler;

        GIObjectInfo* parent = g_object_info_get_parent((GIObjectInfo*)info);
        if (parent != NULL) {
            ObjClass* parentClass = giRegisterClass((GIBaseInfo*)parent);
            if (parentClass != NULL && parentClass != klass) {
                klass->superclass = parentClass;
            }
            g_base_info_unref((GIBaseInfo*)parent);
        }
    } else if (type == GI_INFO_TYPE_STRUCT || type == GI_INFO_TYPE_UNION) {
        klass->callHandler = giClassCallHandler;
    }

    if (type == GI_INFO_TYPE_OBJECT || type == GI_INFO_TYPE_STRUCT ||
            type == GI_INFO_TYPE_UNION || type == GI_INFO_TYPE_INTERFACE) {
        giRegisterInfoMethods(klass, info, 0);

        /* Expose constructors as class fields, so Class.new(...) works. */
        int nMethods = 0;
        if (type == GI_INFO_TYPE_OBJECT) nMethods = g_object_info_get_n_methods((GIObjectInfo*)info);
        else if (type == GI_INFO_TYPE_STRUCT) nMethods = g_struct_info_get_n_methods((GIStructInfo*)info);
        else if (type == GI_INFO_TYPE_UNION) nMethods = g_union_info_get_n_methods((GIUnionInfo*)info);

        for (int i = 0; i < nMethods; i++) {
            GIFunctionInfo* method = NULL;
            if (type == GI_INFO_TYPE_OBJECT) method = g_object_info_get_method((GIObjectInfo*)info, i);
            else if (type == GI_INFO_TYPE_STRUCT) method = g_struct_info_get_method((GIStructInfo*)info, i);
            else method = g_union_info_get_method((GIUnionInfo*)info, i);

            if (g_function_info_get_flags(method) & GI_FUNCTION_IS_CONSTRUCTOR) {
                giRegisterFunction(&klass->fields, method);
            }
            g_base_info_unref((GIBaseInfo*)method);
        }
    }

    vmAnchorObj((Obj*)klass);
    vmAnchorObj((Obj*)className);

    popTemp();
    popTemp();
    return klass;
}

static Value giConvertConstant(GIConstantInfo* constant) {
    GITypeInfo* typeInfo = g_constant_info_get_type(constant);
    if (typeInfo == NULL) return NIL_VAL;

    GIArgument value;
    memset(&value, 0, sizeof(value));
    if (g_constant_info_get_value(constant, &value) < 0) {
        g_base_info_unref((GIBaseInfo*)typeInfo);
        return NIL_VAL;
    }

    Value result = giScalarToLox(&value, g_type_info_get_tag(typeInfo));
    if (g_type_info_get_tag(typeInfo) == GI_TYPE_TAG_UTF8 ||
            g_type_info_get_tag(typeInfo) == GI_TYPE_TAG_FILENAME) {
        if (value.v_string != NULL) {
            result = OBJ_VAL(copyString(value.v_string, (int)strlen(value.v_string)));
        }
    }

    g_constant_info_free_value(constant, &value);
    g_base_info_unref((GIBaseInfo*)typeInfo);
    return result;
}

static void giAddEnumValues(ObjInstance* instance, GIEnumInfo* enumInfo) {
    int nValues = g_enum_info_get_n_values(enumInfo);
    for (int i = 0; i < nValues; i++) {
        GIValueInfo* valueInfo = g_enum_info_get_value(enumInfo, i);
        const char* name = g_base_info_get_name((GIBaseInfo*)valueInfo);
        int64_t value = g_value_info_get_value(valueInfo);

        ObjString* key = copyString(name, (int)strlen(name));
        pushTemp(OBJ_VAL(key));
        tableSet(&instance->fields, key, INT_VAL(value));

        char upper[256];
        size_t len = strlen(name);
        if (len < sizeof(upper)) {
            for (size_t j = 0; j < len; j++) {
                char c = name[j];
                upper[j] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
            }
            upper[len] = '\0';
            if (strcmp(upper, name) != 0) {
                ObjString* upperKey = copyString(upper, (int)strlen(upper));
                pushTemp(OBJ_VAL(upperKey));
                tableSet(&instance->fields, upperKey, INT_VAL(value));
                popTemp();
            }
        }
        popTemp();
        g_base_info_unref((GIBaseInfo*)valueInfo);
    }
}

static ObjInstance* giNewModuleInstance(void) {
    ObjInstance* module = newInstance(vm.moduleClass);
    pushTemp(OBJ_VAL(module));
    return module;
}

static void giModuleSet(ObjInstance* module, const char* name, Value value) {
    /* The key allocation can trigger a collection, so keep the value alive
     * until it is reachable from the module table. */
    pushTemp(value);
    ObjString* key = copyString(name, (int)strlen(name));
    pushTemp(OBJ_VAL(key));
    tableSet(&module->fields, key, value);
    popTemp();
    popTemp();
}

static Value giLoadNamespace(const char* namespace, const char* version,
        GiError* err) {
    giInitTables();

    Value cached;
    if (g_hash_table_lookup_extended(g_modules, namespace, NULL,
                (gpointer*)&cached)) {
        return cached;
    }

    GError* gerror = NULL;
    if (!g_irepository_is_registered(NULL, namespace, version)) {
        if (g_irepository_require(NULL, namespace, version, 0, &gerror) == NULL) {
            giSetError(err, "gi: could not load namespace '%s': %s", namespace,
                    gerror ? gerror->message : "unknown error");
            if (gerror != NULL) g_error_free(gerror);
            return NIL_VAL;
        }
    }

    int nInfos = g_irepository_get_n_infos(NULL, namespace);
    ObjInstance* module = giNewModuleInstance();

    for (int i = 0; i < nInfos; i++) {
        GIBaseInfo* info = g_irepository_get_info(NULL, namespace, i);
        if (info == NULL) continue;

        const char* name = g_base_info_get_name(info);
        GIInfoType type = g_base_info_get_type(info);

        if (name == NULL) {
            g_base_info_unref(info);
            continue;
        }

        if (type == GI_INFO_TYPE_FUNCTION) {
            ObjNative* native = giNewInfoNative((GIFunctionInfo*)info);
            pushTemp(OBJ_VAL(native));
            native->function = giInvokeNative;
            giModuleSet(module, name, OBJ_VAL(native));
            popTemp();
        } else if (type == GI_INFO_TYPE_OBJECT || type == GI_INFO_TYPE_STRUCT ||
                type == GI_INFO_TYPE_UNION || type == GI_INFO_TYPE_INTERFACE ||
                type == GI_INFO_TYPE_BOXED) {
            ObjClass* klass = giRegisterClass(info);
            giModuleSet(module, name, OBJ_VAL(klass));
        } else if (type == GI_INFO_TYPE_ENUM || type == GI_INFO_TYPE_FLAGS) {
            ObjString* enumName = copyString(name, (int)strlen(name));
            pushTemp(OBJ_VAL(enumName));
            ObjClass* enumClass = newClass(enumName);
            pushTemp(OBJ_VAL(enumClass));
            ObjInstance* enumInstance = newInstance(enumClass);
            pushTemp(OBJ_VAL(enumInstance));
            giAddEnumValues(enumInstance, (GIEnumInfo*)info);
            giModuleSet(module, name, OBJ_VAL(enumInstance));
            popTemp();
            popTemp();
            popTemp();
        } else if (type == GI_INFO_TYPE_CONSTANT) {
            Value constant = giConvertConstant((GIConstantInfo*)info);
            giModuleSet(module, name, constant);
        }

        g_base_info_unref(info);
    }

    g_hash_table_insert(g_modules, g_strdup(namespace), module);
    vmAnchorObj((Obj*)module);
    popTemp();
    return OBJ_VAL(module);
}

static Value giLoadNative(int argCount, Value* args) {
    if (argCount < 1 || argCount > 2 || !IS_STRING(args[0])) {
        runtimeError("gi.load() expects a namespace string and an optional version string.");
        return NIL_VAL;
    }
    if (argCount == 2 && !IS_STRING(args[1])) {
        runtimeError("gi.load() version must be a string.");
        return NIL_VAL;
    }

    GiError err;
    memset(&err, 0, sizeof(err));
    Value result = giLoadNamespace(AS_CSTRING(args[0]),
            argCount == 2 ? AS_CSTRING(args[1]) : NULL, &err);
    if (err.set) return giFail(&err);
    return result;
}

/* ------------------------------------------------------------------------- */
/* Signal support                                                            */
/* ------------------------------------------------------------------------- */

static void giStubCallback(void) {
}

static void giUnwindCallbackFrames(int priorFrameCount) {
    while (vm.frameCount > priorFrameCount) {
        CallFrame* frame = &vm.frames[vm.frameCount - 1];
        while (frame->deferHooks.count > 0) {
            Value deferValue = frame->deferHooks.values[--frame->deferHooks.count];
            vmCall(AS_CLOSURE(deferValue), 0);
        }
        freeValueArray(&frame->deferHooks);
        vm.frameCount--;
    }
}

static void giRunLoxCallback(Value callback, int argCount, Value* args,
        Value* result, bool* hasResult) {
    int oldExitDepth = vm.nativeExitDepth;
    Value* callbackStackStart = vm.stackTop;
    int priorFrameCount = vm.frameCount;
    int priorTryCount = vm.tryCount;

    vm.nativeExitDepth = vm.frameCount;

    push(callback);
    for (int i = 0; i < argCount; i++) push(args[i]);

    if (callValue(callback, argCount)) {
        if (vm.frameCount > priorFrameCount) {
            InterpretResult interpretResult = run();
            if (interpretResult == INTERPRET_RUNTIME_ERROR) {
                giUnwindCallbackFrames(priorFrameCount);
            }
        }
    }

    if (vm.tryCount < priorTryCount) {
        vm.nativeExitDepth = oldExitDepth;
        return;
    }
    if (vm.tryCount > priorTryCount) {
        vm.tryCount = priorTryCount;
    }

    if (hasResult != NULL && result != NULL) {
        *result = peek(0);
        *hasResult = true;
    }

    vm.stackTop = callbackStackStart;
    vm.nativeExitDepth = oldExitDepth;
}

static void giMarshalSignal(GClosure* closure, GValue* returnValue,
        guint nParamValues, const GValue* paramValues, gpointer hint,
        gpointer marshalData) {
    (void)hint;
    (void)marshalData;

    Value callback = OBJ_VAL((Obj*)closure->data);
    Value args[32];
    int argCount = 0;
    int tempScope = saveTempScope();

    for (guint i = 0; i < nParamValues && argCount < 32; i++) {
        Value argument = giGValueToLox((GValue*)&paramValues[i]);
        /* Converting a later parameter can trigger a collection. */
        pushTemp(argument);
        args[argCount++] = argument;
    }

    Value result = NIL_VAL;
    bool hasResult = false;
    giRunLoxCallback(callback, argCount, args, &result, &hasResult);
    restoreTempScope(tempScope);

    if (returnValue != NULL && G_VALUE_TYPE(returnValue) != G_TYPE_NONE && hasResult) {
        GiError err;
        memset(&err, 0, sizeof(err));
        giLoxToGValue(result, returnValue, &err);
    }
}

static guint giFindHandlerIndex(GiMeta* meta, guint id) {
    if (meta == NULL || meta->handlers == NULL) return G_MAXUINT;
    for (guint i = 0; i < meta->handlers->len; i++) {
        if (g_array_index(meta->handlers, guint, i) == id) return i;
    }
    return G_MAXUINT;
}

static Value giConnectNative(int argCount, Value* args) {
    if (argCount < 3 || argCount > 4) {
        runtimeError("gi.connect() expects an object, a signal name, a callback, "
                "and an optional 'after' flag.");
        return NIL_VAL;
    }

    if (!IS_INSTANCE(args[0])) {
        runtimeError("gi.connect() expects a native object as its first argument.");
        return NIL_VAL;
    }
    if (!IS_STRING(args[1])) {
        runtimeError("gi.connect() expects a signal name string.");
        return NIL_VAL;
    }
    if (!isCallable(args[2])) {
        runtimeError("gi.connect() expects a callable third argument.");
        return NIL_VAL;
    }
    bool after = false;
    if (argCount == 4) {
        if (!IS_BOOL(args[3])) {
            runtimeError("gi.connect() 'after' argument must be a boolean.");
            return NIL_VAL;
        }
        after = AS_BOOL(args[3]);
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    GiMeta* meta = giMetaFor(instance);
    if (meta == NULL || meta->kind != GI_WRAP_OBJECT || meta->gobj == NULL) {
        runtimeError("gi.connect() requires a live GObject.");
        return NIL_VAL;
    }

    const char* signalName = AS_CSTRING(args[1]);
    guint signalId = g_signal_lookup(signalName, G_OBJECT_TYPE(meta->gobj));
    if (signalId == 0) {
        runtimeError("gi.connect(): signal '%s' does not exist on %s.", signalName,
                G_OBJECT_TYPE_NAME(meta->gobj));
        return NIL_VAL;
    }

    GClosure* closure = g_cclosure_new(G_CALLBACK(giStubCallback),
            (gpointer)AS_OBJ(args[2]), NULL);
    g_closure_set_marshal(closure, giMarshalSignal);

    guint handlerId = g_signal_connect_closure(meta->gobj, signalName, closure, after);
    g_array_append_val(meta->handlers, handlerId);

    char key[64];
    snprintf(key, sizeof(key), "__gi_handler_%u", handlerId);
    ObjString* fieldKey = copyString(key, (int)strlen(key));
    pushTemp(OBJ_VAL(fieldKey));
    tableSet(&instance->fields, fieldKey, args[2]);
    popTemp();

    return INT_VAL((int64_t)handlerId);
}

static Value giDisconnectNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_INSTANCE(args[0]) || !giIsNumeric(args[1])) {
        runtimeError("gi.disconnect() expects an object and a handler id.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    GiMeta* meta = giMetaFor(instance);
    guint id = (guint)giAsInt(args[1]);

    if (meta == NULL || meta->kind != GI_WRAP_OBJECT || meta->gobj == NULL) {
        runtimeError("gi.disconnect() requires a live GObject.");
        return NIL_VAL;
    }

    guint index = giFindHandlerIndex(meta, id);
    if (index == G_MAXUINT) {
        runtimeError("gi.disconnect(): handler %u is not connected to this object.", id);
        return NIL_VAL;
    }

    if (g_signal_handler_is_connected(meta->gobj, id)) {
        g_signal_handler_disconnect(meta->gobj, id);
    }
    g_array_remove_index(meta->handlers, index);

    char key[64];
    snprintf(key, sizeof(key), "__gi_handler_%u", id);
    ObjString* fieldKey = copyString(key, (int)strlen(key));
    pushTemp(OBJ_VAL(fieldKey));
    tableDelete(&instance->fields, fieldKey);
    popTemp();

    return NIL_VAL;
}

static Value giEmitNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1])) {
        runtimeError("gi.emit() expects an object, a signal name, and optional arguments.");
        return NIL_VAL;
    }

    GiMeta* meta = giMetaForValue(args[0]);
    if (meta == NULL || meta->kind != GI_WRAP_OBJECT || meta->gobj == NULL) {
        runtimeError("gi.emit() requires a live GObject.");
        return NIL_VAL;
    }

    const char* signalName = AS_CSTRING(args[1]);
    guint signalId = g_signal_lookup(signalName, G_OBJECT_TYPE(meta->gobj));
    if (signalId == 0) {
        runtimeError("gi.emit(): signal '%s' does not exist on %s.", signalName,
                G_OBJECT_TYPE_NAME(meta->gobj));
        return NIL_VAL;
    }

    GSignalQuery query;
    g_signal_query(signalId, &query);

    guint nParams = query.n_params + 1;
    GValue* params = g_new0(GValue, nParams);
    g_value_init(&params[0], G_OBJECT_TYPE(meta->gobj));
    g_value_set_object(&params[0], meta->gobj);

    GiError err;
    memset(&err, 0, sizeof(err));

    int available = argCount - 2;
    for (guint i = 1; i < nParams; i++) {
        g_value_init(&params[i], query.param_types[i - 1]);
        if ((int)(i - 1) < available) {
            if (!giLoxToGValue(args[i + 1], &params[i], &err)) {
                break;
            }
        }
    }

    GValue returnValue;
    memset(&returnValue, 0, sizeof(returnValue));
    bool hasReturn = query.return_type != G_TYPE_NONE;
    if (hasReturn) g_value_init(&returnValue, query.return_type);

    Value result = NIL_VAL;
    if (!err.set) {
        g_signal_emitv(params, signalId, 0, hasReturn ? &returnValue : NULL);
        if (hasReturn) result = giGValueToLox(&returnValue);
    }

    if (hasReturn) g_value_unset(&returnValue);
    for (guint i = 0; i < nParams; i++) g_value_unset(&params[i]);
    g_free(params);

    if (err.set) return giFail(&err);
    return result;
}

/* ------------------------------------------------------------------------- */
/* Main loop                                                                 */
/* ------------------------------------------------------------------------- */

static void giDestroyToplevelWindows(void) {
    GType windowType = g_type_from_name("GtkWindow");
    if (windowType == G_TYPE_INVALID) return;

    typedef GListModel* (*GetToplevelsFunc)(void);

    GetToplevelsFunc getToplevels =
        (GetToplevelsFunc)dlsym(RTLD_DEFAULT, "gtk_window_get_toplevels");
    typedef void (*WindowDestroyFunc)(gpointer);
    WindowDestroyFunc destroyWindow =
        (WindowDestroyFunc)dlsym(RTLD_DEFAULT, "gtk_window_destroy");

    if (getToplevels == NULL || destroyWindow == NULL) return;

    GListModel* model = getToplevels();
    if (model == NULL) return;

    guint n = g_list_model_get_n_items(model);
    for (guint i = 0; i < n; i++) {
        gpointer window = g_list_model_get_item(model, i);
        if (window != NULL) {
            if (g_type_is_a(G_OBJECT_TYPE(window), windowType)) {
                destroyWindow(window);
            }
            g_object_unref(window);
        }
    }
    g_object_unref(model);
}

static Value giQuitNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    g_giLoopRunning = false;
    giDestroyToplevelWindows();
    return NIL_VAL;
}

static Value giLoopNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;

    GMainContext* context = g_main_context_default();
    g_giLoopRunning = true;

    int oldExitDepth = vm.nativeExitDepth;

    while (g_giLoopRunning) {
        bool dispatched = false;
        while (g_main_context_pending(context)) {
            g_main_context_iteration(context, FALSE);
            dispatched = true;
            if (!g_giLoopRunning) break;
        }
        if (!g_giLoopRunning) break;

        processTimers();

        if (has_pending_signals()) {
            if (vm.frameCount > 0) run();
            process_pending_signals();
        }
        if (!g_giLoopRunning) break;

        if (!dispatched) {
            uint64_t delay = 10;
            if (hasActiveTimers()) {
                uint64_t timerDelay = getNextTimerDelayMs();
                if (timerDelay < delay) delay = timerDelay;
            }
            if (delay == 0) delay = 1;
            g_usleep((gulong)(delay * 1000));
        }
    }

    vm.nativeExitDepth = oldExitDepth;
    return NIL_VAL;
}

static Value giInspectNative(int argCount, Value* args) {
    Value receiver = argCount > 0 ? args[-1] : NIL_VAL;
    if (!IS_INSTANCE(receiver)) {
        printValueMain(receiver);
        printf("\n");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(receiver);
    GiMeta* meta = giMetaFor(instance);
    printf("--- GI instance %p ---\n", (void*)instance);
    printf("class: %s\n", instance->obj.klass != NULL ? instance->obj.klass->name->chars : "?");
    printf("foreignPtr: %p\n", instance->foreignPtr);
    if (meta != NULL) {
        printf("kind: %d type: %s alive: %d\n", (int)meta->kind,
                giTypeNameForGType(meta->type), meta->alive ? 1 : 0);
    }
    return NIL_VAL;
}

/* ------------------------------------------------------------------------- */
/* Module entry point                                                        */
/* ------------------------------------------------------------------------- */

void lox_module_init(VM* vm) {
    giInitTables();

    int scope = saveTempScope();

    ObjInstance* giModule = giNewModuleInstance();

    ObjNative* loadFn = newNative(giLoadNative);
    pushTemp(OBJ_VAL(loadFn));
    giModuleSet(giModule, "load", OBJ_VAL(loadFn));
    popTemp();

    ObjNative* loopFn = newNative(giLoopNative);
    pushTemp(OBJ_VAL(loopFn));
    giModuleSet(giModule, "loop", OBJ_VAL(loopFn));
    popTemp();

    ObjNative* quitFn = newNative(giQuitNative);
    pushTemp(OBJ_VAL(quitFn));
    giModuleSet(giModule, "quit", OBJ_VAL(quitFn));
    popTemp();

    ObjNative* connectFn = newNative(giConnectNative);
    pushTemp(OBJ_VAL(connectFn));
    giModuleSet(giModule, "connect", OBJ_VAL(connectFn));
    popTemp();

    ObjNative* disconnectFn = newNative(giDisconnectNative);
    pushTemp(OBJ_VAL(disconnectFn));
    giModuleSet(giModule, "disconnect", OBJ_VAL(disconnectFn));
    popTemp();

    ObjNative* emitFn = newNative(giEmitNative);
    pushTemp(OBJ_VAL(emitFn));
    giModuleSet(giModule, "emit", OBJ_VAL(emitFn));
    popTemp();

    ObjNative* inspectFn = newNative(giInspectNative);
    pushTemp(OBJ_VAL(inspectFn));
    giModuleSet(giModule, "inspect", OBJ_VAL(inspectFn));
    popTemp();

    ObjString* giStr = copyString("gi", 2);
    pushTemp(OBJ_VAL(giStr));
    tableSet(&vm->globals, giStr, OBJ_VAL(giModule));
    popTemp();

    vmAnchorObj((Obj*)giModule);
    restoreTempScope(scope);
}
