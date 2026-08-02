#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

#include "vm.h"

Value parse_yaml_event(yaml_parser_t* parser, yaml_event_t* event);

Value parse_scalar(yaml_event_t* event) {
    char* val = (char*)event->data.scalar.value;
    size_t len = event->data.scalar.length;

    if (len == 0 || strcmp(val, "null") == 0 || strcmp(val, "Null") == 0 ||
            strcmp(val, "NULL") == 0 || strcmp(val, "~") == 0) {
        yaml_event_delete(event);
        return NIL_VAL;
    }

    if (strcmp(val, "true") == 0 || strcmp(val, "True") == 0 || strcmp(val, "TRUE") == 0 ||
            strcmp(val, "yes") == 0) {
        yaml_event_delete(event);
        return BOOL_VAL(true);
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "False") == 0 || strcmp(val, "FALSE") == 0 ||
            strcmp(val, "no") == 0) {
        yaml_event_delete(event);
        return BOOL_VAL(false);
    }

    char* endptr;
    double num = strtod(val, &endptr);
    if (*endptr == '\0' && endptr != val) {
        yaml_event_delete(event);
        return NUMBER_VAL(num);
    }

    ObjString* str = copyString(val, (int)len);
    yaml_event_delete(event);
    return OBJ_VAL(str);
}

Value parse_yaml_event(yaml_parser_t* parser, yaml_event_t* event) {
    switch (event->type) {
        case YAML_SCALAR_EVENT:
            return parse_scalar(event);
        case YAML_SEQUENCE_START_EVENT:
            {
                yaml_event_delete(event);
                ObjArray* array = newArray();
                push(OBJ_VAL(array));

                while (1) {
                    yaml_event_t child_event;
                    if (!yaml_parser_parse(parser, &child_event)) break;
                    if (child_event.type == YAML_SEQUENCE_END_EVENT) {
                        yaml_event_delete(&child_event);
                        break;
                    }
                    Value item = parse_yaml_event(parser, &child_event);
                    arrayAppend(array, item);
                }
                pop();
                return OBJ_VAL(array);
            }
            break;
        case YAML_MAPPING_START_EVENT:
            {
                yaml_event_delete(event);
                ObjMap* map = newMap();
                push(OBJ_VAL(map));

                while (1) {
                    yaml_event_t key_event;
                    if (!yaml_parser_parse(parser, &key_event)) break;
                    if (key_event.type == YAML_MAPPING_END_EVENT) {
                        yaml_event_delete(&key_event);
                        break;
                    }

                    Value key = parse_yaml_event(parser, &key_event);
                    push(key);

                    yaml_event_t val_event;
                    if (!yaml_parser_parse(parser, &val_event)) {
                        pop();
                        break;
                    }

                    Value val = parse_yaml_event(parser, &val_event);
                    push(val);

                    mapSet(map, key, val);
                    pop();
                    pop();
                }
                pop();
                return OBJ_VAL(map);
            }
            break;
        default:
            yaml_event_delete(event);
            return NIL_VAL;

    }
}

Value parse_yaml_stream(yaml_parser_t* parser) {
    yaml_event_t event;
    Value result = NIL_VAL;

    while (1) {
        if (!yaml_parser_parse(parser, &event)) {
            return NIL_VAL;
        }

        if (event.type == YAML_DOCUMENT_START_EVENT) {
            yaml_event_delete(&event);
            if (yaml_parser_parse(parser, &event)) {
                result = parse_yaml_event(parser, &event);
            }
        } else if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        } else {
            yaml_event_delete(&event);
        }
    }
    return result;
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

    yaml_parser_set_input_string(&parser, (const unsigned char *)str->chars, str->length);
    Value root = parse_yaml_stream(&parser);

    yaml_parser_delete(&parser);
    return root;
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
    return root;
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

