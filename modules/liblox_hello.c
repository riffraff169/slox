#include <stdio.h>
#include <string.h>

#include "../vm.h"

static Value helloNative(int argCount, Value* args) {
    printf("Hello from a dynamically loaded module!\n");
    return NIL_VAL;
}

void lox_module_init(VM* vm) {
    defineNative("sayHello", helloNative);
}
