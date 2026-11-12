#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include "vm.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

typedef struct {
    unsigned char* pixels;
    int width;
    int height;
    size_t size;
} ImageBuffer;

static ObjClass* imageDataClass = NULL;

static ImageBuffer* image_buffer_for(ObjInstance* instance, const char* action) {
    ImageBuffer* img = (ImageBuffer*)instance->foreignPtr;
    if (img == NULL) {
        runtimeError("Cannot %s: the image buffer is empty or has been freed.", action);
        return NULL;
    }

    if (img->pixels == NULL || img->width <= 0 || img->height <= 0) {
        runtimeError("Cannot %s: the image buffer is invalid.", action);
        return NULL;
    }

    size_t expected = (size_t)img->width * (size_t)img->height * 4;
    if (img->size < expected) {
        runtimeError("Cannot %s: the image buffer is smaller than its dimensions.", action);
        return NULL;
    }

    return img;
}

static bool color_array_to_rgba(ObjArray* color, unsigned char* rgba) {
    if (color->count < 3) {
        runtimeError("Color arrays must contain at least r, g, and b components.");
        return false;
    }

    for (int i = 0; i < 4; i++) {
        if (i >= color->count) {
            rgba[i] = 255;
            continue;
        }

        Value value = color->values[i];
        if (!IS_INT(value)) {
            runtimeError("Color components must be integers from 0 to 255.");
            return false;
        }

        int64_t component = AS_INT(value);
        if (component < 0 || component > 255) {
            runtimeError("Color component %lld is out of range (0-255).", (long long)component);
            return false;
        }
        rgba[i] = (unsigned char)component;
    }

    return true;
}

static Value nxImageClear(int argCount, Value* args) {
    if (argCount < 1 || !IS_ARRAY(args[0])) {
        runtimeError("clear() expects a color array.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    ImageBuffer* img = image_buffer_for(instance, "clear the image");
    if (img == NULL) {
        return NIL_VAL;
    }

    unsigned char rgba[4];
    if (!color_array_to_rgba(AS_ARRAY(args[0]), rgba)) {
        return NIL_VAL;
    }

    size_t total = (size_t)img->width * (size_t)img->height;
    for (size_t i = 0; i < total; i++) {
        img->pixels[i * 4] = rgba[0];
        img->pixels[i * 4 + 1] = rgba[1];
        img->pixels[i * 4 + 2] = rgba[2];
        img->pixels[i * 4 + 3] = rgba[3];
    }

    return NIL_VAL;
}

static Value nxSetPixel(int argCount, Value* args) {
    if (argCount < 3 || !IS_INT(args[0]) || !IS_INT(args[1])
            || !IS_ARRAY(args[2])) {
        runtimeError("set_pixel() expects x, y, and a color array.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    ImageBuffer* img = image_buffer_for(instance, "set a pixel");
    if (img == NULL) {
        return NIL_VAL;
    }

    int64_t raw_x = AS_INT(args[0]);
    int64_t raw_y = AS_INT(args[1]);
    if (raw_x < 0 || raw_y < 0 || raw_x >= img->width || raw_y >= img->height) {
        runtimeError("Pixel coordinates out of bounds.");
        return NIL_VAL;
    }

    unsigned char rgba[4];
    if (!color_array_to_rgba(AS_ARRAY(args[2]), rgba)) {
        return NIL_VAL;
    }

    size_t offset = ((size_t)raw_y * (size_t)img->width + (size_t)raw_x) * 4;
    img->pixels[offset] = rgba[0];
    img->pixels[offset + 1] = rgba[1];
    img->pixels[offset + 2] = rgba[2];
    img->pixels[offset + 3] = rgba[3];

    return NIL_VAL;
}

static Value nxGetPixel(int argCount, Value* args) {
    if (argCount < 2 || !IS_INT(args[0]) || !IS_INT(args[1])) {
        runtimeError("getPixel() expects x and y coordinates.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    ImageBuffer* img = image_buffer_for(instance, "read a pixel");
    if (img == NULL) {
        return NIL_VAL;
    }

    int64_t x = AS_INT(args[0]);
    int64_t y = AS_INT(args[1]);

    if (x < 0 || x >= img->width || y < 0 || y >= img->height) {
        runtimeError("Pixel coordinates out of bounds.");
        return NIL_VAL;
    }

    size_t offset = ((size_t)y * (size_t)img->width + (size_t)x) * 4;

    ObjArray* rgba = newArray();
    push(OBJ_VAL(rgba));
    arrayAppend(rgba, INT_VAL(img->pixels[offset]));      // r
    arrayAppend(rgba, INT_VAL(img->pixels[offset + 1]));  // g
    arrayAppend(rgba, INT_VAL(img->pixels[offset + 2]));  // b
    arrayAppend(rgba, INT_VAL(img->pixels[offset + 3]));  // a

    return pop();
}

static Value nxImageScale(int argCount, Value* args) {
    if (argCount < 2 || !IS_INT(args[0]) || !IS_INT(args[1])) {
        runtimeError("scale() expects (max_width, max_height).");
        return NIL_VAL;
    }

    ObjInstance* imgObj = AS_INSTANCE(args[-1]);
    ImageBuffer* img = image_buffer_for(imgObj, "scale the image");
    if (img == NULL) {
        return NIL_VAL;
    }

    int64_t raw_max_w = AS_INT(args[0]);
    int64_t raw_max_h = AS_INT(args[1]);
    if (raw_max_w <= 0 || raw_max_h <= 0
            || raw_max_w > INT_MAX || raw_max_h > INT_MAX) {
        runtimeError("scale() expects positive maximum dimensions.");
        return NIL_VAL;
    }

    int max_w = (int)raw_max_w;
    int max_h = (int)raw_max_h;
    int w = img->width;
    int h = img->height;

    if (w <= max_w && h <= max_h) {
        return args[-1];
    }

    double scale_w = (double)max_w / (double)w;
    double scale_h = (double)max_h / (double)h;
    double scale = (scale_w < scale_h) ? scale_w : scale_h;

    int new_w = (int)(w * scale);
    int new_h = (int)(h * scale);
    if (new_w < 1) new_w = 1;
    if (new_h < 1) new_h = 1;

    if ((size_t)new_w > SIZE_MAX / 4u / (size_t)new_h) {
        runtimeError("Scaled image dimensions are too large.");
        return NIL_VAL;
    }
    size_t new_size = (size_t)new_w * (size_t)new_h * 4u;

    unsigned char* new_data = (unsigned char*)STBI_MALLOC(new_size);
    if (new_data == NULL) {
        runtimeError("Failed to allocate memory for the scaled image.");
        return NIL_VAL;
    }

    if (stbir_resize_uint8_linear(
                img->pixels, w, h, 0,
                new_data, new_w, new_h, 0,
                STBIR_RGBA) == NULL) {
        stbi_image_free(new_data);
        runtimeError("Failed to scale the image.");
        return NIL_VAL;
    }

    stbi_image_free(img->pixels);
    img->pixels = new_data;
    img->width = new_w;
    img->height = new_h;
    img->size = new_size;

    tableSet(&imgObj->fields, copyString("width", 5), INT_VAL(new_w));
    tableSet(&imgObj->fields, copyString("height", 6), INT_VAL(new_h));

    return args[-1];
}

static Value nxGetRawBytes(int argCount, Value* args) {
    if (!IS_INSTANCE(args[-1])) {
        runtimeError("get_bytes() must be called on an Image.Data instance.");
        return NIL_VAL;
    }

    ObjInstance* imgData = AS_INSTANCE(args[-1]);
    ImageBuffer* img = image_buffer_for(imgData, "read the image bytes");
    if (img == NULL) {
        return NIL_VAL;
    }

    if (img->size > (size_t)INT_MAX) {
        runtimeError("Image buffer is too large to expose as a string.");
        return NIL_VAL;
    }

    ObjString* rawString = copyString((const char*)img->pixels, (int)img->size);
    return OBJ_VAL(rawString);
}

static Value nxImageLoad(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("load() expects a path string.");
        return NIL_VAL;
    }

    const char* path = AS_CSTRING(args[0]);
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);

    if (data == NULL) {
        const char* reason = stbi_failure_reason();
        setLastError(0, "Image load failed for '%s': %s", path,
                reason != NULL ? reason : "unknown error");
        return NIL_VAL;
    }

    if (w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / 4u / (size_t)h) {
        stbi_image_free(data);
        runtimeError("Image dimensions are invalid or too large.");
        return NIL_VAL;
    }

    ImageBuffer* img = (ImageBuffer*)malloc(sizeof(ImageBuffer));
    if (img == NULL) {
        stbi_image_free(data);
        runtimeError("Failed to allocate memory for the loaded image.");
        return NIL_VAL;
    }
    img->pixels = data;
    img->width = w;
    img->height = h;
    img->size = (size_t)w * (size_t)h * 4u;

    ObjInstance* imgData = newInstance(imageDataClass);
    push(OBJ_VAL(imgData));

    imgData->foreignPtr = img;
    tableSet(&imgData->fields, copyString("width", 5), INT_VAL(w));
    tableSet(&imgData->fields, copyString("height", 6), INT_VAL(h));

    return pop();
}

static bool has_extension(const char* filename, const char* ext) {
    const char* dot = strrchr(filename, '.');
    if (dot == NULL || dot == filename) {
        return false;
    }

    const char* slash = strrchr(filename, '/');
    if (slash != NULL && dot < slash) {
        return false;
    }

    const char* actual = dot + 1;
    size_t ext_len = strlen(ext);
    if (strlen(actual) != ext_len) {
        return false;
    }

    for (size_t i = 0; i < ext_len; i++) {
        if (tolower((unsigned char)actual[i]) != tolower((unsigned char)ext[i])) {
            return false;
        }
    }

    return true;
}

static Value image_save_instance(ObjInstance* instance, const char* filename) {
    ImageBuffer* img = image_buffer_for(instance, "save the image");
    if (img == NULL) {
        return NIL_VAL;
    }

    int w = img->width;
    int h = img->height;
    void* data = img->pixels;

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

static Value nxImageSave(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        runtimeError("save() expects a filename string.");
        return NIL_VAL;
    }

    return image_save_instance(AS_INSTANCE(args[-1]), AS_CSTRING(args[0]));
}

static Value nxImageSaveModule(int argCount, Value* args) {
    if (argCount < 2 || !IS_INSTANCE(args[0]) || !IS_STRING(args[1])) {
        runtimeError("save() expects an image and a filename string.");
        return NIL_VAL;
    }

    return image_save_instance(AS_INSTANCE(args[0]), AS_CSTRING(args[1]));
}

void imageDestructor(ObjInstance* instance) {
    if (instance->foreignPtr == NULL) {
        return;
    }

    ImageBuffer* img = (ImageBuffer*)instance->foreignPtr;
    stbi_image_free(img->pixels);
    free(img);
    instance->foreignPtr = NULL;
}

void lox_module_init(VM* vm) {
    ObjInstance* imageModule = newInstance(vm->moduleClass);
    push(OBJ_VAL(imageModule)); // [1] instance

    // 1. setup data class
    ObjString* dataStr = copyString("Data", 4);
    push(OBJ_VAL(dataStr)); // [2] string
    imageDataClass = newClass(dataStr);
    push(OBJ_VAL(imageDataClass)); // [3] class

    imageDataClass->destructor = imageDestructor;
    tableSet(&imageModule->fields, dataStr, OBJ_VAL(imageDataClass));

    // Register Image global
    ObjString* imageStr = copyString("Image", 5);
    push(OBJ_VAL(imageStr)); // [4] string
    tableSet(&vm->globals, imageStr, OBJ_VAL(imageModule));

    pop(); // [4] string
    pop(); // [3] class
    pop(); // [2] string
    pop(); // [1] instance

    // 2 setup load/save on imagemodule
    // load
    ObjNative* loadFn = newNative(nxImageLoad);
    push(OBJ_VAL(loadFn)); // [1] native
    ObjString* loadStr = copyString("load", 4);
    push(OBJ_VAL(loadStr)); // [2] string

    tableSet(&imageModule->fields, loadStr, OBJ_VAL(loadFn));
    pop(); // [2] string
    pop(); // [1] native

    // save (module form: Image.save(image, filename))
    ObjNative* saveModuleFn = newNative(nxImageSaveModule);
    push(OBJ_VAL(saveModuleFn)); // [1] native
    ObjString* saveStr = copyString("save", 4);
    push(OBJ_VAL(saveStr)); // [2] string
    tableSet(&imageModule->fields, saveStr, OBJ_VAL(saveModuleFn));
    pop(); // [2] string
    pop(); // [1] native

    // get_pixel
    ObjNative* getPixelFn = newNative(nxGetPixel);
    push(OBJ_VAL(getPixelFn));
    tableSet(&imageDataClass->methods, copyString("get_pixel", 9), OBJ_VAL(getPixelFn));
    pop();

    ObjNative* setPixelFn = newNative(nxSetPixel);
    push(OBJ_VAL(setPixelFn));
    tableSet(&imageDataClass->methods, copyString("set_pixel", 9), OBJ_VAL(setPixelFn));
    pop();

    ObjNative* getRawBytesFn = newNative(nxGetRawBytes);
    push(OBJ_VAL(getRawBytesFn));
    tableSet(&imageDataClass->methods, copyString("get_bytes", 9), OBJ_VAL(getRawBytesFn));
    pop();

    ObjNative* scaleFn = newNative(nxImageScale);
    push(OBJ_VAL(scaleFn));
    tableSet(&imageDataClass->methods, copyString("scale", 5), OBJ_VAL(scaleFn));
    pop();

    ObjNative* saveFn = newNative(nxImageSave);
    push(OBJ_VAL(saveFn));
    tableSet(&imageDataClass->methods, copyString("save", 4), OBJ_VAL(saveFn));
    pop();

    ObjNative* clearFn = newNative(nxImageClear);
    push(OBJ_VAL(clearFn));
    tableSet(&imageDataClass->methods, copyString("clear", 5), OBJ_VAL(clearFn));
    pop();
}
