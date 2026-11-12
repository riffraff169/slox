#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

#include "vm.h"

#define YAML_MAX_DEPTH 512

typedef struct {
    yaml_parser_t* parser;
    char** anchorNames;
    Value* anchorValues;
    int anchorCount;
    int anchorCapacity;
    int depth;
    bool failed;
} YamlContext;

static Value parse_yaml_event(YamlContext* ctx, yaml_event_t* event);

/* -------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */

static bool stringEqualsIgnoreCase(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int digitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool isNullScalar(const char* value, size_t length) {
    return length == 0 ||
           strcmp(value, "~") == 0 ||
           strcmp(value, "null") == 0 ||
           strcmp(value, "Null") == 0 ||
           strcmp(value, "NULL") == 0;
}

static bool parseBoolean(const char* value, bool* result) {
    if (strcmp(value, "true") == 0 || strcmp(value, "True") == 0 ||
        strcmp(value, "TRUE") == 0 || strcmp(value, "yes") == 0 ||
        strcmp(value, "Yes") == 0 || strcmp(value, "YES") == 0 ||
        strcmp(value, "on") == 0 || strcmp(value, "On") == 0 ||
        strcmp(value, "ON") == 0) {
        *result = true;
        return true;
    }

    if (strcmp(value, "false") == 0 || strcmp(value, "False") == 0 ||
        strcmp(value, "FALSE") == 0 || strcmp(value, "no") == 0 ||
        strcmp(value, "No") == 0 || strcmp(value, "NO") == 0 ||
        strcmp(value, "off") == 0 || strcmp(value, "Off") == 0 ||
        strcmp(value, "OFF") == 0) {
        *result = false;
        return true;
    }

    return false;
}

/* Parse a YAML integer, including 0x/0o/0b prefixes and '_' separators. */
static bool parseInteger(const char* value, int64_t* result) {
    const char* cursor = value;
    bool negative = false;

    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        cursor++;
    }
    if (*cursor == '\0') return false;

    int base = 10;
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16;
        cursor += 2;
    } else if (cursor[0] == '0' && (cursor[1] == 'o' || cursor[1] == 'O')) {
        base = 8;
        cursor += 2;
    } else if (cursor[0] == '0' && (cursor[1] == 'b' || cursor[1] == 'B')) {
        base = 2;
        cursor += 2;
    }

    uint64_t magnitude = 0;
    bool sawDigit = false;
    bool overflow = false;

    for (; *cursor != '\0'; cursor++) {
        if (*cursor == '_') continue;

        int digit = digitValue(*cursor);
        if (digit < 0 || digit >= base) return false;

        sawDigit = true;
        uint64_t udigit = (uint64_t)digit;
        if (magnitude > (UINT64_MAX - udigit) / (uint64_t)base) {
            overflow = true;
        } else if (!overflow) {
            magnitude = magnitude * (uint64_t)base + udigit;
        }
    }

    if (!sawDigit || overflow) return false;

    if (negative) {
        uint64_t minMagnitude = (uint64_t)INT64_MAX + 1u;
        if (magnitude > minMagnitude) return false;
        *result = (magnitude == minMagnitude) ? INT64_MIN : -(int64_t)magnitude;
    } else {
        if (magnitude > (uint64_t)INT64_MAX) return false;
        *result = (int64_t)magnitude;
    }
    return true;
}

static bool hasFloatMarker(const char* value) {
    const char* cursor = value;
    if (*cursor == '+' || *cursor == '-') cursor++;

    /* Base-prefixed integers are not floats, even if they contain 'e'/'E'. */
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X' ||
                             cursor[1] == 'o' || cursor[1] == 'O' ||
                             cursor[1] == 'b' || cursor[1] == 'B')) {
        return false;
    }

    for (; *cursor != '\0'; cursor++) {
        if (*cursor == '.' || *cursor == 'e' || *cursor == 'E') return true;
    }
    return false;
}

/* Parse a YAML floating point value, including .inf/.nan and '_' separators. */
static bool parseFloat(const char* value, double* result) {
    const char* cursor = value;
    bool negative = false;

    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        cursor++;
    }

    if (stringEqualsIgnoreCase(cursor, ".inf")) {
        *result = negative ? -INFINITY : INFINITY;
        return true;
    }
    if (stringEqualsIgnoreCase(cursor, ".nan")) {
        *result = NAN;
        return true;
    }

    /* strtod() accepts these spellings, but YAML reserves them for strings. */
    if (stringEqualsIgnoreCase(cursor, "inf") ||
        stringEqualsIgnoreCase(cursor, "infinity") ||
        stringEqualsIgnoreCase(cursor, "nan")) {
        return false;
    }

    size_t length = strlen(value);
    char* buffer = (char*)malloc(length + 1);
    if (buffer == NULL) return false;

    size_t out = 0;
    for (size_t i = 0; i < length; i++) {
        if (value[i] != '_') buffer[out++] = value[i];
    }
    buffer[out] = '\0';

    char* end = NULL;
    double number = strtod(buffer, &end);
    bool ok = end != buffer && *end == '\0';
    free(buffer);

    if (!ok) return false;
    *result = number;
    return true;
}

static bool parseNumber(const char* value, bool forceInt, bool forceFloat,
                        Value* result) {
    int64_t integer = 0;
    double floating = 0.0;
    bool integerOk = parseInteger(value, &integer);
    bool floatOk = parseFloat(value, &floating);
    bool floatForm = hasFloatMarker(value);

    if (forceInt) {
        if (!integerOk) return false;
        *result = INT_VAL(integer);
        return true;
    }

    if (forceFloat) {
        if (floatOk) {
            *result = FLOAT_VAL(floating);
            return true;
        }
        if (integerOk) {
            *result = FLOAT_VAL((double)integer);
            return true;
        }
        return false;
    }

    if (floatForm) {
        if (floatOk) {
            *result = FLOAT_VAL(floating);
            return true;
        }
        if (integerOk) {
            *result = FLOAT_VAL((double)integer);
            return true;
        }
        return false;
    }

    if (integerOk) {
        *result = INT_VAL(integer);
        return true;
    }
    if (floatOk) {
        /* An integer too large for int64_t, parsed as a float. */
        *result = FLOAT_VAL(floating);
        return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * Anchors and aliases
 * ------------------------------------------------------------------------- */

static void anchorRegister(YamlContext* ctx, const yaml_char_t* anchor,
                           Value value) {
    if (anchor == NULL) return;

    const char* name = (const char*)anchor;

    /*
     * Keep the anchored value alive for the rest of the document. Anchor
     * entries are appended to the VM's permanent root array and the array is
     * truncated when the document (or the whole parse) is finished.
     */
    push(value);
    writeValueArray(&vm.globalRoots, value);
    pop();

    for (int i = 0; i < ctx->anchorCount; i++) {
        if (strcmp(ctx->anchorNames[i], name) == 0) {
            ctx->anchorValues[i] = value;
            return;
        }
    }

    if (ctx->anchorCount == ctx->anchorCapacity) {
        int newCapacity = ctx->anchorCapacity < 8 ? 8 : ctx->anchorCapacity * 2;

        char** newNames = (char**)realloc(
            ctx->anchorNames, sizeof(char*) * (size_t)newCapacity);
        if (newNames == NULL) {
            ctx->failed = true;
            return;
        }
        ctx->anchorNames = newNames;

        Value* newValues = (Value*)realloc(
            ctx->anchorValues, sizeof(Value) * (size_t)newCapacity);
        if (newValues == NULL) {
            ctx->failed = true;
            return;
        }
        ctx->anchorValues = newValues;
        ctx->anchorCapacity = newCapacity;
    }

    size_t length = strlen(name);
    char* copy = (char*)malloc(length + 1);
    if (copy == NULL) {
        ctx->failed = true;
        return;
    }
    memcpy(copy, name, length + 1);

    ctx->anchorNames[ctx->anchorCount] = copy;
    ctx->anchorValues[ctx->anchorCount] = value;
    ctx->anchorCount++;
}

static Value anchorLookup(YamlContext* ctx, const yaml_char_t* anchor) {
    if (anchor == NULL) return NIL_VAL;

    const char* name = (const char*)anchor;
    for (int i = 0; i < ctx->anchorCount; i++) {
        if (strcmp(ctx->anchorNames[i], name) == 0) {
            return ctx->anchorValues[i];
        }
    }
    return NIL_VAL;
}

static void anchorsClear(YamlContext* ctx) {
    for (int i = 0; i < ctx->anchorCount; i++) {
        free(ctx->anchorNames[i]);
    }
    ctx->anchorCount = 0;
}

/* -------------------------------------------------------------------------
 * Event parsing
 * ------------------------------------------------------------------------- */

static bool nextEvent(YamlContext* ctx, yaml_event_t* event) {
    if (!yaml_parser_parse(ctx->parser, event)) {
        ctx->failed = true;
        return false;
    }
    if (event->type == YAML_NO_EVENT) {
        ctx->failed = true;
        return false;
    }
    return true;
}

static Value parseScalar(YamlContext* ctx, yaml_event_t* event) {
    const char* value = (const char*)event->data.scalar.value;
    size_t length = event->data.scalar.length;
    const char* tag = (const char*)event->data.scalar.tag;
    const yaml_char_t* anchor = event->data.scalar.anchor;
    bool plain = event->data.scalar.style == YAML_PLAIN_SCALAR_STYLE;

    bool explicitString = tag != NULL &&
        strcmp(tag, "tag:yaml.org,2002:str") == 0;
    bool explicitNull = tag != NULL &&
        strcmp(tag, "tag:yaml.org,2002:null") == 0;
    bool explicitBool = tag != NULL &&
        strcmp(tag, "tag:yaml.org,2002:bool") == 0;
    bool explicitInt = tag != NULL &&
        strcmp(tag, "tag:yaml.org,2002:int") == 0;
    bool explicitFloat = tag != NULL &&
        strcmp(tag, "tag:yaml.org,2002:float") == 0;

    bool implicit = !explicitString &&
        (tag == NULL || strcmp(tag, "!") == 0);
    bool infer = implicit && plain;

    Value result;

    if (explicitNull || (infer && isNullScalar(value, length))) {
        result = NIL_VAL;
    } else if (explicitBool || infer) {
        bool boolean;
        if (parseBoolean(value, &boolean)) {
            result = BOOL_VAL(boolean);
        } else if (explicitBool) {
            result = NIL_VAL;
        } else if (parseNumber(value, false, false, &result)) {
            /* result already set */
        } else {
            ObjString* string = copyString(value, (int)length);
            result = OBJ_VAL(string);
        }
    } else if (explicitInt) {
        if (!parseNumber(value, true, false, &result)) {
            ObjString* string = copyString(value, (int)length);
            result = OBJ_VAL(string);
        }
    } else if (explicitFloat) {
        if (!parseNumber(value, false, true, &result)) {
            ObjString* string = copyString(value, (int)length);
            result = OBJ_VAL(string);
        }
    } else {
        ObjString* string = copyString(value, (int)length);
        result = OBJ_VAL(string);
    }

    if (anchor != NULL) {
        anchorRegister(ctx, anchor, result);
    }

    yaml_event_delete(event);
    return result;
}

static Value parseSequence(YamlContext* ctx, yaml_event_t* event) {
    const yaml_char_t* anchor = event->data.sequence_start.anchor;

    if (++ctx->depth > YAML_MAX_DEPTH) {
        ctx->depth--;
        ctx->failed = true;
        yaml_event_delete(event);
        return NIL_VAL;
    }

    ObjArray* array = newArray();
    push(OBJ_VAL(array));

    /* The anchor name lives in the event, so register it before deleting. */
    if (anchor != NULL) {
        anchorRegister(ctx, anchor, OBJ_VAL(array));
    }
    yaml_event_delete(event);

    while (!ctx->failed) {
        yaml_event_t child;
        if (!nextEvent(ctx, &child)) break;

        if (child.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&child);
            break;
        }
        if (child.type == YAML_DOCUMENT_END_EVENT ||
            child.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&child);
            ctx->failed = true;
            break;
        }

        Value item = parse_yaml_event(ctx, &child);
        if (ctx->failed) break;

        push(item);
        arrayAppend(array, item);
        pop();
    }

    pop();
    ctx->depth--;
    return OBJ_VAL(array);
}

static Value parseMapping(YamlContext* ctx, yaml_event_t* event) {
    const yaml_char_t* anchor = event->data.mapping_start.anchor;

    if (++ctx->depth > YAML_MAX_DEPTH) {
        ctx->depth--;
        ctx->failed = true;
        yaml_event_delete(event);
        return NIL_VAL;
    }

    ObjMap* map = newMap();
    push(OBJ_VAL(map));

    /* The anchor name lives in the event, so register it before deleting. */
    if (anchor != NULL) {
        anchorRegister(ctx, anchor, OBJ_VAL(map));
    }
    yaml_event_delete(event);

    while (!ctx->failed) {
        yaml_event_t keyEvent;
        if (!nextEvent(ctx, &keyEvent)) break;

        if (keyEvent.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&keyEvent);
            break;
        }
        if (keyEvent.type == YAML_DOCUMENT_END_EVENT ||
            keyEvent.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&keyEvent);
            ctx->failed = true;
            break;
        }

        Value key = parse_yaml_event(ctx, &keyEvent);
        if (ctx->failed) break;
        push(key);

        yaml_event_t valueEvent;
        if (!nextEvent(ctx, &valueEvent)) {
            pop();
            break;
        }
        if (valueEvent.type == YAML_MAPPING_END_EVENT ||
            valueEvent.type == YAML_DOCUMENT_END_EVENT ||
            valueEvent.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&valueEvent);
            pop();
            ctx->failed = true;
            break;
        }

        Value value = parse_yaml_event(ctx, &valueEvent);
        if (ctx->failed) {
            pop();
            break;
        }
        push(value);

        mapSet(map, key, value);

        pop();
        pop();
    }

    pop();
    ctx->depth--;
    return OBJ_VAL(map);
}

static Value parse_yaml_event(YamlContext* ctx, yaml_event_t* event) {
    if (ctx->failed) {
        yaml_event_delete(event);
        return NIL_VAL;
    }

    switch (event->type) {
        case YAML_SCALAR_EVENT:
            return parseScalar(ctx, event);
        case YAML_SEQUENCE_START_EVENT:
            return parseSequence(ctx, event);
        case YAML_MAPPING_START_EVENT:
            return parseMapping(ctx, event);
        case YAML_ALIAS_EVENT: {
            Value result = anchorLookup(ctx, event->data.alias.anchor);
            yaml_event_delete(event);
            return result;
        }
        default:
            yaml_event_delete(event);
            return NIL_VAL;
    }
}

/* A single-document stream returns its root value. A stream with more than
 * one document returns an array containing each document root. */
static Value parse_yaml_stream(yaml_parser_t* parser) {
    YamlContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.parser = parser;

    Value* stackBase = vm.stackTop;
    int rootBase = vm.globalRoots.count;

    ObjArray* documents = newArray();
    push(OBJ_VAL(documents));
    int documentCount = 0;

    while (1) {
        yaml_event_t event;
        if (!nextEvent(&ctx, &event)) break;

        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }

        if (event.type != YAML_DOCUMENT_START_EVENT) {
            yaml_event_delete(&event);
            continue;
        }
        yaml_event_delete(&event);

        Value* documentBase = vm.stackTop;

        yaml_event_t rootEvent;
        if (!nextEvent(&ctx, &rootEvent)) {
            vm.stackTop = documentBase;
            break;
        }

        Value document = parse_yaml_event(&ctx, &rootEvent);

        if (!ctx.failed) {
            push(document);
            arrayAppend(documents, document);
            pop();
            documentCount++;
        }

        anchorsClear(&ctx);
        vm.globalRoots.count = rootBase;
        vm.stackTop = documentBase;

        if (ctx.failed) break;
    }

    anchorsClear(&ctx);
    free(ctx.anchorNames);
    free(ctx.anchorValues);
    vm.globalRoots.count = rootBase;

    vm.stackTop = stackBase;

    if (ctx.failed || documentCount == 0) return NIL_VAL;
    if (documentCount == 1) return documents->values[0];
    return OBJ_VAL(documents);
}

Value lox_yaml_parse(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[0]);
    yaml_parser_t parser;

    if (!yaml_parser_initialize(&parser)) {
        return NIL_VAL;
    }

    yaml_parser_set_input_string(
        &parser, (const unsigned char*)str->chars, str->length);
    Value root = parse_yaml_stream(&parser);
    yaml_parser_delete(&parser);

    push(root);
    return pop();
}

Value lox_yaml_load(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);
    FILE* file = fopen(path, "rb");
    if (!file) {
        return NIL_VAL;
    }

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        fclose(file);
        return NIL_VAL;
    }

    yaml_parser_set_input_file(&parser, file);
    Value root = parse_yaml_stream(&parser);
    yaml_parser_delete(&parser);
    fclose(file);

    push(root);
    return pop();
}

void yamlDestructor(ObjInstance* inst) {
}

void lox_module_init(VM* vm) {
    ObjString* str = copyString("Yaml", 4);
    push(OBJ_VAL(str));
    ObjClass* yamlClass = newClass(str);
    yamlClass->superclass = vm->objectClass;
    push(OBJ_VAL(yamlClass));
    tableSet(&vm->globals, str, OBJ_VAL(yamlClass));
    yamlClass->destructor = yamlDestructor;

    ObjNative* parseFn = newNative(lox_yaml_parse);
    push(OBJ_VAL(parseFn));
    tableSet(&yamlClass->methods, copyString("parse", 5), OBJ_VAL(parseFn));
    pop();

    ObjNative* loadFn = newNative(lox_yaml_load);
    push(OBJ_VAL(loadFn));
    tableSet(&yamlClass->methods, copyString("load", 4), OBJ_VAL(loadFn));
    pop();

    pop();
    pop();
}
