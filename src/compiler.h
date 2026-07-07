#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction* compile(const char* source, ObjString* filename);
ObjFunction* compileModule(const char* source, ObjString* filename);
bool compileClassModule(const char* source, ObjClass* klass);
void markCompilerRoots();
char* readFile(const char* path);

#endif
