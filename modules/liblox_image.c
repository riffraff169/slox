#define STB_IMAGE_IMPLEMENTATION
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../vm.h"
#include "external/stb/stb_image.h"
#include "external/stb/stb_image_write.h"

static ObjClass* imageDataClass = NULL;

static Value nxImageClear(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[0]);
    ObjArray* color = AS_ARRAY(args[1]);
    unsigned char* pixels = (unsigned char*)instance->foreignPtr;

    Value wval, hval;
    tableGet(&instance->fields, copyString("width", 5), &wval);
    tableGet(&instance->fields, copyString("height", 6), &hval);
    int total = (int)AS_NUMBER(wval) * (int)AS_NUMBER(hval);

    unsigned char r = (unsigned char)AS_NUMBER(color->values[0]);
    unsigned char g = (unsigned char)AS_NUMBER(color->values[1]);
    unsigned char b = (unsigned char)AS_NUMBER(color->values[2]);
    unsigned char a = (color->count > 3) ? (unsigned char)AS_NUMBER(color->values[3]) : 255;

    for (int i = 0; i < total; i++) {
        pixels[i * 4] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
    return NIL_VAL;
}

static Value nxSetPixel(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])
            || !IS_ARRAY(args[3])) {
        runtimeError("set_pixel() expects x, y, and a color array.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    unsigned char* pixels = (unsigned char*)instance->foreignPtr;

    Value wval, hval;
    tableGet(&instance->fields, copyString("width", 5), &wval);

    int width = (int)AS_NUMBER(wval);
    int x = (int)AS_NUMBER(args[1]);
    int y = (int)AS_NUMBER(args[2]);
    ObjArray* color = AS_ARRAY(args[3]);

    if (x < 0 || x >= width || y < 0) {
        return NIL_VAL;
    }

    int offset = (y * width + x) * 4;

    pixels[offset] = (unsigned char)AS_NUMBER(color->values[0]);
    pixels[offset + 1] = (unsigned char)AS_NUMBER(color->values[1]);
    pixels[offset + 2] = (unsigned char)AS_NUMBER(color->values[2]);

    if (color->count > 3) {
        pixels[offset + 3] = (unsigned char)AS_NUMBER(color->values[3]);
    } else {
        pixels[offset + 3] = 255;
    }

    return NIL_VAL;
}

static Value nxGetPixel(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        runtimeError("getPixel() expects x and y coordinates.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    unsigned char* pixels = (unsigned char*) instance->foreignPtr;

    Value wval, hval;
    tableGet(&instance->fields, copyString("width", 5), &wval);
    tableGet(&instance->fields, copyString("height", 6), &hval);

    int width = (int)AS_NUMBER(wval);
    int height = (int)AS_NUMBER(hval);
    int x = (int)AS_NUMBER(args[1]);
    int y = (int)AS_NUMBER(args[2]);

    if (x < 0 || x >= width || y < 0 || y >= height) {
        runtimeError("Pixel coordinates out of bounds.");
        return NIL_VAL;
    }

    int offset = (y * width + x) * 4;

    ObjArray* rgba = newArray(0);
    push(OBJ_VAL(rgba));
    arrayAppend(rgba, NUMBER_VAL(pixels[offset]));      // r
    arrayAppend(rgba, NUMBER_VAL(pixels[offset + 1]));  // g
    arrayAppend(rgba, NUMBER_VAL(pixels[offset + 2]));  // b
    arrayAppend(rgba, NUMBER_VAL(pixels[offset + 3]));  // a

    return pop();
}

static Value nxImageLoad(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("load() expects a path string.");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);
    int w, h, ch;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);

    if (!data) {
        printf("Data load failed: %s\n", stbi_failure_reason());
        return NIL_VAL;
    }

    ObjInstance* imgData = newInstance(imageDataClass);
    push(OBJ_VAL(imgData));

    imgData->foreignPtr = data;
    tableSet(&imgData->fields, copyString("width", 5), NUMBER_VAL(w));
    tableSet(&imgData->fields, copyString("height", 6), NUMBER_VAL(h));

    return pop();
}

static bool has_extension(const char* filename, const char* ext) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) return false;

    for (int i = 0; ext[i]; i++) {
        if (tolower(dot[1 + i]) != tolower(ext[i])) return false;
    }
    return true;
}

static Value nxImageSave(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[1])) {
        runtimeError("save() expects a filename string.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[0]);
    const char* filename = AS_CSTRING(args[1]);

    Value wval, hval;
    tableGet(&instance->fields, copyString("width", 5), &wval);
    tableGet(&instance->fields, copyString("height", 6), &hval);


    int w = (int)AS_NUMBER(wval);
    int h = (int)AS_NUMBER(hval);
    void* data = instance->foreignPtr;

    int result = 0;

    if (has_extension(filename, "jpg") || has_extension(filename, "jpeg")) {
        result = stbi_write_jpg(filename, w, h, 4, data, 90);
    } else if (has_extension(filename, "bmp")) {
        result = stbi_write_bmp(filename, w, h, 4, data);
    } else if (has_extension(filename, "tga")) {
        result = stbi_write_tga(filename, w, h, 4, data);
    } else {
        result = stbi_write_png(filename, w, h, 4, data, w * 4);
    }

    if (result == 0) {
        runtimeError("Failed to save image to %s", filename);
    }

    return BOOL_VAL(result != 0);
}

void imageDestructor(ObjInstance* instance) {
    if (instance->foreignPtr != NULL) {
        stbi_image_free(instance->foreignPtr);
        instance->foreignPtr = NULL;
    }
}

void lox_module_init(VM* vm) {
    ObjInstance* imageModule = newInstance(vm->moduleClass);
    push(OBJ_VAL(imageModule)); // [1] instance

    ObjString* dataStr = copyString("Data", 4);
    push(OBJ_VAL(dataStr)); // [2] string

    imageDataClass = newClass(dataStr);
    push(OBJ_VAL(imageDataClass)); // [3] class
    imageDataClass->destructor = imageDestructor;
    tableSet(&imageModule->fields, dataStr, OBJ_VAL(imageDataClass));
    ObjString* imageStr = copyString("Image", 5);
    push(OBJ_VAL(imageStr)); // [4] string
    tableSet(&vm->globals, imageStr, OBJ_VAL(imageModule));
    pop(); // [4] string
    pop(); // [3] class
    pop(); // [2] string
    pop(); // [1] instance

    ObjNative* loadFn = newNative(nxImageLoad);
    push(OBJ_VAL(loadFn)); // [1] native
    ObjString* loadStr = copyString("load", 4);
    push(OBJ_VAL(loadStr)); // [2] string

    tableSet(&imageModule->fields, loadStr, OBJ_VAL(loadFn));
    pop(); // [2] string
    pop(); // [1] native

    ObjNative* saveFn = newNative(nxImageSave);
    push(OBJ_VAL(saveFn)); // [1] native
    ObjString* saveStr = copyString("save", 4);
    push(OBJ_VAL(saveStr)); // [2] string
    tableSet(&imageModule->fields, saveStr, OBJ_VAL(saveFn));

    ObjNative* getPixelFn = newNative(nxGetPixel);
    push(OBJ_VAL(getPixelFn));
    tableSet(&imageDataClass->methods, copyString("get_pixel", 9), OBJ_VAL(getPixelFn));
    pop();

    ObjNative* setPixelFn = newNative(nxSetPixel);
    push(OBJ_VAL(setPixelFn));
    tableSet(&imageDataClass->methods, copyString("set_pixel", 9), OBJ_VAL(setPixelFn));
    pop();

    pop(); // [2] string
    pop(); // [1] instance
}

