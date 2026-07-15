#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "native.h"
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

// 1. Setup phase: Capture the original depth and current stack pointer
#define VM_CALLBACK_INIT() \
    int _oldExitDepth = vm.nativeExitDepth; \
    Value* _callbackStackStart = vm.stackTop

// 2. Execute phase: Tell the vm where to return when the callback yields
#define VM_CALLBACK_ENTER() \
    vm.nativeExitDepth = vm.frameCount

// 3. Error Guard: Abort immediately if the loop encountered a runtime panic
#define VM_CALLBACK_CHECK_ERROR(resultState) \
    if ((resultState) == INTERPRET_RUNTIME_ERROR) { \
        vm.stackTop = _callbackStackStart; \
        vm.nativeExitDepth = _oldExitDepth; \
        return NIL_VAL; \
    }

// 4. Iteration reset: Clear the stack back to the stable start point for the next loop
#define VM_CALLBACK_RESET_STACK() \
    vm.stackTop = _callbackStackStart

// 5. Final Teardown: Restore the exit depth before returning a final value
#define VM_CALLBACK_EXIT() \
    vm.nativeExitDepth = _oldExitDepth

#define RUNTIME_ERROR(...) \
    do { \
        if (runtimeError(__VA_ARGS__)) { \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        frame = &vm.frames[vm.frameCount - 1]; \
    } while (false)

#define GET_GLOBAL(name, outValue) tableGet(&vm.globals, (name), (outValue))

void defineNativeClassConstant(ObjClass* klass, const char* name, Value value) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    tableSet(&klass->constants, AS_STRING(peek(0)), value);
    pop();
}

Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

Value strNative(int argCount, Value* args) {
    if (argCount != 1) return OBJ_VAL(copyString("", 0));

    return valueToString(args[0]);
}

Value typeofNative(int argCount, Value* args) {
    if (argCount < 1) return OBJ_VAL(vm.nilClass->name);
    ObjClass* klass = getClassForValue(args[0]);

    if (klass != NULL) {
        return OBJ_VAL(klass->name);
    }

    return OBJ_VAL(copyString("UNKNOWN", 7));
}

Value chrNative(int argCount, Value* args) {
    if (argCount != 1) {
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        return NIL_VAL;
    }

    uint8_t code = (uint8_t)AS_NUMBER(args[0]);
    char c_str[2];
    c_str[0] = (char)code;
    c_str[1] = '\0';

    return OBJ_VAL(copyString(c_str, 1));
}

Value evalNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError("eval() expects exactly 1 argument, got %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError("Argument to eval() must be a string.");
        return NIL_VAL;
    }

    ObjString* source = AS_STRING(args[0]);

    ObjFunction* function = compile(source->chars, copyString("<eval>", 6));
    if (function == NULL) {
        return NIL_VAL;
    }

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));

    VM_CALLBACK_INIT();

    VM_CALLBACK_ENTER();

    if (callValue(OBJ_VAL(closure), 0)) {
        InterpretResult result = run();

        VM_CALLBACK_CHECK_ERROR(result);

        Value evalResult = pop();

        VM_CALLBACK_EXIT();
        return evalResult;
    }
    VM_CALLBACK_EXIT();
    return NIL_VAL;
}

Value createInstanceNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("create_instance() expects a string argument.");
        return NIL_VAL;
    }

    ObjString* className = AS_STRING(args[0]);
    Value classVal;

    if (!GET_GLOBAL(className, &classVal) | !IS_CLASS(classVal)) {
        runtimeError("No such class: %s", className->chars);
        return NIL_VAL;
    }

    ObjClass* klass = AS_CLASS(classVal);
    ObjInstance* instance = newInstance(klass);
    return OBJ_VAL(instance);
}

Value programNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("program() expects a string filename argument.");
        return NIL_VAL;
    }

    ObjString* filename = AS_STRING(args[0]);
    push(OBJ_VAL(filename));
    char* source = locateAndReadLoxFile(filename->chars);

    if (source == NULL) {
        runtimeError("Could not locate or read class file '%s'.", filename->chars);
        return NIL_VAL;
    }

    const char* pathStart = filename->chars;
    const char* lastSlash = strrchr(pathStart, '/');
    if (lastSlash != NULL) {
        pathStart = lastSlash + 1;
    }

    int nameLength = (int)strlen(pathStart);
    if (nameLength > 4 && strcmp(pathStart + nameLength - 4, ".lox") == 0) {
        nameLength -= 4;
    }

    ObjString* className = copyString(pathStart, nameLength);
    push(OBJ_VAL(className));

    ObjClass* klass = newClass(className);
    push(OBJ_VAL(klass));

    klass->superclass = vm.objectClass;
    bool success = compileClassModule(source, klass);
    free(source);

    pop();
    pop();

    if (!success) {
        runtimeError("Compile error inside dynamic class '%s'.", filename->chars);
        return NIL_VAL;
    }

    return OBJ_VAL(klass);
}

static inline Value getCheckTarget(int argCount, Value* args) {
    if (IS_NATIVE(args[-1])) {
        return (argCount > 0) ? args[0] : NIL_VAL;
    }
    return args[-1];
}

Value isNumberNative(int argCount, Value* args) {
    Value target = getCheckTarget(argCount, args);
    return BOOL_VAL(IS_NUMBER(target));
}

Value isStringNative(int argCount, Value* args) {
    Value target = getCheckTarget(argCount, args);
    return BOOL_VAL(IS_STRING(target));
}

Value isBoolNative(int argCount, Value* args) {
    Value target = getCheckTarget(argCount, args);
    return BOOL_VAL(IS_BOOL(target));
}

Value isNilNative(int argCount, Value* args) {
    Value target = getCheckTarget(argCount, args);
    return BOOL_VAL(IS_NIL(target));
}

Value isClassNative(int argCount, Value* args) {
    Value target = getCheckTarget(argCount, args);
    return BOOL_VAL(IS_CLASS(target));
}

Value isInstanceNative(int argCount, Value* args) {
    Value target = getCheckTarget(argCount, args);
    return BOOL_VAL(IS_INSTANCE(target));
}

Value listFieldsNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        return OBJ_VAL(newArray());
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);

    ObjArray* array = newArray();
    push(OBJ_VAL(array));

    for (int i = 0; i < instance->fields.capacity; i++) {
        Entry* entry = &instance->fields.entries[i];
        if (entry->key != NULL) {
            arrayAppend(array, OBJ_VAL(entry->key));
        }
    }

    pop();

    return OBJ_VAL(array);
}

Value getFieldNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("get_field() expects a string argument.");
        return NIL_VAL;
    }

    Value receiver = args[-1];
    ObjString* fieldName = AS_STRING(args[0]);
    Value value;

    if (IS_INSTANCE(receiver)) {
        if (tableGet(&AS_INSTANCE(receiver)->fields, fieldName, &value)) {
            return value;
        }
    } else if (IS_CLASS(receiver)) {
        if (tableGet(&AS_CLASS(receiver)->fields, fieldName, &value)) {
            return value;
        }
    } else {
        runtimeError("Only instance and classes can have fields.");
        return NIL_VAL;
    }

    return NIL_VAL;
}

Value setFieldNative(int argCount, Value* args) {
    if (argCount != 2 || !IS_STRING(args[0])) {
        runtimeError("set_field() expects string, value arguments.");
        return NIL_VAL;
    }

    Value receiver = args[-1];
    ObjString* fieldName = AS_STRING(args[0]);
    Value value = args[1];

    if (IS_OBJ(receiver)) {
        if (IS_INSTANCE(receiver)) {
            ObjInstance* instance = AS_INSTANCE(receiver);
            if (instance->obj.klass) {
                if (instance->obj.klass->name) {
                }
            }
        }
    }
    if (IS_INSTANCE(receiver)) {
        tableSet(&AS_INSTANCE(receiver)->fields, fieldName, value);
    } else if (IS_CLASS(receiver)) {
        tableSet(&AS_CLASS(receiver)->fields, fieldName, value);
    } else {
        runtimeError("Only instances and classes can have fields.");
        return NIL_VAL;
    }
    return value;
}

Value getMethodsNative(int argCount, Value* args) {
    Value receiver = args[-1];
    ObjClass* klass = getClassForValue(receiver);

    if (klass == NULL) {
        runtimeError("Cannot get methods of a non-object/non-class.");
        return NIL_VAL;
    }

    ObjArray* list = newArray();
    push(OBJ_VAL(list));

    ObjClass* current = klass;
    while (current != NULL) {
        Table* table = &current->methods;
        for (int i = 0; i < table->capacity; i++) {
            Entry* entry = &table->entries[i];
            if (entry->key != NULL) {
                arrayAppend(list, OBJ_VAL(entry->key));
            }
        }
        current = current->superclass;
    }

    return pop();
}

Value hasMethodNative(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError("has_method() expects exactly 1 argument.");
        return BOOL_VAL(false);
    }

    if (!IS_STRING(args[0])) {
        runtimeError("has_method() expects a string argument.");
        return BOOL_VAL(false);
    }

    Value receiver = args[-1];
    ObjString* methodName = AS_STRING(args[0]);

    ObjClass* klass = getClassForValue(receiver);
    if (klass == NULL) {
        return BOOL_VAL(false);
    }

    ObjClass* current = klass;
    Value method;

    while (current != NULL) {
        if (tableGet(&current->methods, methodName, &method)) {
            return BOOL_VAL(true);
        }
        current = current->superclass;
    }
    return BOOL_VAL(false);
}

Value getSuperclassNative(int argCount, Value* args) {
    Value receiver = args[-1];
    ObjClass* klass = getClassForValue(receiver);

    if (klass == NULL) {
        runtimeError("Cannot get superclass of a non-object type.");
        return NIL_VAL;
    }

    if (klass->superclass != NULL) {
        return OBJ_VAL(klass->superclass);
    }

    return NIL_VAL;
}

Value objectToStringNative(int argCount, Value* args) {
    if (argCount != 0) return NIL_VAL;

    return valueToString(args[-1]);
}

Value objectClassMethod(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError("Expected 0 arguments but got %d.", argCount);
        return NIL_VAL;
    }

    Value receiver = args[-1];
    ObjClass* klass = getClassForValue(receiver);

    if (klass == NULL) {
        return NIL_VAL;
    }

    return OBJ_VAL(klass);
}

Value objectClassNameMethod(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    return OBJ_VAL(instance->obj.klass->name);
}

Value objectFreezeNative(int argCount, Value* args) {
    Value receiver = args[-1];

    if (IS_CLASS(receiver)) {
        AS_CLASS(receiver)->isFrozen = true;
    } else if (IS_INSTANCE(receiver)) {
        AS_INSTANCE(receiver)->isFrozen = true;
    } else {
        runtimeError("Method .freeze() can only be called on classes or instances .");
        return NIL_VAL;
    }

    return receiver;
}

Value objectIsfrozenNative(int argCount, Value* args) {
    Value receiver = args[-1];

    if (IS_CLASS(receiver)) {
        return BOOL_VAL(AS_CLASS(receiver)->isFrozen);
    } else if (IS_INSTANCE(receiver)) {
        return BOOL_VAL(AS_INSTANCE(receiver)->isFrozen);
    }
    runtimeError("Method .is_frozen() can only be called on classes or instances .");
    return NIL_VAL;
}

#define CORE_GLOBAL_LIST(X) \
    X("clock", clockNative) \
    X("str", strNative) \
    X("typeof", typeofNative) \
    X("chr", chrNative) \
    X("eval", evalNative) \
    X("create_instance", createInstanceNative) \
    X("program", programNative)

#define CORE_DUAL_LIST(X) \
    X("isnumber", isNumberNative) \
    X("isstring", isStringNative) \
    X("isbool", isBoolNative) \
    X("isnil", isNilNative) \
    X("isclass", isClassNative) \
    X("isinstance", isInstanceNative)

#define OBJECT_METHOD_LIST(X) \
    X("fields", listFieldsNative) \
    X("get_fields", listFieldsNative) \
    X("get_field", getFieldNative) \
    X("set_field", setFieldNative) \
    X("get_methods", getMethodsNative) \
    X("has_method", hasMethodNative) \
    X("responds_to", hasMethodNative) \
    X("get_superclass", getSuperclassNative) \
    X("superclass", getSuperclassNative) \
    X("to_string", objectToStringNative) \
    X("class", objectClassMethod) \
    X("class_name", objectClassNameMethod) \
    X("freeze", objectFreezeNative) \
    X("is_frozen", objectIsfrozenNative)

void initCoreLibrary() {
#define X(name, func) defineNative(name, func);
    CORE_GLOBAL_LIST(X);
#undef X

#define X(name, func) \
    defineNative(name, func); \
    defineNativeMethod(vm.objectClass, name, func);
    CORE_DUAL_LIST(X);
#undef X

#define X(name, func) defineNativeMethod(vm.objectClass, name, func);
    OBJECT_METHOD_LIST(X);
#undef X
}

#define STRING_METHOD_LIST(X) \
    X("trim", stringTrimNative) \
    X("contains", stringContainsNative) \
    X("find", stringFindNative) \
    X("to_upper", stringToUpperNative) \
    X("to_lower", stringToLowerNative) \
    X("len", stringLenNative) \
    X("length", stringLenNative) \
    X("split", stringSplitNative) \
    X("slice", stringSliceNative) \
    X("to_array", stringToarrayNative) \
    X("to_number", toNumberNative) \
    X("tokens", stringTokensNative) \
    X("format", stringFormatNative)

Value stringTrimNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[-1]);
    char* start = str->chars;
    char* end = str->chars + str->length - 1;

    while (isspace(*start)) start++;

    while (end > start && isspace(*end)) end--;

    int newLength = (int)(end - start + 1);
    if (newLength <= 0) return OBJ_VAL(copyString("", 0));

    return OBJ_VAL(copyString(start, newLength));
}

Value stringContainsNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return BOOL_VAL(false);
    }

    ObjString* haystack = AS_STRING(args[-1]);
    ObjString* needle = AS_STRING(args[0]);

    return BOOL_VAL(strstr(haystack->chars, needle->chars) != NULL);
}

Value stringFindNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return NIL_VAL;
    }

    ObjString* haystack = AS_STRING(args[-1]);
    ObjString* needle = AS_STRING(args[0]);

    char* location = strstr(haystack->chars, needle->chars);
    if (location != NULL) {
        return NUMBER_VAL(location - haystack->chars);
    }
    return NIL_VAL;
}

Value stringToUpperNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[-1]);

    char* buffer = (char*)malloc(str->length + 1);
    if (buffer == NULL) {
        runtimeError("to_upper() failed to allocate memory.");
        return NIL_VAL;
    }

    for (int i = 0; i < str->length; i++) {
        buffer[i] = toupper((unsigned char)str->chars[i]);
    }
    buffer[str->length] = '\0';

    return OBJ_VAL(takeString(buffer, str->length));
}

Value stringToLowerNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[-1]);

    char* buffer = (char*)malloc(str->length + 1);
    if (buffer == NULL) {
        runtimeError("to_lower() failed to allocate memory.");
        return NIL_VAL;
    }

    for (int i = 0; i < str->length; i++) {
        buffer[i] = tolower((unsigned char)str->chars[i]);
    }
    buffer[str->length] = '\0';

    return OBJ_VAL(takeString(buffer, str->length));
}

Value stringLenNative(int argCount, Value* args) {
    ObjString* str = AS_STRING(args[-1]);
    return NUMBER_VAL((double)str->length);
}

Value stringSplitNative(int argCount, Value* args) {
    if (argCount < 1) {
        runtimeError("split() expects 1 argument.");
        return NIL_VAL;
    }

    Value val = args[0];
    ObjString* receiver = AS_STRING(args[-1]);

    if (IS_STRING(val)) {
        ObjString* sep = AS_STRING(args[0]);

        ObjArray* result = newArray();
        push(OBJ_VAL(result));

        if (sep->length == 0) {
            for (int i = 0; i < receiver->length; i++) {
                ObjString* charStr = copyString(receiver->chars + i, 1);
                push(OBJ_VAL(charStr));
                arrayAppend(result, OBJ_VAL(charStr));
                pop();
            }
            return pop();
        }

        char* text = receiver->chars;
        char* found;
        int sepLen = sep->length;

        while ((found = strstr(text, sep->chars)) != NULL) {
            int segmentLen = (int)(found - text);

            ObjString* segment = copyString(text, segmentLen);
            push(OBJ_VAL(segment));
            arrayAppend(result, OBJ_VAL(segment));
            pop();

            text = found + sepLen;
        }

        ObjString* lastSegment = copyString(text, (int)strlen(text));
        push(OBJ_VAL(lastSegment));
        arrayAppend(result, OBJ_VAL(lastSegment));
        pop();

        return pop();
    } else if (IS_NUMBER(val)) {
        int split_size = (int)AS_NUMBER(val);
        if (split_size <= 0) {
            runtimeError("split size must be > 0.");
            return NIL_VAL;
        }

        ObjArray* array = newArray();
        push(OBJ_VAL(array));

        int index = 0;

        while (index < receiver->length) {
            int rem = receiver->length - index;
            int current_size = (rem < split_size) ? rem : split_size;

            ObjString* str = copyString(&receiver->chars[index], current_size);
            push(OBJ_VAL(str));
            arrayAppend(array, OBJ_VAL(str));
            pop();

            index += split_size;
        }
        return pop();
    }
    runtimeError("split() expects a string or positive number arguments.");
    return NIL_VAL;
}

Value stringSliceNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("slice() expects at least a start index.");
        return NIL_VAL;
    }

    ObjString* dom = AS_STRING(args[-1]);
    int length = dom->length;

    int start = (int)AS_NUMBER(args[0]);
    if (start < 0) start += length;
    if (start < 0) start = 0;
    if (start > length) start = length;

    int end = length;
    if (argCount >= 2 && IS_NUMBER(args[1])) {
        end = (int)AS_NUMBER(args[1]);
        if (end < 0) end += length;
        if (end < 0) end = 0;
        if (end > length) end = length;
    }

    if (start >= end) return OBJ_VAL(copyString("", 0));

    return OBJ_VAL(copyString(dom->chars + start, end - start));
}

Value stringToarrayNative(int argCount, Value* args) {
    ObjString* string = AS_STRING(args[-1]);
    ObjArray* array = newArray();
    push(OBJ_VAL(array));

    for (int i = 0; i < string->length; i++) {
        uint8_t byte = (uint8_t)string->chars[i];
        arrayAppend(array, NUMBER_VAL((double)byte));
    }

    return pop();
}

Value stringTokensNative(int argCount, Value* args) {
    if (argCount > 0) {
        runtimeError("tokens() expects 0 arguments.");
        return NIL_VAL;
    }

    ObjString* receiver = AS_STRING(args[-1]);

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    char* chars = receiver->chars;
    int length = receiver->length;
    int i = 0;

    while (i < length) {
        while (i < length && isspace((unsigned char)chars[i])) {
            i++;
        }

        if (i >= length) break;

        int start = i;

        while (i < length && !isspace((unsigned char)chars[i])) {
            i++;
        }
        int tokenLen = i - start;

        ObjString* token = copyString(chars + start, tokenLen);
        push(OBJ_VAL(token));
        arrayAppend(result, OBJ_VAL(token));
        pop();
    }
    return pop();
}

Value stringFormatNative(int argCount, Value* args) {
    ObjString* formatStr = AS_STRING(args[-1]);

    int capacity = formatStr->length + 64;
    char* buffer = malloc(capacity);
    int length = 0;

    int currentArg = 0;

    for (int i = 0; i < formatStr->length; i++) {
        if (length + 32 > capacity) {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
        }

        if (formatStr->chars[i] == '%' && i + 1 < formatStr->length) {
            char specifier = formatStr->chars[i + 1];
            i++;

            if (specifier == '%') {
                buffer[length++] = '%';
                continue;
            }

            if (currentArg >= argCount) {
                length += sprintf(buffer + length, "%%c", specifier);
                continue;
            }

            Value val = args[currentArg++];

            switch (specifier) {
                case 's':
                    {
                        if (IS_STRING(val)) {
                            ObjString* s = AS_STRING(val);
                            while (length + s->length >= capacity) {
                                capacity += s->length + 64;
                                buffer = realloc(buffer, capacity);
                            }
                            memcpy(buffer + length, s->chars, s->length);
                            length += s->length;
                        } else if (IS_NIL(val)) {
                            length += sprintf(buffer + length, "nil");
                        } else {
                            length += sprintf(buffer + length, "<object>");
                        }
                    }
                    break;
                case 'd':
                case 'f':
                    if (IS_NUMBER(val)) {
                        double num = AS_NUMBER(val);
                        length += sprintf(buffer + length, specifier == 'd' ? "%.0f" : "%f", num);
                    } else {
                        length += sprintf(buffer + length, "NaN");
                    }
                    break;
                case 'b':
                    if (IS_BOOL(val)) {
                        length += sprintf(buffer + length, AS_BOOL(val) ? "true" : "false");
                    } else {
                        length += sprintf(buffer + length, "false");
                    }
                    break;
                default:
                    buffer[length++] = '%';
                    buffer[length++] = specifier;
                    break;
            }
        } else {
            buffer[length++] = formatStr->chars[i];
        }
    }

    buffer[length] = '\0';

    ObjString* result = copyString(buffer, length);
    free(buffer);

    return OBJ_VAL(result);
}

void initStringClass() {
    ObjString* empty = copyString("", 0);
    push(OBJ_VAL(empty));
    empty->obj.klass = vm.stringClass;

#define X(name, func) defineNativeMethod(vm.stringClass, name, func);
    STRING_METHOD_LIST(X)
#undef X
    pop();
}

#define MAP_METHOD_LIST(X) \
    X("keys", mapKeysNative) \
    X("values", mapValuesNative) \
    X("has", mapHasNative) \
    X("remove", mapRemoveNative) \
    X("len", mapLenNative)

Value mapNativeConstructor(int argCount, Value* args) {
    if (argCount % 2 != 0) {
        runtimeError("Map constructor requires an even number of key-value arguments.");
        return NIL_VAL;
    }
    
    ObjMap* map = newMap();
    push(OBJ_VAL(map));

    for (int i = 0; i < argCount; i += 2) {
        Value key = args[i];
        Value value = args[i+1];

        if (!IS_STRING(key)) {
            runtimeError("Map keys must be strings.");
            pop();
            return NIL_VAL;
        }
        tableSet(&map->items, AS_STRING(key), value);
    }

    return pop();
}

Value mapKeysNative(int argCount, Value* args) {
    ObjMap* map = AS_MAP(args[-1]);
    ObjArray* valuesArray = newArray();
    push(OBJ_VAL(valuesArray));

    for (int i = 0; i < map->items.capacity; i++) {
        Entry* entry = &map->items.entries[i];
        if (entry->key != NULL) {
            arrayAppend(valuesArray, OBJ_VAL(entry->key));
        }
    }
    return pop();
}

Value mapValuesNative(int argCount, Value* args) {
    ObjMap* map = AS_MAP(args[-1]);
    ObjArray* valuesArray = newArray();
    push(OBJ_VAL(valuesArray));

    for (int i = 0; i < map->items.capacity; i++) {
        Entry* entry = &map->items.entries[i];
        if (entry->key != NULL) {
            arrayAppend(valuesArray, entry->value);
        }
    }
    return pop();
}

Value mapHasNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        return BOOL_VAL(false);
    }

    ObjMap* map = AS_MAP(args[-1]);
    Value dummy;
    return BOOL_VAL(mapGetByValue(map, args[0], &dummy));
}

Value mapRemoveNative(int argCount, Value* args) {
    ObjMap* sourceMap = AS_MAP(args[-1]);
    ObjMap* deletedMap = newMap();
    push(OBJ_VAL(deletedMap));

    for (int i = 0; i < argCount; i++) {
        Value key = args[i];

        if (!IS_STRING(key)) {
            runtimeError("Map keys must be strings.");
            pop();
            return NIL_VAL;
        }

        Value value;
        if (mapGetByValue(sourceMap, key, &value)) {
            mapSetByValue(deletedMap, key, value);
            tableDelete(&sourceMap->items, AS_STRING(key));
        }
    }

    return pop();
}

Value mapLenNative(int argCount, Value* args) {
    return NUMBER_VAL(AS_MAP(args[-1])->items.count);
}

void initMapClass() {
    ObjString* string = NULL;
    string = copyString("Map", 3);
    push(OBJ_VAL(string));

    vm.mapClass = newClass(string);
    vm.mapClass->superclass = vm.objectClass;
    vm.mapClass->callHandler = mapNativeConstructor;
    tableSet(&vm.globals, string, OBJ_VAL(vm.mapClass));
    push(OBJ_VAL(vm.mapClass));

#define X(name, func) defineNativeMethod(vm.mapClass, name, func);
    MAP_METHOD_LIST(X)
#undef X
    pop();
}

#define MATH_DUAL_METHOD_LIST(X) \
    X("sqrt", mathSqrtNative) \
    X("abs", mathAbsNative) \
    X("floor", mathFloorNative) \
    X("ceil", mathCeilNative) \
    X("exp", mathExpNative) \
    X("hex", hexNative) \
    X("oct", octNative) \
    X("bin", binNative) \
    X("sin", mathSinNative) \
    X("tan", mathTanNative) \
    X("atan2", mathAtan2Native) \
    X("cos", mathCosNative) \
    X("acos", mathAcosNative) \
    X("to_int", numberToIntNative) \
    X("to_fixed", numberToFixedNative)

#define MATH_ONLY_METHOD_LIST(X) \
    X("random", mathRandomNative) \
    X("bit_test", bitTestNative) \
    X("min", mathMinNative) \
    X("max", mathMaxNative) \
    X("parse", mathParseNative) \
    X("from_hex", fromHexNative) \
    X("from_bin", fromBinNative) \
    X("round", mathRoundNative) \
    X("to_number", toNumberNative)

#define EXTRACT_MATH_OP(outVar, funcName) \
    double outVar; \
    if (IS_NUMBER(args[-1])) { \
        outVar = AS_NUMBER(args[-1]); \
    } else if (argCount > 0 && IS_NUMBER(args[0])) { \
        outVar = AS_NUMBER(args[0]); \
    } else { \
        runtimeError(funcName "() expects a number reciver or a number argument."); \
        return NIL_VAL; \
    }

Value mathSqrtNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "sqrt");
    return NUMBER_VAL(sqrt(val));
}

Value mathAbsNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "abs");
    return NUMBER_VAL(fabs(val));
}

Value mathFloorNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "floor");
    return NUMBER_VAL(floor(val));
}

Value mathCeilNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "ceil");
    return NUMBER_VAL(ceil(val));
}

Value mathRandomNative(int argCount, Value* args) {
    // return NUMBER_VAL((double)rand() / (double)RAND_MAX);
    // unsigned long large_rand = ((unsigned long)rand() << 15) | rand();
    double r = (double)rand();
    double m = (double)RAND_MAX;
    // return NUMBER_VAL((double)large_rand / (double)0x3fffffff);
    return NUMBER_VAL(r / (m + 1.0));
}

Value mathExpNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "exp");
    return NUMBER_VAL(exp(val));
}

Value hexNative(int argCount, Value* args) {
    uint64_t num;
    int precision = 1;
    bool prefix = true;

    if (IS_NUMBER(args[-1])) {
        num = (uint64_t)AS_NUMBER(args[-1]);
        if (argCount >= 1 && IS_NUMBER(args[0])) precision = (int)AS_NUMBER(args[0]);
        if (argCount >= 2 && IS_BOOL(args[1])) prefix = AS_BOOL(args[1]);
    } else {
        if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;
        num = (uint64_t)AS_NUMBER(args[0]);
        if (argCount >= 2 && IS_NUMBER(args[1])) precision = (int)AS_NUMBER(args[1]);
        if (argCount >= 3 && IS_BOOL(args[2])) prefix = AS_BOOL(args[2]);
    }

    char buffer[64];
    if (prefix)
        snprintf(buffer, sizeof(buffer), "0x%.*llx", precision, (uint64_t)num);
    else
        snprintf(buffer, sizeof(buffer), "%.*llx", precision, (uint64_t)num);

    return OBJ_VAL(copyString(buffer, strlen(buffer)));
}

Value octNative(int argCount, Value* args) {
    uint64_t num;
    int precision = 1;

    if (IS_NUMBER(args[-1])) {
        num = (uint64_t)AS_NUMBER(args[-1]);
        if (argCount >= 1 && IS_NUMBER(args[0])) precision = (int)AS_NUMBER(args[0]);
    } else {
        if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;
        num = (uint64_t)AS_NUMBER(args[0]);
        if (argCount >= 1 && IS_NUMBER(args[1])) precision = (int)AS_NUMBER(args[1]);
    }

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "0%.*llo", precision, (unsigned long long)num);

    return OBJ_VAL(copyString(buffer, strlen(buffer)));
}

Value binNative(int argCount, Value* args) {
    uint64_t num;
    int min_bits = 1;

    if (IS_NUMBER(args[-1])) {
        num = (uint64_t)AS_NUMBER(args[-1]);
        if (argCount >= 1 && IS_NUMBER(args[0])) min_bits = (int)AS_NUMBER(args[0]);
    } else {
        if (argCount < 1 || !IS_NUMBER(args[0])) return NIL_VAL;
        num = (uint64_t)AS_NUMBER(args[0]);
        if (argCount >= 1 && IS_NUMBER(args[1])) min_bits = (int)AS_NUMBER(args[1]);
    }

    if (min_bits > 64) min_bits = 64;
    if (min_bits < 1) min_bits = 1;

    char buffer[70];
    char* p = buffer;
    *p++ = '0';
    *p++ = 'b';

    int highest_bit = 0;
    for (int i = 63; i >= 0; i--) {
        if ((num >> i) & 1) {
            highest_bit = i;
            break;
        }
    }
    int start_bit = (highest_bit >= min_bits) ? highest_bit : min_bits - 1;

    for (int i = start_bit; i >= 0; i--) {
        *p++ = ((num >> i) & 1) ? '1' : '0';
    }
    *p = '\0';

    return OBJ_VAL(copyString(buffer, (int)(p - buffer)));
}

Value bitTestNative(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) return NIL_VAL;

    uint64_t num = (uint64_t)AS_NUMBER(args[1]);
    int bit = (int)AS_NUMBER(args[2]);

    if (bit < 0 || bit > 63) return BOOL_VAL(false);

    return BOOL_VAL((num >> bit) & 1);
}

Value mathMinNative(int argCount, Value* args) {
    if (argCount != 3) return NIL_VAL;
    return NUMBER_VAL(fmin(AS_NUMBER(args[1]), AS_NUMBER(args[2])));
}

Value mathMaxNative(int argCount, Value* args) {
    if (argCount != 3) return NIL_VAL;
    return NUMBER_VAL(fmax(AS_NUMBER(args[1]), AS_NUMBER(args[2])));
}

Value mathParseNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[1])) return NIL_VAL;

    const char* str = AS_CSTRING(args[1]);
    char* endptr;

    unsigned long long result = strtoull(str, &endptr, 0);

    if (str == endptr) return NIL_VAL;

    return NUMBER_VAL((double)result);
}

Value fromHexNative(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) return NIL_VAL;

    const char* str = AS_CSTRING(args[0]);

    uint32_t result = (uint32_t)strtoul(str, NULL, 0);
    return NUMBER_VAL((double)result);
}

Value fromBinNative(int argCount, Value* args) {
    return mathParseNative(argCount, args);
}

Value mathRoundNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "round");
    return NUMBER_VAL(round(val));
}

// also callhandler/constructor
Value toNumberNative(int argCount, Value* args) {
    Value value = NUMBER_VAL(0);

    if (IS_STRING(args[-1]) || IS_NUMBER(args[-1]) || IS_BOOL(args[-1]) || IS_NIL(args[-1])) {
        value = args[-1];
    } else {
        if (argCount < 1) return NUMBER_VAL(0);
        value = args[0];
    }

    if (IS_NUMBER(value)) return value;
    if (!IS_STRING(value)) return NUMBER_VAL(0);

    char* end;
    const char* str = AS_CSTRING(value);
    double number = strtod(str, &end);

    if (str == end) {
        return NUMBER_VAL(0);
    }

    return NUMBER_VAL(number);
}

Value mathSinNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "sin");
    return NUMBER_VAL(sin(val));
}

Value mathTanNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "tan");
    return NUMBER_VAL(tan(val));
}

Value mathAtan2Native(int argCount, Value* args) {
    double y, x;

    if (IS_NUMBER(args[-1])) {
        if (argCount < 1 || !IS_NUMBER(args[0])) {
            runtimeError("atan2() expects a number argument when called as a method.");
            return NIL_VAL;
        }
        y = AS_NUMBER(args[-1]);
        x = AS_NUMBER(args[0]);
    } else {
        if (argCount < 2 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
            runtimeError("atan2() expects two number arguments when called statically.");
            return NIL_VAL;
        }
        y = AS_NUMBER(args[0]);
        x = AS_NUMBER(args[1]);
    }
    return NUMBER_VAL(atan2(y, x));
}

Value mathCosNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "cos");
    return NUMBER_VAL(cos(val));
}

Value mathAcosNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "acos");
    return NUMBER_VAL(acos(val));
}

Value numberToIntNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "to_int");

    if (isnan(val)) {
        return OBJ_VAL(copyString("NaN", 3));
    }
    if (isinf(val)) {
        return OBJ_VAL(copyString(val > 0 ? "Infinity" : "-Infinity", val > 0 ? 8 : 9));
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%0.f", trunc(val));
    return OBJ_VAL(copyString(buffer, (int)strlen(buffer)));
}

Value numberToFixedNative(int argCount, Value* args) {
    EXTRACT_MATH_OP(val, "to_fixed");
    int decimals = 0;

    if (argCount >= 2) {
        if (!IS_NUMBER(args[1])) {
            runtimeError("Number.to_fixed() decimals argument must be a number.");
            return NIL_VAL;
        }
        decimals = (int)AS_NUMBER(args[1]);
        if (decimals < 0) decimals = 0;
        if (decimals > 20) decimals = 20;
    }

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%.*f", decimals, val);

    return OBJ_VAL(copyString(buffer, (int)strlen(buffer)));
}

void initMathLibrary() {
    ObjString* mathName = copyString("Math", 4);
    push(OBJ_VAL(mathName));
    ObjClass* mathClass = newClass(mathName);
    push(OBJ_VAL(mathClass));
    tableSet(&vm.globals, mathName, OBJ_VAL(mathClass));

#define X(name, func) \
    defineNativeMethod(mathClass, name, func); \
    defineNativeMethod(vm.numberClass, name, func);
    MATH_DUAL_METHOD_LIST(X)
#undef X

#define X(name, func) defineNativeMethod(mathClass, name, func);
    MATH_ONLY_METHOD_LIST(X)
#undef X

    defineNativeMethod(vm.numberClass, "to_int", numberToIntNative);
    defineNativeMethod(vm.numberClass, "to_fixed", numberToFixedNative);

    // or if want single source:
#define X(name, func, isDual) \
    defineNativeMethod(mathClass, name, func); \
    if (isDual) { defineNativeMethod(vm.numberClass, name, func); }
    // .. list like X("sqrt", mathSqrtNative, true) \
    // ..           X("random", mathRandomNative, false)
    // then: MATH_SYSTEM_METHODS(X)
#undef X

    defineNativeClassConstant(mathClass, "PI", NUMBER_VAL(3.1415926535897932));
    defineNativeClassConstant(mathClass, "E",  NUMBER_VAL(2.7182818284590452));

    pop();
    pop();
    srand((unsigned int)time(NULL));
}

#define ARRAY_METHOD_LIST(X) \
    X("push", arrayPushNative) \
    X("pop", arrayPopNative) \
    X("len", arrayLenNative) \
    X("length", arrayLenNative) \
    X("map", arrayMapNative) \
    X("dup", arrayDupNative) \
    X("is_empty", arrayIsEmptyNative) \
    X("filter", arrayFilterNative) \
    X("reduce", arrayReduceNative) \
    X("join", arrayJoinNative) \
    X("each", arrayEachNative) \
    X("find", arrayFindNative) \
    X("has", arrayHasNative) \
    X("slice", arraySliceNative) \
    X("sort", arraySortNative) \
    X("sort_slice", arraySortSliceNative) \
    X("reverse", arrayReverseNative) \
    X("flatten", arrayFlattenNative) \
    X("to_string", arrayStringNative) \
    X("first", arrayFirstNative) \
    X("rest", arrayRestNative) \
    X("split", arraySplitNative)

Value arrayPushNative(int argCount, Value* args) {
    if (argCount < 1) return NIL_VAL;

    ObjArray* array = AS_ARRAY(args[-1]);
    for (int i = 0; i < argCount; i++) {
        arrayAppend(array, args[i]);
    }
    return OBJ_VAL(array);
}

Value arrayPopNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);

    if (array->count == 0) {
        return NIL_VAL;
    }

    Value lastValue = array->values[array->count - 1];
    array->count--;
    array->values[array->count] = NIL_VAL;
    return lastValue;
}

Value arrayLenNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    return NUMBER_VAL(array->count);
}

Value arrayMapNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) {
        runtimeError("Expected a closure callback argument for map().");
        return NIL_VAL;
    }

    ObjArray* original = AS_ARRAY(args[-1]);
    Value callback = args[0];

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    VM_CALLBACK_INIT();

    for (int i = 0; i < original->count; i++) {
        push(callback);
        push(original->values[i]);

        VM_CALLBACK_ENTER();

        if (callValue(callback, 1)) {
            InterpretResult res = run();

            VM_CALLBACK_CHECK_ERROR(res);

            Value testResult = peek(0);
            arrayAppend(result, testResult);
        }

        VM_CALLBACK_RESET_STACK();
    }

    VM_CALLBACK_EXIT();

    return pop();
}

Value arrayDupNative(int argCount, Value* args) {
    if (!IS_ARRAY(args[-1])) return NIL_VAL;

    ObjArray* original = AS_ARRAY(args[-1]);
    ObjArray* copy = duplicateArray(original);

    return OBJ_VAL(copy);
}

Value arrayIsEmptyNative(int argCount, Value* args) {
    return BOOL_VAL(AS_ARRAY(args[-1])->count == 0);
}

Value arrayFilterNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) {
        runtimeError("Expected a callback function argument for filter().");
        return NIL_VAL;
    }

    ObjArray* original = AS_ARRAY(args[-1]);
    Value callback = args[0];

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    VM_CALLBACK_INIT();

    for (int i = 0; i < original->count; i++) {
        push(callback);
        push(original->values[i]);

        VM_CALLBACK_ENTER();

        if (callValue(callback, 1)) {
            InterpretResult res = run();

            VM_CALLBACK_CHECK_ERROR(res);

            if (isTruthy(pop())) {
                arrayAppend(result, original->values[i]);
            }
        }

        VM_CALLBACK_RESET_STACK();
    }

    VM_CALLBACK_EXIT();

    return pop();
}

Value arrayReduceNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    Value callback = NIL_VAL;
    Value acc = NIL_VAL;
    int startindex = 0;

    if (argCount == 1) {
        // case A: [1,2,3].reduce(callback)
        if (!IS_CLOSURE(args[0])) {
            runtimeError("reduce() with 1 argument expects a callback function.");
            return NIL_VAL;
        }

        if (array->count == 0) {
            return NUMBER_VAL(0);
            //runtimeError("Cannot reuduce an empty array with no initial value.");
            //return NIL_VAL;
        }
        callback = args[0];
        acc = array->values[0];
        startindex = 1;
    } else if (argCount >= 2) {
        // case B: [1,2,3].reduce(initialValue, callback)
        if (!IS_CLOSURE(args[1])) {
            runtimeError("reduce() with 2 arguments expecats a callback function as the second argument.");
            return NIL_VAL;
        }
        acc = args[0];
        callback = args[1];
        startindex = 0;
    } else {
        runtimeError("reduce() expects at least 1 argument (callback).");
        return NIL_VAL;
    }

    push(acc);

    VM_CALLBACK_INIT();

    for (int i = startindex; i < array->count; i++) {
        push(callback);

        push(_callbackStackStart[-1]);
        push(array->values[i]);

        VM_CALLBACK_ENTER();

        if (callValue(callback, 2)) {
            InterpretResult res = run();

            VM_CALLBACK_CHECK_ERROR(res);

            _callbackStackStart[-1] = peek(0);
        }

        VM_CALLBACK_RESET_STACK();
    }

    VM_CALLBACK_EXIT();

    return pop();
}

Value arrayJoinNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("join() expects 1 string argument (separator).");
        return NIL_VAL;
    }

    ObjArray* array = AS_ARRAY(args[-1]);
    ObjString* sep = AS_STRING(args[0]);

    if (array->count == 0) return OBJ_VAL(copyString("", 0));

    int capacity = 32;
    int length = 0;
    char* buffer = ALLOCATE(char, capacity);

    for (int i = 0; i < array->count; i++) {
        Value item = array->values[i];

        ObjString* s = AS_STRING(valueToString(item));
        push(OBJ_VAL(s));

        int sepLen = (i < array->count - 1) ? sep->length : 0;
        int neededCapacity = length + s->length + sepLen + 1;

        if (neededCapacity > capacity) {
            int oldCapacity = capacity;
            capacity = neededCapacity * 2;
            buffer = GROW_ARRAY(char, buffer, oldCapacity, capacity);
        }

        memcpy(buffer + length, s->chars, s->length);
        length += s->length;

        pop();

        if (sepLen > 0) {
            memcpy(buffer + length, sep->chars, sepLen);
            length += sepLen;
        }
    }

    buffer = GROW_ARRAY(char, buffer, capacity, length + 1);
    buffer[length] = '\0';

    ObjString* result = takeString(buffer, length);

    return OBJ_VAL(result);
}

Value arrayEachNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) return NIL_VAL;

    ObjArray* array = AS_ARRAY(args[-1]);
    push(OBJ_VAL(array));
    ObjClosure* callback = AS_CLOSURE(args[0]);

    VM_CALLBACK_INIT();

    for (int i = 0; i < array->count; i++) {
        push(args[0]);
        push(array->values[i]);

        VM_CALLBACK_ENTER();

        if (vmCall(callback, 1)) {
            InterpretResult state = run();

            VM_CALLBACK_CHECK_ERROR(state);
        }

        VM_CALLBACK_RESET_STACK();
    }

    VM_CALLBACK_EXIT();
    return pop();
}

Value arrayFindNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) {
        runtimeError("Expected a closure callback argument for find().");
        return NIL_VAL;
    }

    ObjArray* array = AS_ARRAY(args[-1]);
    ObjClosure* callback = AS_CLOSURE(args[0]);

    VM_CALLBACK_INIT();

    for (int i = 0; i < array->count; i++) {
        push(OBJ_VAL(callback));
        push(array->values[i]);

        VM_CALLBACK_ENTER();

        if (vmCall(callback, 1)) {
            InterpretResult state = run();

            VM_CALLBACK_CHECK_ERROR(state);

            Value result = pop();

            if (!isFalsey(result)) {
                VM_CALLBACK_RESET_STACK();
                VM_CALLBACK_EXIT();
                return array->values[i];
            }
        }
        VM_CALLBACK_RESET_STACK();
    }

    VM_CALLBACK_EXIT();
    return NIL_VAL;
}

Value arrayHasNative(int argCount, Value* args) {
    if (argCount < 1) {
        runtimeError("Expected 1 argument for has().");
        return NIL_VAL;
    }

    ObjArray* array = AS_ARRAY(args[-1]);
    Value target = args[0];

    for (int i = 0; i < array->count; i++) {
        if (valuesEqual(array->values[i], target)) {
            return BOOL_VAL(true);
        }
    }
    return BOOL_VAL(false);
}

Value arraySliceNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    int count = array->count;

    int start = (argCount >= 2 && IS_NUMBER(args[0])) ? (int)AS_NUMBER(args[0]) : 0;
    if (start < 0) start = count + start;
    if (start < 0) start = 0;
    if (start > count) start = count;

    int end = (argCount >= 3 && IS_NUMBER(args[1])) ? (int)AS_NUMBER(args[1]) : count;
    if (end < 0) end = count + end;
    if (end < 0) end = 0;
    if (end > count) end = count;

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    if (end > start) {
        for (int i = start; i < end; i++) {
            arrayAppend(result, array->values[i]);
        }
    }

    return pop();
}

static int defaultSortComparator(const void* a, const void* b) {
    Value valA = *(Value*)a;
    Value valB = *(Value*)b;

    if (IS_NUMBER(valA) && IS_NUMBER(valB)) {
        double diff = AS_NUMBER(valA) - AS_NUMBER(valB);
        return (diff > 0) - (diff < 0);
    }

    if (IS_STRING(valA) && IS_STRING(valB)) {
        return strcmp(AS_CSTRING(valA), AS_CSTRING(valB));
    }

    if (IS_BOOL(valA) && IS_BOOL(valB)) {
        return (int)AS_BOOL(valA) - (int)AS_BOOL(valB);
    }

    // probably not useful
    return (int)valA.type - (int)valB.type;

}
static int loxSortComparator(const void* a, const void* b, void* userdata) {
    ObjClosure* callback = (ObjClosure*)userdata;
    Value valA = *(Value*)a;
    Value valB = *(Value*)b;

    Value* comparisonStackBase = vm.stackTop;

    push(OBJ_VAL(callback));
    push(valA);
    push(valB);

    Value* stackStart = vm.stackTop;
    int oldExitDepth = vm.nativeExitDepth;
    vm.nativeExitDepth = vm.frameCount;

    if (vmCall(callback, 2)) {
        InterpretResult state = run();
        if (state == INTERPRET_RUNTIME_ERROR) {
            vm.stackTop = comparisonStackBase;
            vm.nativeExitDepth = oldExitDepth;
            return 0;
        }

        Value result = pop();

        vm.stackTop = comparisonStackBase;

        if (IS_NUMBER(result)) return (int)AS_NUMBER(result);
        if (IS_BOOL(result)) {
            if (AS_BOOL(result) == false)
                return 1;
            else
                return -1;
        }
    }
    vm.stackTop = comparisonStackBase;
    return 0;
}

Value arraySortNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    if (array->count < 2) return args[-1];

    if (argCount >= 2 && IS_CLOSURE(args[0])) {
        qsort_r(array->values, array->count, sizeof(Value),
                loxSortComparator, AS_CLOSURE(args[0]));
    } else {
        // default sort
        qsort(array->values, array->count, sizeof(Value),
                defaultSortComparator);
    }
    return args[-1];
}

Value arraySortSliceNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    if (array->count < 2) return args[-1];
    
    int start = AS_NUMBER(args[0]);
    int end = AS_NUMBER(args[1]);
    if (start < 0 || end > array->count || start > end) {
        return args[-1];
    }

    Value* sliceStart = &array->values[start];
    int count = end - start;
    if (argCount >= 3 && IS_CLOSURE(args[2])) {
        qsort_r(sliceStart, count, sizeof(Value),
                loxSortComparator, AS_CLOSURE(args[2]));
    } else {
        qsort(sliceStart, count, sizeof(Value),
                defaultSortComparator);
    }
    return args[-1];
}

Value arrayReverseNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    if (array->count < 2) return args[-1];

    int left = 0;
    int right = array->count - 1;

    while (left < right) {
        Value temp = array->values[left];
        array->values[left] = array->values[right];
        array->values[right] = temp;
        left++;
        right--;
    }
    return args[-1];
}

static void flattenDeepHelper(ObjArray* result, ObjArray* current) {
    for (int i = 0; i < current->count; i++) {
        Value item = current->values[i];

        if (IS_ARRAY(item)) {
            flattenDeepHelper(result, AS_ARRAY(item));
        } else {
            arrayAppend(result, item);
        }
    }
}

Value arrayFlattenNative(int argCount, Value* args) {
    ObjArray* source = AS_ARRAY(args[-1]);
    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    flattenDeepHelper(result, source);
    return pop();
}

Value arrayStringNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    int count = array->count;

    if (count == 0) {
        return OBJ_VAL(copyString("", 0));
    }

    uint8_t* buffer = ALLOCATE(uint8_t, count);

    for (int i = 0; i < count; i++) {
        Value v = array->values[i];
        if (!IS_NUMBER(v)) {
            FREE_ARRAY(uint8_t, buffer, count);
            runtimeError("Array contains non-number at index %d.", i);
            return NIL_VAL;
        }
        double num = AS_NUMBER(v);
        if (num < 0 || num > 255) {
            FREE_ARRAY(uint8_t, buffer, count);
            runtimeError("Byte value %g out of range (0-255).", num);
            return NIL_VAL;
        }
        buffer[i] = (uint8_t)num;
    }
    ObjString* string = copyString((char*)buffer, count);
    FREE_ARRAY(uint8_t, buffer, count);

    return OBJ_VAL(string);
}

Value arrayFirstNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);

    if (array->count == 0)
        return NIL_VAL;

    return array->values[0];
}

Value arrayRestNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);
    ObjArray* rest = newArray();

    push(OBJ_VAL(rest));

    for (int i = 1; i < array->count; i++) {
        arrayAppend(rest, array->values[i]);
    }

    pop();
    return OBJ_VAL(rest);
}

Value arraySplitNative(int argCount, Value* args) {
    ObjArray* array = AS_ARRAY(args[-1]);

    Value head = (array->count > 0) ? array->values[0] : NIL_VAL;

    ObjArray* rest = newArray();
    push(OBJ_VAL(rest));

    for (int i = 1; i < array->count; i++) {
        arrayAppend(rest, array->values[i]);
    }

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    arrayAppend(result, head);
    arrayAppend(result, OBJ_VAL(rest));

    pop();
    pop();

    return OBJ_VAL(result);
}

void debugPrintArrayMethods() {
#define X(name, func) printf(" -- Array.%s()\n", name);

    printf("Registered Array Methods:\n");
    ARRAY_METHOD_LIST(X)
#undef X
}

Value arrayNativeConstructor(int argCount, Value* args) {
    ObjArray* array = newArray();

    if (argCount > 0) {
        array->values = ALLOCATE(Value, argCount);
        array->capacity = argCount;
        array->count = argCount;

        for (int i = 0; i < argCount; i++) {
            array->values[i] = args[i];
        }
    }

    return OBJ_VAL(array);
}

void initArrayClass() {
    ObjString* string = copyString("Array", 5);
    push(OBJ_VAL(string));
    vm.arrayClass = newClass(string);
    vm.arrayClass->superclass = vm.objectClass;
    vm.arrayClass->callHandler = arrayNativeConstructor;
    tableSet(&vm.globals, string, OBJ_VAL(vm.arrayClass));
    push(OBJ_VAL(vm.arrayClass));

#define X(name, func) defineNativeMethod(vm.arrayClass, name, func);
    ARRAY_METHOD_LIST(X)
#undef X

    pop();
    pop();
#undef METHOD
}

Value resultNativeConstructor(int argCount, Value* args) {
    if (argCount < 1 || argCount > 2) {
        runtimeError("Result() rexpects 1 or 2 arguments, got %d.", argCount);
        return NIL_VAL;
    }

    ObjInstance* instance = newInstance(vm.resultClass);
    push(OBJ_VAL(instance));

    Value isOk = args[0];
    Value payload = (argCount == 2) ? args[1] : NIL_VAL;

    tableSet(&instance->fields, vm.okString, isOk);
    if (AS_BOOL(isOk)) {
        tableSet(&instance->fields, vm.valString, payload);
    } else {
        tableSet(&instance->fields, vm.errString, payload);
    }

    pop();
    return OBJ_VAL(instance);
}

Value resultUnwrapNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    if (instance->obj.klass != vm.resultClass) {
        runtimeError("Method unwrap() expected a Result instance.");
        return NIL_VAL;
    }

    Value okVal = NIL_VAL;
    tableGet(&instance->fields, vm.okString, &okVal);

    if (isFalsey(okVal)) {
        Value errVal = NIL_VAL;
        tableGet(&instance->fields, vm.errString, &errVal);

        if (IS_STRING(errVal)) {
            runtimeError("Panic: Tried to unwrap an error Result: %s", AS_CSTRING(errVal));
        } else {
            runtimeError("Panic: Tried to unwrap an error Result.");
        }
        return NIL_VAL;
    }

    Value successVal = NIL_VAL;
    tableGet(&instance->fields, vm.valString, &successVal);

    return successVal;
}

Value resultUnwrapOrNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    if (instance->obj.klass != vm.resultClass) {
        runtimeError("Method unwrap_or() expected a Result instance.");
        return NIL_VAL;
    }

    Value okVal = NIL_VAL;
    tableGet(&instance->fields, vm.okString, &okVal);

    if (isTruthy(okVal)) {
        Value successVal = NIL_VAL;
        tableGet(&instance->fields, vm.valString, &successVal);
        return successVal;
    }

    return args[0];
}

Value optionNativeConstructor(int argCount, Value* args) {
    if (argCount < 1 || argCount > 2) {
        runtimeError("Option() expects 1 or 2 arguments, got %d", argCount);
        return NIL_VAL;
    }

    ObjInstance* instance = newInstance(vm.optionClass);
    push(OBJ_VAL(instance));

    Value isSome = args[0];
    Value payload = (argCount == 2) ? args[1] : NIL_VAL;

    tableSet(&instance->fields, vm.isSomeString, isSome);
    tableSet(&instance->fields, vm.valString, payload);

    pop();
    return OBJ_VAL(instance);
}

Value optionUnwrapNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);

    Value is_some = NIL_VAL;
    tableGet(&instance->fields, vm.isSomeString, &is_some);

    if (!AS_BOOL(is_some)) {
        runtimeError("Attempted to unwrap a 'None' Option.");
        return NIL_VAL;
    }

    Value val = NIL_VAL;
    tableGet(&instance->fields, vm.valString, &val);
    return val;
}

Value optionUnwrapOrNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);

    Value is_some = NIL_VAL;
    tableGet(&instance->fields, vm.isSomeString, &is_some);

    if (isTruthy(is_some)) {
        Value val = NIL_VAL;
        tableGet(&instance->fields, vm.valString, &val);
        return val;
    }
    return args[0];
}

void initResultAndOptionClass() {
    vm.okString = copyString("ok", 2);
    vm.valString = copyString("val", 3);
    vm.errString = copyString("err", 3);
    vm.isSomeString = copyString("is_some", 7);

    ObjString* resultName = copyString("Result", 6);
    push(OBJ_VAL(resultName));
    vm.resultClass = newClass(resultName);
    vm.resultClass->superclass = vm.objectClass;
    vm.resultClass->callHandler = resultNativeConstructor;
    push(OBJ_VAL(vm.resultClass));

    tableSet(&vm.globals, resultName, OBJ_VAL(vm.resultClass));
    defineNativeMethod(vm.resultClass, "unwrap", resultUnwrapNative);
    defineNativeMethod(vm.resultClass, "unwrap_or", resultUnwrapOrNative);

    ObjString* optionName = copyString("Option", 6);
    push(OBJ_VAL(optionName));
    vm.optionClass = newClass(optionName);
    vm.optionClass->superclass = vm.objectClass;
    vm.optionClass->callHandler = optionNativeConstructor;
    push(OBJ_VAL(vm.optionClass));

    tableSet(&vm.globals, optionName, OBJ_VAL(vm.optionClass));
    defineNativeMethod(vm.optionClass, "unwrap", optionUnwrapNative);
    defineNativeMethod(vm.optionClass, "unwrap_or", optionUnwrapOrNative);

    popn(4);
}

Value regexNativeConstructor(int argCount, Value* args) {
    if (argCount != 1 || !IS_STRING(args[0])) {
        runtimeError("Regex constructor expects a pattern string.");
        return NIL_VAL;
    }

    if (!IS_CLASS(args[-1])) {
        runtimeError("Constructor called on a non-class object.");
        return NIL_VAL;
    }
    ObjClass* klass = AS_CLASS(args[-1]);
    ObjString* pattern = AS_STRING(args[0]);

    int errornumber;
    PCRE2_SIZE erroroffset;

    pcre2_code* code = pcre2_compile(
            (unsigned char*)pattern->chars, PCRE2_ZERO_TERMINATED,
            0, &errornumber, &erroroffset, NULL);

    if (code == NULL) {
        runtimeError("Regex compilation failed.");
        return NIL_VAL;
    }

    RegexInternal* internal = ALLOCATE(RegexInternal, 1);
    internal->code = code;
    internal->pattern = pattern;

    ObjInstance* instance = newInstance(klass);
    instance->foreignPtr = internal;

    return OBJ_VAL(instance);
}

Value regexTestNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[1])) {
        runtimeError("test() expects 1 string argument.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    RegexInternal* re = (RegexInternal*)instance->foreignPtr;

    if (re == NULL) {
        runtimeError("Regex not initialized.");
        return NIL_VAL;
    }

    ObjString* subject = AS_STRING(args[1]);

    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re->code, NULL);
    int rc = pcre2_match(re->code, (unsigned char*)subject->chars, subject->length,
            0, 0, match_data, NULL);

    pcre2_match_data_free(match_data);
    return BOOL_VAL(rc >= 0);
}

Value regexMatchNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("match() expects 1 string argument.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    RegexInternal* re = (RegexInternal*)instance->foreignPtr;

    if (re == NULL) {
        runtimeError("Regex not initialized.");
        return NIL_VAL;
    }

    ObjString* subject = AS_STRING(args[0]);

    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(re->code, NULL);
    int rc = pcre2_match(re->code, (unsigned char*)subject->chars, subject->length,
            0, 0, match_data, NULL);

    if (rc < 0) {
        pcre2_match_data_free(match_data);
        return NIL_VAL;
    }

    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);

    ObjArray* results = newArray();
    push(OBJ_VAL(results));

    for (int i = 0; i < rc; i++) {
        int start = (int)ovector[2 * i];
        int end = (int)ovector[2 * i + 1];

        if (start == -1) {
            arrayAppend(results, NIL_VAL);
        } else {
            ObjString* matchStr = copyString(subject->chars + start, end - start);
            push(OBJ_VAL(matchStr));
            arrayAppend(results, OBJ_VAL(matchStr));
            pop();
        }
    }

    pcre2_match_data_free(match_data);
    return pop();
}

Value regexGetPatternNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[0]);

    if (instance->foreignPtr == NULL) {
        runtimeError("Regex instance not initialized.");
        return NIL_VAL;
    }

    RegexInternal* internal = (RegexInternal*)instance->foreignPtr;
    return OBJ_VAL(internal->pattern);
}

void regexDestructor(ObjInstance* inst) {
    if (inst->foreignPtr != NULL) {
        RegexInternal* re = (RegexInternal*)inst->foreignPtr;
        pcre2_code_free(re->code);
        FREE(RegexInternal, inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
}

void initRegexClass() {
    ObjString* string = copyString("Regex", 5);
    push(OBJ_VAL(string));
    vm.regexClass = newClass(string);
    vm.regexClass->superclass = vm.objectClass;
    vm.regexClass->callHandler = regexNativeConstructor;
    vm.regexClass->destructor = regexDestructor;
    tableSet(&vm.globals, string, OBJ_VAL(vm.regexClass));

    defineNativeMethod(vm.regexClass, "test", regexTestNative);
    defineNativeMethod(vm.regexClass, "match", regexMatchNative);
    defineNativeMethod(vm.regexClass, "get_pattern", regexGetPatternNative);

    pop();
}

void closeFileInternal(ObjInstance* inst) {
    if (inst->foreignPtr !=  NULL) {
        FILE* handle = (FILE*)inst->foreignPtr;

        if (handle != stdout && handle != stderr && handle != stdin) {
            fclose(handle);
        }
        inst->foreignPtr = NULL;
    }
}

void fileDestructor(ObjInstance* inst) {
    closeFileInternal(inst);
}

Value fileLoadNative(int argCount, Value* args) {
    if (argCount < 1) {
        runtimeError("File.load() expects a string path.");
        return NIL_VAL;
    }

    Value pathValue = NIL_VAL;
    if (IS_STRING(args[0])) {
        pathValue = args[0];
    } else if (argCount >= 2 && IS_STRING(args[1])) {
        pathValue = args[1];
    } else {
        runtimeError("File.load() expects a string path.");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(pathValue);
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        setLastError(errno, "%s", "Failed to open file.");
        return NIL_VAL;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        setLastError(errno, "%s", "Failed to seek file stream.");
        return NIL_VAL;
    }

    long signedSize = ftell(file);
    if (signedSize < 0) {
        fclose(file);
        setLastError(errno, "%s", "Invalid file stream length or directory handle.");
        return NIL_VAL;
    }

    if (signedSize > INT_MAX) {
        fclose(file);
        runtimeError("File is too large to load into a string.");
        return NIL_VAL;
    }

    size_t fileSize = (size_t)signedSize;
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        runtimeError("Not enough memory to read file.");
        return NIL_VAL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);

    Value result = OBJ_VAL(copyString(buffer, (int)bytesRead));
    free(buffer);

    return result;
}

Value fileSaveNative(int argCount, Value* args) {
    if (argCount <= 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError("File.read() expects (path, content).");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);
    const char* content = AS_CSTRING(args[1]);

    FILE* file = fopen(path, "w");
    if (file == NULL) return errorResult("%s", "Unable to open file.");

    fprintf(file, "%s", content);
    fclose(file);
    return okResult(BOOL_VAL(true));
}

Value fileExistsNative(int argCount, Value* args) {
    if (argCount < 1) return BOOL_VAL(false);

    Value pathValue = NIL_VAL;
    if (IS_STRING(args[0])) {
        pathValue = args[0];
    } else if (argCount >= 2 && IS_STRING(args[1])) {
        pathValue = args[1];
    } else {
        return BOOL_VAL(false);
    }

    const char* path = AS_CSTRING(pathValue);
    if (access(path, F_OK) == 0) {
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

Value fileListNative(int argCount, Value* args) {
    if (argCount < 1) {
        runtimeError("File.list() expects a directory path string.");
        return NIL_VAL;
    }

    Value pathValue = NIL_VAL;
    if (IS_STRING(args[0])) {
        pathValue = args[0];
    } else if (argCount >= 2 && IS_STRING(args[1])) {
        pathValue = args[1];
    } else {
        runtimeError("File.list() expects a directory path string.");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(pathValue);
    DIR* dir = opendir(path);

    if (dir == NULL) {
        return errorResult("%s", "Unable to open directory.");
    }

    ObjArray *fileList = newArray();
    push(OBJ_VAL(fileList));

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        ObjString* name = copyString(entry->d_name, (int)strlen(entry->d_name));
        push(OBJ_VAL(name));
        arrayAppend(fileList, OBJ_VAL(name));
        pop();
    }

    Value result = okResult(OBJ_VAL(fileList));

    pop();

    return result;
}

Value fileOpenNative(int argCount, Value* args) {
    if (!IS_CLASS(args[-1])) {
        runtimeError("File.open() must be called as a class method.");
        return NIL_VAL;
    }
    ObjClass* fileClass = AS_CLASS(args[-1]);

    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("File.open() expects at least a path string.");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);
    const char* mode = "r";

    if (argCount >= 2 && IS_STRING(args[1])) {
        mode = AS_CSTRING(args[1]);
    }

    FILE* handle = NULL;

    if (strcmp(path, "STDOUT") == 0) {
        handle = stdout;
        mode = "w";
    } else if (strcmp(path, "STDERR") == 0) {
        handle = stderr;
        mode = "w";
    } else if (strcmp(path, "STDIN") == 0) {
        handle = stdin;
        mode = "r";
    } else {
        handle = fopen(path, mode);
    }

    if (handle == NULL) {
        int errsv = errno;
        setLastError(errsv, "%s", strerror(errsv));
        return errorResult("Failed to open file '%s': %s", path, strerror(errsv));
    }

    ObjInstance* fileInst = newInstance(fileClass);
    push(OBJ_VAL(fileInst));

    fileInst->foreignPtr = handle;

    Value result = okResult(OBJ_VAL(fileInst));

    pop();

    return result;
}

Value fileReadNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        runtimeError("File.read() must be called on a File instance.");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[-1]);
    FILE* handle = (FILE*)inst->foreignPtr;
    if (!handle) return errorResult("%s", "No open file handle found.");

    int length = -1;

    if (argCount >= 1 && IS_NUMBER(args[0])) {
        length = (int)AS_NUMBER(args[0]);
        if (length < 0) {
            return errorResult("%s", "Read length cannot be negative.");
        }
    } else {
        long currentPos = ftell(handle);
        if (currentPos < 0) {
            return errorResult("%s", "Cannot read unbounded stream (unseekable pipe or stdin).");
        }

        if (fseek(handle, 0L, SEEK_END) != 0) {
            return errorResult("%s", "Cannot seek file streadm.");
        }

        long endPos = ftell(handle);
        if (endPos < 0) {
            return errorResult("%s", "Cannot determine stream end size.");
        }

        if (fseek(handle, currentPos, SEEK_SET) != 0) {
            return errorResult("%s", "Failed to restore stream cursor position.");
        }

        length = (int)(endPos - currentPos);
    }

    if (length == 0) {
        return okResult(OBJ_VAL(copyString("", 0)));
    }

    char* buffer = (char*)malloc(length + 1);
    if (buffer == NULL) {
        return errorResult("%s", "Could not allocate read buffer.");
    }

    size_t bytesRead = fread(buffer, 1, length, handle);

    if (bytesRead == 0 && ferror(handle)) {
        free(buffer);
        return errorResult("%s", "Error reading data from file descriptor.");
    }

    ObjString* resultString = copyString(buffer, (int)bytesRead);
    free(buffer);

    push(OBJ_VAL(resultString));

    Value finalResult = okResult(OBJ_VAL(resultString));

    pop();

    return finalResult;
}

Value fileReadlineNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        runtimeError("File.readline() must be called on a File instance.");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[-1]);
    FILE* handle = (FILE*)inst->foreignPtr;
    if (!handle) return errorResult("%s", "No open file handle found.");

    size_t capacity = 512;
    size_t length = 0;
    char* buffer = (char*)malloc(capacity);

    if (buffer == NULL) {
        return errorResult("%s", "Could not allocate readline buffer.");
    }
    bool readAny = false;

    while (true) {
        char* writePtr = buffer + length;
        size_t remainingSpace = capacity - length;

        if (fgets(writePtr, (int)remainingSpace, handle) == NULL) {
            if (ferror(handle)) {
                free(buffer);
                return errorResult("%s", "Error reading data from file stream.");
            }
            break;
        }

        readAny = true;
        size_t segmentLength = strlen(writePtr);
        length += segmentLength;

        if (length > 0 && buffer[length - 1] == '\n') {
            break;
        }

        if (length >= capacity - 1) {
            size_t oldCapacity = capacity;
            capacity = oldCapacity * 2;
            char* newBuffer = (char*)realloc(buffer, capacity);

            if (newBuffer == NULL) {
                free(buffer);
                return errorResult("%s", "Out of memory while reading line.");
            }
            buffer = newBuffer;
        }
    }

    if (!readAny) {
        free(buffer);
        ObjString* emptyString = copyString("", 0);
        push(OBJ_VAL(emptyString));
        Value finalResult = okResult(OBJ_VAL(emptyString));
        pop();
        return finalResult;
    }

    ObjString* lineString = copyString(buffer, (int)length);
    free(buffer);

    push(OBJ_VAL(lineString));
    Value finalResult = okResult(OBJ_VAL(lineString));
    pop();

    return finalResult;
}

Value fileWriteNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        runtimeError("File.write() must be called on a File instance.");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[-1]);
    FILE* handle = (FILE*)inst->foreignPtr;

    if (handle == NULL) {
        return errorResult("%s", "Cannot write to a closed file descriptor.");
    }

    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("File.write() expects 1 string argument (data).");
        return NIL_VAL;
    }

    ObjString* str = AS_STRING(args[0]);

    if (str->length == 0) {
        return okResult(NUMBER_VAL(0));
    }

    size_t written = fwrite(str->chars, 1, str->length, handle);

    if (written < (size_t)str->length) {
        int errsv = errno;
        setLastError(errsv, "%s", strerror(errsv));
        return errorResult("Failed to write all data. wrote %zu of %d bytes; %s",
                written, str->length, strerror(errsv));
    }

    return okResult(NUMBER_VAL((double)written));
}

Value fileCloseNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        runtimeError("File.close( must be called on a File instance.");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[-1]);
    closeFileInternal(inst);
    return okResult(NIL_VAL);
}

Value fileSeekNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        runtimeError("File.seek() must be called on a File instance.");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[-1]);
    FILE* handle = (FILE*)inst->foreignPtr;

    if (!handle) {
        return errorResult("%s", "Cannot seek within a closed file descriptor.");
    }

    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("File.seek() requires at least 1 number argument(offset).");
        return NIL_VAL;
    }

    if (argCount == 2 && !IS_NUMBER(args[1])) {
        runtimeError("File.seek() second argument (whence) must be a number.");
        return NIL_VAL;
    }

    long offset = (long)AS_NUMBER(args[0]);
    int whence = 0;

    if (argCount == 2) {
        whence = (int)AS_NUMBER(args[1]);
    }

    if (fseek(handle, offset, whence) != 0) {
        int errsv = errno;
        setLastError(errsv, "%s", strerror(errsv));
        return errorResult("Failed to seek stream position: %s", strerror(errsv));
    }

    return okResult(NIL_VAL);
}

Value fileTellNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        runtimeError("File.tell() must be called on a File instance.");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[-1]);
    FILE* handle = (FILE*)inst->foreignPtr;

    if (!handle) {
        return errorResult("%s", "Cannot query position of a closed file descriptor.");
    }

    long position = ftell(handle);
    if (position == -1L) {
        int errsv = errno;
        setLastError(errsv, "%s", strerror(errsv));
        return errorResult("Failed to query stream position: %s", strerror(errsv));
    }

    return okResult(NUMBER_VAL((double)position));
}

Value fileStderrNative(int argCount, Value* args) {
    if (!IS_CLASS(args[-1])) {
        runtimeError("File.stderr() must be called as a class method.");
        return NIL_VAL;
    }
    ObjClass* fileClass = AS_CLASS(args[-1]);
    ObjInstance* instance = newInstance(fileClass);
    instance->foreignPtr = stderr;
    return OBJ_VAL(instance);
}

Value fileFlushNative(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        runtimeError("File.flush() must be called on a File instance.");
        return NIL_VAL;
    }

    ObjInstance* inst = AS_INSTANCE(args[-1]);
    FILE* handle = (FILE*)inst->foreignPtr;

    if (!handle) {
        return errorResult("%s", "Cannot flush a closed file descriptor.");
    }

    if (fflush(handle) == EOF) {
        int errsv = errno;
        setLastError(errsv, "%s", strerror(errsv));
        return errorResult("Failed to flush stream: %s", strerror(errsv));
    }

    return okResult(NIL_VAL);
}

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif

Value fileMkdirNative(int argCount, Value* args) {
    if (!IS_CLASS(args[-1])) {
        runtimeError("File.mkdir() must be called as a class method.");
        return NIL_VAL;
    }

    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("File.mkdir() expects a path string as the first argument.");
        return NIL_VAL;
    }

    int mode = 0755;

    if (argCount > 1 && IS_NUMBER(args[1])) {
        mode = (int)AS_NUMBER(args[1]);
    }

    const char* path = AS_CSTRING(args[0]);

    if (mkdir(path, mode) == 0) {
        return BOOL_VAL(true);
    }

    int errsv = errno;
    setLastError(errsv, "%s", strerror(errsv));
    return BOOL_VAL(false);
}

void initFileLibrary(){
    ObjString* fileName = copyString("File", 4);
    push(OBJ_VAL(fileName));
    ObjClass* fileClass = newClass(fileName);
    push(OBJ_VAL(fileClass));
    fileClass->destructor = fileDestructor;

    defineNativeMethod(fileClass, "load", fileLoadNative);
    defineNativeMethod(fileClass, "save", fileSaveNative);
    defineNativeMethod(fileClass, "exists", fileExistsNative);
    defineNativeMethod(fileClass, "list", fileListNative);
    defineNativeMethod(fileClass, "open", fileOpenNative);
    defineNativeMethod(fileClass, "read", fileReadNative);
    defineNativeMethod(fileClass, "readline", fileReadlineNative);
    defineNativeMethod(fileClass, "write", fileWriteNative);
    defineNativeMethod(fileClass, "close", fileCloseNative);
    defineNativeMethod(fileClass, "seek", fileSeekNative);
    defineNativeMethod(fileClass, "tell", fileTellNative);
    defineNativeMethod(fileClass, "stderr", fileStderrNative);
    defineNativeMethod(fileClass, "flush", fileFlushNative);
    defineNativeMethod(fileClass, "mkdir", fileMkdirNative);

    tableSet(&vm.globals, fileName, OBJ_VAL(fileClass));

    defineClassConstant(fileClass, "SEEK_SET", NUMBER_VAL(SEEK_SET));
    defineClassConstant(fileClass, "SEEK_CUR", NUMBER_VAL(SEEK_CUR));
    defineClassConstant(fileClass, "SEEK_END", NUMBER_VAL(SEEK_END));

    popn(2);

}

Value processRunStatic(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("Process.run() requires a string command argument.");
        return NIL_VAL;
    }

    const char* command = AS_CSTRING(args[0]);

    int status = system(command);

#ifdef WEXITSTATUS
    if (status != -1) {
        status = WEXITSTATUS(status);
    }
#endif

    return okResult(NUMBER_VAL((double)status));
}

Value processCaptureStatic(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("Process.execute() requires a string command argument.");
        return NIL_VAL;
    }

    const char* command = AS_CSTRING(args[0]);
    FILE* fp = popen(command, "r");
    if (fp == NULL) {
        int errsv = errno;
        return errorResult("Failed to spawn process stream: %s", strerror(errsv));
    }

    size_t capacity = 4096;
    size_t length = 0;
    char* buffer = ALLOCATE(char, capacity);

    while (true) {
        if (length + 1024 >= capacity) {
            size_t oldCapacity = capacity;
            capacity = GROW_CAPACITY(oldCapacity);
            buffer = GROW_ARRAY(char, buffer, oldCapacity, capacity);
        }

        if (fgets(buffer + length, (int)(capacity - length), fp) == NULL) {
            break;
        }

        length += strlen(buffer + length);
    }

    int status = pclose(fp);
    if (status == -1) {
        FREE_ARRAY(char, buffer, capacity);
        return errorResult("Process failed to terminate cleanly.");
    }

    size_t exactSize = length + 1;
    buffer = GROW_ARRAY(char, buffer, capacity, exactSize);
    buffer[length] = '\0';

    ObjString* outputString = takeString(buffer, (int)length);
    return okResult(OBJ_VAL(outputString));
}

Value processForkStatic(int argCount, Value* args) {
    fflush(NULL);

    pid_t pid = fork();

    if (pid < 0) {
        int errsv = errno;
        setLastError(errsv, "%s", strerror(errsv));
        runtimeError("fork() failed: %s", strerror(errsv));
        return errorResult("Failed to fork process: %s", strerror(errsv));
    }

    return okResult(NUMBER_VAL((double)pid));
}

static char* valueToCString(Value val) {
    if (IS_STRING(val)) {
        return strdup(AS_CSTRING(val));
    } else if (IS_NUMBER(val)) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%g", AS_NUMBER(val));
        return strdup(buffer);
    } else if (IS_BOOL(val)) {
        return strdup(AS_BOOL(val) ? "true" : "false");
    } else if (IS_NIL(val)) {
        return strdup("nil");
    }
    return strdup("object");
}

Value processExecStatic(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("exec() expects a string path as the first argument.");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);

    ObjArray* argsArray = NULL;
    if (argCount >= 2 && !IS_NIL(args[1])) {
        if (!IS_ARRAY(args[1])) {
            runtimeError("Second argument to exec() must be an Array or nil.");
            return NIL_VAL;
        }
        argsArray = AS_ARRAY(args[1]);
    }

    int arrayCount = argsArray ? argsArray->count : 0;

    char** argv = malloc((arrayCount + 2) * sizeof(char*));
    if (argv == NULL) {
        runtimeError("Out of memory converting arguments for exec.");
        return NIL_VAL;
    }

    argv[0] = strdup(path);

    for (int i = 0; i < arrayCount; i++) {
        argv[i + 1] = valueToCString(argsArray->values[i]);
    }
    argv[arrayCount + 1] = NULL;

    if (argCount >= 3 && IS_MAP(args[2])) {
        if (!IS_MAP(args[2])) {
            for (int i = 0; i <= arrayCount; i++) {
                free(argv[i]);
                free(argv);
                runtimeError("exec() third argument must be a Map or nil.");
                return NIL_VAL;
            }

            ObjMap* envMap = AS_MAP(args[2]);
            for (int i = 0; i < envMap->items.capacity; i++) {
                Entry* entry = &envMap->items.entries[i];

                if (entry->key != NULL) {
                    char* valStr = valueToCString(entry->value);
                    setenv(entry->key->chars, valStr, 1);
                    free(valStr);
                }
            }
        }

    }

    execvp(path, argv);

    int errsv = errno;
    for (int i = 0; i < arrayCount; i++) {
        free(argv[i]);
    }
    free(argv);

    setLastError(errsv, "%s", strerror(errsv));
    runtimeError("exec() failed: %s", strerror(errsv));

    return NIL_VAL;
}

Value processPidStatic(int argCount, Value* args) {
    return NUMBER_VAL((double)getpid());
}

Value processWaitStatic(int argCount, Value* args) {
    pid_t targetPid = -1;

    if (argCount > 0) {
        if (!IS_NUMBER(args[0])) {
            runtimeError("Process.wait() argument must be a process ID number.");
            return NIL_VAL;
        }
        targetPid = (pid_t)AS_NUMBER(args[0]);
    }

    int status;
    pid_t reapedPid = waitpid(targetPid, &status, 0);

    if (reapedPid < 0) {
        int errsv = errno;
        setLastError(errsv, "%s", strerror(errsv));
        runtimeError("wait() failed: %s", strerror(errsv));
        return errorResult("Wait failed: %s", strerror(errsv));
    }

    int exitCode = 0;
#ifdef WEXITSTATUS
    if (WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }
#endif

    return okResult(NUMBER_VAL((double)exitCode));
}

Value processPipeStatic(int argCount, Value* args) {
    int fds[2];
    if (pipe(fds) < 0) {
        return errorResult("Failed to allocate OS pipe: %s", strerror(errno));
    }

    ObjMap* pipeMap = newMap();
    push(OBJ_VAL(pipeMap));

    ObjString* readKey = copyString("read", 4);
    push(OBJ_VAL(readKey));
    mapSet(pipeMap, readKey, NUMBER_VAL((double)fds[0]));
    pop();

    ObjString* writeKey = copyString("write", 5);
    push(OBJ_VAL(writeKey));
    mapSet(pipeMap, writeKey, NUMBER_VAL((double)fds[1]));
    pop();

    pop();
    return okResult(OBJ_VAL(pipeMap));
}

Value processReadStatic(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("Process.read() requires a numeric descriptor.");
        return NIL_VAL;
    }

    int fd = (int)AS_NUMBER(args[0]);
    size_t maxBytes = 4096;

    char* buffer = ALLOCATE(char, maxBytes + 1);
    ssize_t bytesRead = read(fd, buffer, maxBytes);

    if (bytesRead < 0) {
        FREE_ARRAY(char, buffer, maxBytes + 1);
        return errorResult("Failed to read from stream: %s", strerror(errno));
    }

    buffer[bytesRead] = '\0';
    buffer = GROW_ARRAY(char, buffer, maxBytes + 1, bytesRead + 1);

    ObjString* outString = takeString(buffer, bytesRead);
    return okResult(OBJ_VAL(outString));
}

Value processCloseStatic(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("Process.close() requires a numeric descriptor.");
        return NIL_VAL;
    }

    close((int)AS_NUMBER(args[0]));
    return NIL_VAL;
}

Value processWriteStatic(int argCount, Value* args) {
    if (argCount < 2 || !IS_NUMBER(args[0]) || !IS_STRING(args[1])) {
        runtimeError("Process.write() requires a numeric descriptor and a string message.");
        return NIL_VAL;
    }

    int fd = (int)AS_NUMBER(args[0]);
    ObjString* message = AS_STRING(args[1]);

    ssize_t bytesWritten = write(fd, message->chars, message->length);
    if (bytesWritten < 0) {
        return errorResult("Failed to write to stream: %s", strerror(errno));
    }

    return okResult(NUMBER_VAL((double)bytesWritten));
}

void initProcessClass() {
    ObjString* processName = copyString("Process", 7);
    push(OBJ_VAL(processName));
    ObjClass* processClass = newClass(processName);
    push(OBJ_VAL(processClass));
    tableSet(&vm.globals, processName, OBJ_VAL(processClass));

    defineNativeMethod(processClass, "run", processRunStatic);
    defineNativeMethod(processClass, "capture", processCaptureStatic);
    defineNativeMethod(processClass, "fork", processForkStatic);
    defineNativeMethod(processClass, "exec", processExecStatic);
    defineNativeMethod(processClass, "pid", processPidStatic);
    defineNativeMethod(processClass, "wait", processWaitStatic);
    defineNativeMethod(processClass, "pipe", processPipeStatic);
    defineNativeMethod(processClass, "read", processReadStatic);
    defineNativeMethod(processClass, "write", processWriteStatic);
    defineNativeMethod(processClass, "close", processCloseStatic);


    processClass->isFrozen = true;

    pop();
    pop();
}

Value structPackNative(int argCount, Value* args) {
    if (argCount < 2) {
        runtimeError("Struct.pack() expects at least 2 arguments (format string, value array).");
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError("First argument to Struct.pack() must be a format string.");
        return NIL_VAL;
    }

    if (!IS_ARRAY(args[1])) {
        runtimeError("Second argument to Struct.pack() must be an Array.");
        return NIL_VAL;
    }

    const char* format = AS_CSTRING(args[0]);
    ObjArray* array = AS_ARRAY(args[1]);
    bool bigend = (argCount == 3) ? AS_BOOL(args[2]) : true;

    const char* f = format;
    int val_index = 0;
    int totalSize = 0;

    while (*f != '\0') {
        if (isspace(*f)) {
            f++;
            continue;
        }

        int width = 0;
        bool hasWidth = false;
        while (isdigit(*f)) {
            width = width * 10 + (*f - '0');
            hasWidth = true;
            f++;
        }

        const char type = *f;
        if (type != '\0') {
            if (val_index >= array->count) {
                runtimeError("Format string requires more values than provided in the array.");
                return NIL_VAL;
            }

            Value currentVal = array->values[val_index];

            switch (type) {
                case 'B': totalSize += 1;  break;
                case 'H': totalSize += 2;  break;
                case 'I': totalSize += 4;  break;
                case 'Q': totalSize += 8;  break;
                case 's':
                      {
                          if (!IS_STRING(currentVal)) {
                              runtimeError("Expected string value for 's' format specifier.");
                              return NIL_VAL;
                          }
                          totalSize += hasWidth ? width : AS_STRING(currentVal)->length;
                      }
                      break;
                default:
                      runtimeError("Unknown format specifier '%c'.", *f);
                      return NIL_VAL;
            }
            val_index++;
            f++;
        }
    }

    uint8_t* buffer = (uint8_t*)calloc(1, totalSize);
    uint8_t* cursor = buffer;
    f = format;
    val_index = 0;

    while (*f != '\0') {
        if (isspace(*f)) {
            f++;
            continue;
        }

        int width = 0;
        bool hasWidth = false;

        while (isdigit(*f)) {
            width = width * 10 + (*f - '0');
            hasWidth = true;
            f++;
        }

        const char type = *f;
        switch (type) {
            case 'B':
                double num = AS_NUMBER(array->values[val_index++]);
                *cursor++ = (uint8_t)num;
                break;
            case 'H':
                {
                    uint16_t val = (uint16_t)AS_NUMBER(array->values[val_index++]);
                    if (bigend) {
                        *cursor++ = (val >> 8) & 0xff;
                        *cursor++ = val & 0xff;
                    } else {
                        *cursor++ = val & 0xff;
                        *cursor++ = (val >> 8) & 0xff;
                    }
                }
                break;
            case 'I':
                {
                    uint32_t val = (uint32_t)AS_NUMBER(array->values[val_index++]);
                    if (bigend) {
                        *cursor++ = (val >> 24) & 0xff;
                        *cursor++ = (val >> 16) & 0xff;
                        *cursor++ = (val >> 8) & 0xff;
                        *cursor++ = val & 0xff;
                    } else {
                        *cursor++ = val & 0xff;
                        *cursor++ = (val >> 8) & 0xff;
                        *cursor++ = (val >> 16) & 0xff;
                        *cursor++ = (val >> 24) & 0xff;
                    }
                }
                break;
            case 'Q':
                {
                    uint64_t val = (uint64_t)AS_NUMBER(array->values[val_index++]);
                    if (bigend) {
                        for (int i = 7; i >= 0; i--) {
                            *cursor++ = (val >> (i * 8)) & 0xff;
                        }
                    } else {
                        for (int i = 0; i < 8; i++) {
                            *cursor++ = (val >> (i * 8)) & 0xff;
                        }
                    }
                }
                break;
            case 's':
                {
                    ObjString* s = AS_STRING(array->values[val_index++]);
                    int finalWidth = hasWidth ? width : s->length;
                    int copyLen = (s->length < finalWidth) ? s->length : finalWidth;

                    memcpy(cursor, s->chars, copyLen);
                    cursor += copyLen;

                    if (copyLen < finalWidth) {
                        memset(cursor, 0, finalWidth - copyLen);
                        cursor += (finalWidth - copyLen);
                    }
                }
                break;
        }
        if (*f != '\0') f++;
    }

    ObjString* result = copyString((const char*)buffer, totalSize);
    free(buffer);
    return OBJ_VAL(result);
}

Value structUnpackNative(int argCount, Value* args) {
    if (argCount < 2) {
        runtimeError("Struct.unpack() expects at least 2 arguments (format string, data string).");
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError("First argument to Struct.unpack() must be a format string.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError("Second argument to Struct.unpack() must be a data string.");
        return NIL_VAL;
    }

    const char* format = AS_CSTRING(args[0]);
    ObjString* data = AS_STRING(args[1]);
    const uint8_t* buffer = (const uint8_t*)data->chars;
    bool bigend = (argCount >= 3) ? AS_BOOL(args[2]) : true;

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    const char* f = format;
    int offset = 0;

    while (*f != '\0') {
        if (isspace(*f)) {
            f++;
            continue;
        }

        int width = 0;
        bool hasWidth = false;
        while (isdigit(*f)) {
            width = width * 10 + (*f - '0');
            hasWidth = true;
            f++;
        }

        const char type = *f;
        if (type == '\0') {
            runtimeError("Format string ended unexpectedly after width specifier.");
            return NIL_VAL;
        }

        int requiredSize = 0;
        switch (type) {
            case 'B': requiredSize = 1; break;
            case 'H': requiredSize = 2; break;
            case 'I': requiredSize = 4; break;
            case 'Q': requiredSize = 8; break;
            case 's': requiredSize = hasWidth ? width : (data->length - offset); break;
            default:
                      runtimeError("Unknown format specifier '%c'.", type);
                      return NIL_VAL;
        }

        if (offset + requiredSize > data->length) {
            runtimeError("Buffer underflow: Data string is too short o unpack the specified format.");
            return NIL_VAL;
        }

        switch (type) {
            case 'B':
                {
                    uint8_t val = buffer[offset];
                    arrayAppend(result, NUMBER_VAL((double)val));
                    offset += 1;
                }
                break;
            case 'H':
                {
                    uint16_t val;
                    if (bigend) {
                        val = (buffer[offset] << 8) | buffer[offset + 1];
                    } else {
                        val = buffer[offset] | (buffer[offset + 1] << 8);
                    }
                    arrayAppend(result, NUMBER_VAL((double)val));
                    offset += 2;
                }
                break;
            case 'I':
                {
                    uint32_t val;
                    if (bigend) {
                        val = ((uint32_t)buffer[offset] << 24) |
                            ((uint32_t)buffer[offset + 1] << 16) |
                            ((uint32_t)buffer[offset + 2] << 8) |
                            (uint32_t)buffer[offset + 3];
                    } else {
                        val = (uint32_t)buffer[offset] |
                            ((uint32_t)buffer[offset + 1] << 8) |
                            ((uint32_t)buffer[offset + 2] << 16) |
                            ((uint32_t)buffer[offset + 3] << 24);
                    }
                    arrayAppend(result, NUMBER_VAL((double)val));
                    offset += 4;
                }
                break;
            case 'Q':
                {
                    uint64_t val = 0;
                    if (bigend) {
                        for (int i = 0; i < 8; i++) {
                            val = (val << 8) | buffer[offset + i];
                        }
                    } else {
                        for (int i = 7; i >= 0; i--) {
                            val = (val << 8) | buffer[offset + i];
                        }
                    }
                    arrayAppend(result, NUMBER_VAL((double)val));
                    offset += 8;
                }
                break;
            case 's':
                {
                    ObjString* str = copyString((const char*)buffer + offset, requiredSize);
                    push(OBJ_VAL(str));
                    arrayAppend(result, OBJ_VAL(str));
                    pop();
                    offset += requiredSize;
                }
                break;
        }
        f++;
    }

    pop();
    return OBJ_VAL(result);
}

void initStructClass() {
    ObjString* string = NULL;

    string = copyString("Struct", 6);
    push(OBJ_VAL(string));
    ObjClass* structClass = newClass(string);
    push(OBJ_VAL(structClass));
    structClass->superclass = vm.objectClass;
    tableSet(&vm.globals, string, OBJ_VAL(structClass));

    defineNativeMethod(structClass, "pack", structPackNative);
    defineNativeMethod(structClass, "unpack", structUnpackNative);

    pop();
    pop();
}

Value hgfGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("heap_growth_Factor() expects a numeric multiplier.");
        return NIL_VAL;
    }

    double val = AS_NUMBER(args[0]);

    if (val < 1.1) {
        runtimeError("Heap growth factor must be 1.1 or greater to avoid collection thrashing.");
        return NIL_VAL;
    }

    vm.heap_growth_factor = val;
    return args[0];
}

Value get_hgfGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_growth_factor() takes 0 arguments.");
        return NIL_VAL;
    }

    return NUMBER_VAL(vm.heap_growth_factor);
}

Value thresholdGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("init_threshold() expects a numeric byte size.");
        return NIL_VAL;
    }

    double val = AS_NUMBER(args[0]);
    if (val < 0) {
        runtimeError("Initial GC threshold cannot be negative.");
        return NIL_VAL;
    }

    vm.init_threshold = (size_t)val;
    return args[0];
}

Value get_thresholdGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_threshold() takes 0 arguments.");
        return NIL_VAL;
    }

    return NUMBER_VAL(vm.init_threshold);
}

Value bumpsizeGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("bump_size() expects a numeric byte size.");
        return NIL_VAL;
    }

    double val = AS_NUMBER(args[0]);
    if (val < 0) {
        runtimeError("GC bump size cannot be negative.");
        return NIL_VAL;
    }

    if (val < 4096) {
        runtimeError("GC bump size must be at least 4096 bytes (4Kb) to prevent thrashing.");
        return NIL_VAL;
    }

    vm.bump_size = (size_t)val;

    return args[0];
}

Value get_bumpsizeGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_bumpsize() takes 0 arguments.");
        return NIL_VAL;
    }

    return NUMBER_VAL(vm.bump_size);
}

Value stressmodeGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("stress_mode() expects an integer (GC.NormalMode, GC.StressMode, GC.DisabledMode."); 
        return NIL_VAL;
    }

    double val = AS_NUMBER(args[0]);

    if (val != 0.0 && val != 1.0 && val != 2.0) {
        runtimeError("Invalied stress mode.");
        return NIL_VAL;
    }

    vm.stress_mode = (int)val;

    return args[0];
}

Value get_stressmodeGCNative(int argCount, Value* args) {
    if (argCount > 1) {
        runtimeError("get_stressmode() takes 0 arguments.");
        return NIL_VAL;
    }

    return NUMBER_VAL(vm.stress_mode);
}

Value typeGCNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("type() expects an integer (GC.TypeLinear = Linear/Bump, GC.TypeMult Multipler.");
        return NIL_VAL;
    }


    double val = AS_NUMBER(args[0]);
    if (val != 0.0 && val != 1.0) {
        runtimeError("Invalid GC strategy type. Use GC.TypeLinear (0) or GC.TypeMult (1).");
        return NIL_VAL;
    }

    vm.gctype = (int)val;
    return args[0];
}

Value get_typeGCNative(int argCount, Value* args) {
    if (argCount > 0) {
        runtimeError("get_gctype() expects 0 arguments.");
        return NIL_VAL;
    }
    return NUMBER_VAL((double)vm.gctype);
}

Value systemGCNative(int argCount, Value* args) {
    collectGarbage();
    return NIL_VAL;
}

void initGCLibrary() {
    ObjString* gcName = copyString("GC", 2);
    push(OBJ_VAL(gcName));
    ObjClass* gcClass = newClass(gcName);
    push(OBJ_VAL(gcClass));

    defineNativeMethod(gcClass, "heap_growth_factor", hgfGCNative);
    defineNativeMethod(gcClass, "get_growth_factor", get_hgfGCNative);
    defineNativeMethod(gcClass, "init_threshold", thresholdGCNative);
    defineNativeMethod(gcClass, "get_threshold", get_thresholdGCNative);
    defineNativeMethod(gcClass, "bump_size", bumpsizeGCNative);
    defineNativeMethod(gcClass, "get_bumpsize", get_bumpsizeGCNative);
    defineNativeMethod(gcClass, "stress_mode", stressmodeGCNative);
    defineNativeMethod(gcClass, "get_stress_mode", get_stressmodeGCNative);
    defineNativeMethod(gcClass, "type", typeGCNative);
    defineNativeMethod(gcClass, "get_gctype", get_typeGCNative);
    // same as System.gc()
    defineNativeMethod(gcClass, "gc", systemGCNative);

    tableSet(&vm.globals, gcName, OBJ_VAL(gcClass));

    defineClassConstant(gcClass, "NormalMode", NUMBER_VAL(0));
    defineClassConstant(gcClass, "StressMode", NUMBER_VAL(1));
    defineClassConstant(gcClass, "DisabledMode", NUMBER_VAL(2));

    defineClassConstant(gcClass, "TypeLinear", NUMBER_VAL(0));
    defineClassConstant(gcClass, "TypeMult", NUMBER_VAL(1));

    pop();
    pop();
}

void ioDestructor(ObjInstance* inst) {
    if (inst->foreignPtr != NULL) {
        SocketInternal* so = (SocketInternal*)inst->foreignPtr;
        if (so->fd < 0) {
            close(so->fd);
        }
        FREE(SocketInternal, inst->foreignPtr);
        inst->foreignPtr = NULL;
    }
}

Value ioCallHandler(int argCount, Value* args) {
    ObjClass* klass = AS_CLASS(args[-1]);

    if (strcmp(klass->name->chars, "IO") == 0) {
        runtimeError("Cannot instantiate IO base class directly. Use IO.tcp() or IO.udp().");
        return NIL_VAL;
    }

    ObjInstance* instance = newInstance(klass);
    push(OBJ_VAL(instance));

    int sockType = (strcmp(klass->name->chars, "udp") == 0) ? SOCK_DGRAM : SOCK_STREAM;

    int fd = socket(AF_INET, sockType, 0);
    if (sockType == SOCK_STREAM) {
        int opt = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    }

    if (fd == -1) {
        runtimeError("Could not create OS socket.");
        return NIL_VAL;
    }

    SocketInternal* so = ALLOCATE(SocketInternal, 1);
    so->fd = fd;
    so->type = sockType;
    so->connected = false;

    instance->foreignPtr = so;
    pop();

    return OBJ_VAL(instance);
}

Value ioTcpNative(int argCount, Value* args) {
    Value tcpClassVal;
    if (!tableGet(&vm.globals, copyString("Tcp", 3), &tcpClassVal)) {
    }
}

Value ioInspectNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    if (instance->foreignPtr != NULL) {
        SocketInternal* so = (SocketInternal*)instance->foreignPtr;
        printf("========\n");
        printf("fd: %d\n", so->fd);
        printf("type: %d\n", so->type);
        printf("connected: %s\n", so->connected == true ? "TRUE" : "FALSE");
        printf("========\n");
    } else {
        printf("[Socket]: <Closed/Deallocated>\n");
    }

    return NIL_VAL;
}

Value ioConnectNative(int argCount, Value* args) {
    clearLastError();

    if (argCount < 2) {
        runtimeError("Connect expects at least 2 arguments: (ip, port).");
        return errorResult("Connect expects at least 2 arguments: (ip, port).");
    }

    if (!IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("Connect expects a string (host) and a number (port).");
        return errorResult("Invalid argument types.");
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL) {
        return errorResult("Socket not initialized.");
    }

    if (so->connected) {
        return errorResult("Socket is already connected.");
    }

    double timeoutVal = 0.0;
    if (argCount > 2) {
        if (!IS_NUMBER(args[2])) {
            runtimeError("Timeout must be a numeric value.");
            return errorResult("Invalid timeout type.");
        }
        timeoutVal = AS_NUMBER(args[2]);
        if (timeoutVal < 0.0) {
            runtimeError("Timeout cannot be negative.");
            return errorResult("Negative timeout.");
        }
    }

    if (so->fd == -1) {
        so->fd = socket(AF_INET, so->type, 0);
        if (so->fd == -1) {
            return errorResult("Failed to re-open socket.");
        }
    }

    double portNum = AS_NUMBER(args[1]);
    int port = (int)portNum;
    if (portNum != (double)port || port < 0 || port > 65535) {
        runtimeError("Port must be an integer between 0 and 65535.");
        return errorResult("Invalid port value.");
    }

    const char* host = AS_CSTRING(args[0]);
    char portStr[6];
    snprintf(portStr, sizeof(portStr), "%d", port);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = so->type;

    int status = getaddrinfo(host, portStr, &hints, &res);
    if (status != 0) {
        return errorResult("getaddrinfo: %s", gai_strerror(status));
    }

    int flags = fcntl(so->fd, F_GETFL, 0);
    if (timeoutVal > 0.0) {
        fcntl(so->fd, F_SETFL, flags | O_NONBLOCK);
    }

    int cres = connect(so->fd, res->ai_addr, res->ai_addrlen);

    if (cres < 0 && errno != EINPROGRESS) {
        freeaddrinfo(res);
        if (timeoutVal > 0.0) fcntl(so->fd, F_SETFL, flags);
        return errorResult("Immediate connection failure: %s", strerror(errno));
    }

    if (cres != 0) {
        struct pollfd pfd;
        pfd.fd = so->fd;
        pfd.events = POLLOUT;

        int timeout_ms = (int)(timeoutVal * 1000.0);
        int poll_res = poll(&pfd, 1, timeout_ms);

        if (poll_res <= 0) {
            if (timeoutVal > 0.0) fcntl(so->fd, F_SETFL, flags);
            close(so->fd);
            so->fd = -1;
            freeaddrinfo(res);
            return errorResult("Connection timed out afte %g seconds", timeoutVal);
        }

        int so_error;
        socklen_t len = sizeof(so_error);
        getsockopt(so->fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
            if (timeoutVal > 0.0) fcntl(so->fd, F_SETFL, flags);
            close(so->fd);
            so->fd = -1;
            freeaddrinfo(res);
            return errorResult("Connect error: %s", strerror(so_error));
        }
    }

    if (timeoutVal > 0.0) {
        fcntl(so->fd, F_SETFL, flags);
    }

    freeaddrinfo(res);
    so->connected = true;
    return okResult(BOOL_VAL(true));
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

Value ioSendNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("send() expects a string as the first argument.");
        return errorResult("%s", "send() expects a string as the first argument.");
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL || so->fd == -1 || !so->connected) {
        return errorResult("%s", "Socket is not initialized or connected.");
    }

    ObjString* data = AS_STRING(args[0]);

    ssize_t bytesSent = send(so->fd, data->chars, data->length, MSG_NOSIGNAL);

    if (bytesSent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return okResult(NUMBER_VAL(0.0));
        }
        return errorResult("Socket write error: %s.", strerror(errno));
    }
    
    return okResult(NUMBER_VAL((double)bytesSent));
}

Value ioRecvNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("recv() expects a buffer size number as the first argument.");
        return errorResult("recv() expects a buffer size number as the first argument.");
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL || so->fd == -1 || !so->connected) {
        runtimeError("Socket is not initialized or connected.");
        return errorResult("Socket is not initialized or connected");
    }

    double requestedLength = AS_NUMBER(args[0]);
    if (requestedLength <= 0) {
        runtimeError("recv() buffer size must be greater than 0.");
        return errorResult("recv() buffer size must be greater than 0.");
    }

    if (requestedLength > 16 * 1024 * 1024) {
        runtimeError("recv() buffer size exceeds maximum limit of 16MB.");
        return errorResult("recv() buffer size exceeds maximum limit of 16MB.");
    }

    int length = (int)requestedLength;
    char* buffer = (char*)malloc(length);

    if (buffer == NULL) {
        return errorResult("Out of memory allocating receiver buffer.");
    }

    ssize_t bytesRead = recv(so->fd, buffer, length, 0);

    if (bytesRead < 0 ) {
        free(buffer);

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ObjString* emptyStr = copyString("", 0);
            push(OBJ_VAL(emptyStr));
            Value resultVal = okResult(OBJ_VAL(emptyStr));
            pop();
            return resultVal;
        }
        return errorResult("SOcket read error: %s", strerror(errno));
    }

    if (bytesRead == 0) {
        free(buffer);
        return errorResult("Connection closed by peer.");
    }

    ObjString* result = copyString(buffer, (int)bytesRead);
    free(buffer);

    push(OBJ_VAL(result));
    Value resultVal = okResult(OBJ_VAL(result));
    pop();

    return resultVal;
}

Value ioListenNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("listen() expects a port number as the first argument.");
        return errorResult("listen() expects a port number as the first argument.");
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL || so->fd == -1) {
        runtimeError("Socket is not initialized.");
        return errorResult("Socket is not initialized.");
    }

    double portVal = AS_NUMBER(args[0]);
    if (portVal < 0 || portVal > 65535) {
        runtimeError("Port number must be between 0 and 65535.");
        return errorResult("Port number must be between 0 and 65535.");
    }
    int port = (int)portVal;

    int opt = 1;
    if (setsockopt(so->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "[Warning] Faile dto set SO_REUSEADDR: %s\n", strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(so->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        return errorResult("Could not bind to port %d: %s", port, strerror(errno));
    }

    if (listen(so->fd, 128) < 0) {
        return errorResult("Could not listen on socket: %s", strerror(errno));
    }

    return okResult(args[-1]);
}

Value ioAcceptNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL || so->fd == -1) {
        runtimeError("Server socket is not initialized.");
        return errorResult("Server socket is not initialized.");
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(so->fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return okResult(NIL_VAL);
        }

        return errorResult("Accept failed: %s", strerror(errno));
    }
    
    ObjClass* tcpClass = instance->obj.klass;
    ObjInstance* client_instance = newInstance(tcpClass);

    push(OBJ_VAL(client_instance));

    SocketInternal* client_so = ALLOCATE(SocketInternal, 1);
    client_so->fd = client_fd;
    client_so->type = SOCK_STREAM;
    client_so->connected = true;
    client_instance->foreignPtr = client_so;

    Value resultVal = okResult(OBJ_VAL(client_instance));

    pop();

    return resultVal;
}

Value ioBindNative(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("bind() expects an IP address string and a port number.");
        return errorResult("bind() expects an IP address string and a port number.");
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL || so->fd == -1) {
        runtimeError("Socket is not initialized.");
        return errorResult("Socket is not initialized.");
    }

    double portVal = AS_NUMBER(args[1]);
    if (portVal < 0 || portVal > 65535) {
        runtimeError("Port number must be between 0 and 65535.");
        return errorResult("Port number must be between 0 and 65535.");
    }
    int port = (int)portVal;

    ObjString* ipStr = AS_STRING(args[0]);
    const char* ip = ipStr->chars;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        return errorResult("Invalid IP address format: %s", ip);
    }

    int opt = 1;
    if (setsockopt(so->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "[Warning] Failed to get SO_REUSEADDR: %s\n", strerror(errno));
    }

    if (bind(so->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        return errorResult("Could not bind to %s:%s: %s", ip, port, strerror(errno));
    }

    return okResult(args[-1]);
}

Value ioReadableNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL || so->fd == -1) {
        return errorResult("Socket is not initialized.");
    }

    struct pollfd pfd;
    pfd.fd = so->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ready = poll(&pfd, 1, 0);

    if (ready < 0) {
        return errorResult("Failed to check socket readability: %s", strerror(errno));
    }

    bool isReadable = (ready > 0) && (pfd.revents & (POLLIN | POLLHUP | POLLERR));

    return okResult(BOOL_VAL(isReadable));
}

Value ioCloseNative(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL) {
        return okResult(NIL_VAL);
    }

    if (so->fd != -1) {
        if (so->type == SOCK_STREAM && so->connected) {
            shutdown(so->fd, SHUT_RDWR);
        }
        close(so->fd);
        so->fd = -1;
        so->connected = false;
    }

    return okResult(NIL_VAL);
}

Value ioPollNative(int argCount, Value* args) {
    if (!IS_CLASS(args[-1])) {
        return errorResult("poll() must be called on the IO.class.");
    }

    if (argCount < 1 || !IS_ARRAY(args[0])) {
        return errorResult("poll() must have an array of sockets as the first argument.");
    }

    int eventMask = POLLIN;
    if (argCount > 2 && IS_NUMBER(args[2])) {
        eventMask = (int)AS_NUMBER(args[2]);
    }

    ObjArray* socket_array = AS_ARRAY(args[0]);

    int max_fds = socket_array->count > 0 ? socket_array->count : 1;
    struct pollfd fds[max_fds];
    int valid_fd_count = 0;

    for (int i = 0; i < socket_array->count; i++) {
        Value item = socket_array->values[i];
        if (!IS_INSTANCE(item)) continue;

        ObjInstance* instance = AS_INSTANCE(item);
        SocketInternal* so = (SocketInternal*)instance->foreignPtr;

        if (so != NULL && so->fd != 1) {
            fds[valid_fd_count].fd = so->fd;
            fds[valid_fd_count].events = eventMask;
            fds[valid_fd_count].revents = 0;
            valid_fd_count++;
        }
    }

    int timeout = (argCount < 1 && IS_NUMBER(args[1])) ? (int)AS_NUMBER(args[1]) : -1;
    int pollResult = poll(fds, valid_fd_count, timeout);

    if (pollResult < 0) {
        return errorResult("Poll failed: %s", strerror(errno));
    }

    ObjArray* array = newArray();
    push(OBJ_VAL(array));

    if (pollResult > 0) {
        int fds_idx = 0;

        for (int i = 0; i < socket_array->count; i++) {
            Value item = socket_array->values[i];
            if (!IS_INSTANCE(item)) continue;

            ObjInstance* instance = AS_INSTANCE(item);
            SocketInternal* so = (SocketInternal*)instance->foreignPtr;

            if (so != NULL && so->fd != -1) {
                short revents = fds[fds_idx].revents;

                if (revents & (eventMask | POLLERR | POLLHUP)) {
                    arrayAppend(array, item);
                }
                fds_idx++;
            }
        }
    }
    Value resultVal = okResult(OBJ_VAL(array));
    pop();
    return resultVal;
}

Value ioSetRecvTimeoutNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("set_recv_timeout() expects a timeout in milliseconds.");
        return errorResult("set_recv_timeout() expects a timeout in milliseconds.");
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SocketInternal* so = (SocketInternal*)instance->foreignPtr;

    if (so == NULL || so->fd == -1) {
        return errorResult("Socket is not initialized.");
    }

    double requestedMs = AS_NUMBER(args[0]);
    if (requestedMs < 0) {
        runtimeError("Timeout value cannot be negative.");
        return errorResult("Timeout value cannot be negative.");
    }

    int ms = (int)requestedMs;

    struct timeval timeout;
    timeout.tv_sec = ms / 1000;
    timeout.tv_usec = (ms % 1000) * 1000;

    if (setsockopt(so->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        return errorResult("Failed to set receive timeout: %s", strerror(errno));
    }

    return okResult(args[-1]);
}

void initIOClass() {

    ObjString* ioName = copyString("IO", 2);
    push(OBJ_VAL(ioName));

    ObjClass* ioClass = newClass(ioName);
    push(OBJ_VAL(ioClass));

    ioClass->superclass = vm.objectClass;
    ioClass->callHandler = ioCallHandler;
    ioClass->destructor = ioDestructor;

    tableSet(&vm.globals, ioName, OBJ_VAL(ioClass));

    ObjString* tcpName = copyString("tcp", 3);
    push(OBJ_VAL(tcpName));

    ObjClass* tcpClass = newClass(tcpName);
    push(OBJ_VAL(tcpClass));

    tcpClass->superclass = ioClass;
    tcpClass->callHandler = ioCallHandler;
    tcpClass->destructor = ioDestructor;
    tableSet(&ioClass->methods, tcpName, OBJ_VAL(tcpClass));

    //defineClassConstant(ioClass, "tcp", OBJ_VAL(tcpClass));

    pop(); // tcpClass
    pop(); // tcpName

    ObjString* udpName = copyString("udp", 3);
    push(OBJ_VAL(udpName));

    ObjClass* udpClass = newClass(udpName);
    push(OBJ_VAL(udpClass));

    udpClass->superclass = ioClass;
    udpClass->callHandler = ioCallHandler;
    udpClass->destructor = ioDestructor;

    tableSet(&ioClass->methods, udpName, OBJ_VAL(udpClass));
    //defineClassConstant(ioClass, "udp", OBJ_VAL(udpClass));

    pop(); // udpClass
    pop(); // udpName

    defineNativeMethod(ioClass, "inspect", ioInspectNative);
    defineNativeMethod(ioClass, "connect", ioConnectNative);
    defineNativeMethod(ioClass, "send", ioSendNative);
    defineNativeMethod(ioClass, "recv", ioRecvNative);
    defineNativeMethod(ioClass, "listen", ioListenNative);
    defineNativeMethod(ioClass, "accept", ioAcceptNative);
    defineNativeMethod(ioClass, "bind", ioBindNative);
    defineNativeMethod(ioClass, "readable", ioReadableNative);
    defineNativeMethod(ioClass, "close", ioCloseNative);
    defineNativeMethod(ioClass, "poll", ioPollNative);
    defineNativeMethod(ioClass, "set_recv_timeout", ioSetRecvTimeoutNative);

    defineClassConstant(ioClass, "PollIn", NUMBER_VAL(POLLIN));
    defineClassConstant(ioClass, "PollOut", NUMBER_VAL(POLLOUT));
    defineClassConstant(ioClass, "PollErr", NUMBER_VAL(POLLERR));
    defineClassConstant(ioClass, "PollHup", NUMBER_VAL(POLLHUP));

    pop(); // ioClass
    pop(); // ioName
}

Value systemTimeNative(int argCount, Value* args) {
    return NUMBER_VAL((double)time(NULL));
}

Value systemExitNative(int argCount, Value* args) {
    int code = 0;
    if (argCount > 0) {
        if (!IS_NUMBER(args[0])) {
            runtimeError("exit() expects a number argument.");
            return NIL_VAL;
        }

        double dcode = AS_NUMBER(args[0]);
        if (dcode <= -INT_MAX - 1 || dcode > INT_MAX || isnan(dcode)) {
            fprintf(stderr, "exit(): RangeError: exit code is outside integer range.");
            exit(1);
        }
        int rcode = (int)dcode;

        if (rcode < 0 | rcode > 255) {
            code = (unsigned char)dcode;
            fprintf(stderr, "exit() Warning: exit code %d is out of range (0-255).", rcode);
        } else {
            code = rcode;
        }
    }

    exit(code);

    return NIL_VAL;
}

static void setMapField(ObjMap* map, const char* name, double value) {
    ObjString* key = copyString(name, (int)strlen(name));
    push(OBJ_VAL(key));
    tableSet(&map->items, key, NUMBER_VAL(value));
    pop();
}

typedef struct {
    unsigned long size, resident, share, text, lib, data, dt;
} statm_t;

Value systemMemNative(int argCount, Value* args) {
    ObjMap* memmap = newMap();
    push(OBJ_VAL(memmap));

    statm_t res;
    const char* statm_path = "/proc/self/statm";
    FILE *f = fopen(statm_path, "r");
    if (!f) {
        int errsv = errno;
        char* errmsg = strerror(errsv);
        setLastError(errsv, "%s", errmsg);
        runtimeError("Error reading statm: %s\n", errmsg);
        pop();
        return NIL_VAL;
    }

    if (7 != fscanf(f, "%lu %lu %lu %lu %lu %lu %lu",
                &res.size, &res.resident, &res.share, &res.text,
                &res.lib, &res.data, &res.dt)) {
        int errsv = errno;
        char* errmsg = strerror(errsv);
        setLastError(errsv, "%s", errmsg);
        runtimeError("Error parsing statm: %s\n", errmsg);
        fclose(f);
        pop();
        return NIL_VAL;
    }
    fclose(f);

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    setMapField(memmap, "size", (double)res.size * page_size);
    setMapField(memmap, "resident", (double)res.resident * page_size);
    setMapField(memmap, "share", (double)res.share * page_size);
    setMapField(memmap, "text", (double)res.text * page_size);
    setMapField(memmap, "data", (double)res.data * page_size);

    return pop();
}

Value systemResetStackNative(int argCount, Value* args) {
    for (Value* slot = vm.stackTop; slot < vm.stack + STACK_MAX; slot++) {
        *slot = NIL_VAL;
    }
    return NIL_VAL;
}

Value systemShowStackNative(int argCount, Value* args) {
    printf("[SHOW_STACK]: stack: %d\n", (int)(vm.stackTop - vm.stack));
    return NIL_VAL;
}

Value systemSetPrecisionNative(int argCount, Value* args) {
    if (argCount > 0) {
        if (!IS_NUMBER(args[0])) {
            runtimeError("set_precision() expects a number argument.");
            return NIL_VAL;
        }

        int precision = (int)AS_NUMBER(args[0]);
        if (precision < 0) precision = 0;
        if (precision > 20) precision = 20;
        vm.numPrecision = precision;
    }

    return NUMBER_VAL(vm.numPrecision);
}

Value systemSetNotationNative(int argCount, Value* args) {
    if (argCount > 0) {
        if (!IS_NUMBER(args[0])) {
            runtimeError("set_notation() expects a number argument.");
            return NIL_VAL;
        }

        int style = (int)AS_NUMBER(args[0]);

        if (style < 0 || style > 2) {
            runtimeError("Invalid notation style type.");
            return NIL_VAL;
        }

        vm.numNotation = style;
    }

    return NUMBER_VAL(vm.numNotation);
}

Value systemDebugPrintNative(int argCount, Value* args) {
    if (argCount < 0) {
        if (!IS_BOOL(args[0])) {
            runtimeError("debug_print() expects a boolean argument.");
            return NIL_VAL;
        }
        vm.debugPrintCode = AS_BOOL(args[0]);
    }
    return BOOL_VAL(vm.debugPrintCode);
}

Value systemTraceNative(int argCount, Value* args) {
    if (argCount > 0) {
        if (!IS_BOOL(args[0])) {
            runtimeError("trace() expects a boolean argument.");
        }
        vm.debugTraceExecution = AS_BOOL(args[0]);
    }
    return BOOL_VAL(vm.debugTraceExecution);
}

Value systemStrictNative(int argCount, Value* args) {
    if (argCount > 0) {
        if (!IS_BOOL(args[0])) {
            runtimeError("strict() expects a boolean argument.");
            return NIL_VAL;
        }
        vm.strictMode = AS_BOOL(args[0]);
    }
    return BOOL_VAL(vm.strictMode);
}

Value systemWarnNative(int argCount, Value* args) {
    if (argCount > 0) {
        if (!IS_BOOL(args[0])) {
            runtimeError("warn() expects a boolean argument.");
            return NIL_VAL;
        }
        vm.warnMode = AS_BOOL(args[0]);
    }
    return BOOL_VAL(vm.warnMode);
}

Value systemSleepNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) {
        runtimeError("System.sleep() requires a numeric duration in seconds.");
        return NIL_VAL;
    }

    double totalSeconds = AS_NUMBER(args[0]);
    if (totalSeconds < 0) totalSeconds = 0;

    struct timespec ts;
    ts.tv_sec = (time_t)totalSeconds;
    ts.tv_nsec = (long)((totalSeconds - (double)ts.tv_sec) * 1e9);

    nanosleep(&ts, NULL);

    return NIL_VAL;
}

void initSystemLibrary(int argc, const char* argv[], const char* env[]) {
    ObjString* systemName = copyString("System", 6);
    push(OBJ_VAL(systemName));
    ObjClass* systemClass = newClass(systemName);
    push(OBJ_VAL(systemClass));

    defineNativeMethod(systemClass, "time", systemTimeNative);
    defineNativeMethod(systemClass, "exit", systemExitNative);
    defineNativeMethod(systemClass, "gc", systemGCNative);
    defineNativeMethod(systemClass, "mem", systemMemNative);
    defineNativeMethod(systemClass, "reset_stack", systemResetStackNative);
    defineNativeMethod(systemClass, "show_stack", systemShowStackNative);
    defineNativeMethod(systemClass, "set_notation", systemSetNotationNative);
    defineNativeMethod(systemClass, "set_precision", systemSetPrecisionNative);
    defineNativeMethod(systemClass, "debug_print", systemDebugPrintNative);
    defineNativeMethod(systemClass, "trace", systemTraceNative);
    defineNativeMethod(systemClass, "strict", systemStrictNative);
    defineNativeMethod(systemClass, "warn", systemWarnNative);
    defineNativeMethod(systemClass, "sleep", systemSleepNative);

    vm.includePathCount = 0;
    vm.scriptName = NULL;

    ObjArray* argsArray = newArray();
    push(OBJ_VAL(argsArray));

    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-I") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -I option requires a directory path.\n");
                exit(64);
            }
            if (vm.includePathCount < 64) {
                vm.includePaths[vm.includePathCount++] = argv[i + 1];
            }
            i += 2;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            exit(64);
        }
    }

    if (i < argc) {
        vm.scriptName = argv[i];
        i++;
    }

    const char* exeTarget = (vm.scriptName != NULL) ? vm.scriptName : argv[0];

    ObjString* exeKey = copyString("EXE", 3);
    push(OBJ_VAL(exeKey));
    ObjString* exeVal = copyString(exeTarget, strlen(exeTarget));
    push(OBJ_VAL(exeVal));

    tableSet(&systemClass->fields, exeKey, OBJ_VAL(exeVal));
    pop(); // pop exeVal
    pop(); // pop exeKey

    for (int j = i; j < argc; j++) {
        ObjString* argStr = copyString(argv[j], strlen(argv[j]));
        push(OBJ_VAL(argStr));
        arrayAppend(argsArray, OBJ_VAL(argStr));
        pop();
    }
    tableSet(&systemClass->fields, copyString("ARGS", 4), OBJ_VAL(argsArray));

    ObjMap* envMap = newMap();
    push(OBJ_VAL(envMap));

    for (const char **envp = env; *envp != NULL; envp++) {
        const char* entry = *envp;
        char *sep = strchr(entry, '=');

        if (sep != NULL) {
            int keyLen = (int)(sep - entry);
            int valLen = (int)strlen(sep + 1);

            ObjString* key = copyString(entry, keyLen);
            push(OBJ_VAL(key));
            ObjString* val = copyString(sep + 1, valLen);
            push(OBJ_VAL(val));

            tableSet(&envMap->items, key, OBJ_VAL(val));
            pop();
            pop();
        }
    }
    tableSet(&systemClass->fields, copyString("ENV", 3), OBJ_VAL(envMap));

    defineClassConstant(systemClass, "Scientific", NUMBER_VAL(1));
    defineClassConstant(systemClass, "Fixed", NUMBER_VAL(2));
    defineClassConstant(systemClass, "Default", NUMBER_VAL(0));

    tableSet(&vm.globals, systemName, OBJ_VAL(systemClass));

    popn(4);
}
