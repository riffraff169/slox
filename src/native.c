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

void defineNativeClassConstant(ObjClass* klass, const char* name, Value value) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    tableSet(&klass->constants, AS_STRING(peek(0)), value);
    pop();
}

Value clockNative(int argCount, Value* args) {
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

void initGlobalNatives() {
    defineNative("clock", clockNative);
    defineNative("str", strNative);
    defineNative("typeof", typeofNative);
    defineNative("isnumber", isNumberNative);
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
    X("acos", mathAcosNative)

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
    ObjClosure* callback = AS_CLOSURE(args[0]);

    ObjArray* result = newArray();
    push(OBJ_VAL(result));

    VM_CALLBACK_INIT();

    for (int i = 0; i < original->count; i++) {
        push(OBJ_VAL(callback));
        push(original->values[i]);

        VM_CALLBACK_ENTER();

        if (vmCall(callback, 1)) {
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

            if (!isFalsey(pop())) {
                arrayAppend(result, original->values[i]);
            }
        }

        VM_CALLBACK_RESET_STACK();
    }

    VM_CALLBACK_EXIT();

    return pop();
}

Value arrayReduceNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_CLOSURE(args[0])) {
        return NIL_VAL;
    }

    ObjArray* array = AS_ARRAY(args[-1]);
    Value callback = args[0];

    Value acc = (argCount > 1) ? args[1] : NIL_VAL;
    int startindex = (argCount > 1) ? 0 : 1;
    if (argCount <= 1 && array->count > 0) {
        acc = array->values[0];
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

        ObjString* s = valueToString(item);
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
