#ifndef NATIVE_H
#define NATIVE_H

/* Injected dependencies from native_includes.h.in */
#include "value.h"
#include "object.h"
//#include "table.h"
//#include "vm.h"

// Automatically extracted signatures
void defineNativeClassConstant(ObjClass* klass, const char* name, Value value);
Value stringTrimNative(int argCount, Value* args);
Value stringContainsNative(int argCount, Value* args);
Value stringFindNative(int argCount, Value* args);
Value stringToUpperNative(int argCount, Value* args);
Value stringToLowerNative(int argCount, Value* args);
Value stringLenNative(int argCount, Value* args);
Value stringSplitNative(int argCount, Value* args);
Value stringSliceNative(int argCount, Value* args);
Value stringToarrayNative(int argCount, Value* args);
Value stringTokensNative(int argCount, Value* args);
Value stringFormatNative(int argCount, Value* args);
void initStringClass();
Value mapNativeConstructor(int argCount, Value* args);
Value mapKeysNative(int argCount, Value* args);
Value mapValuesNative(int argCount, Value* args);
Value mapHasNative(int argCount, Value* args);
Value mapRemoveNative(int argCount, Value* args);
Value mapLenNative(int argCount, Value* args);
void initMapClass();
Value mathSqrtNative(int argCount, Value* args);
Value mathAbsNative(int argCount, Value* args);
Value mathFloorNative(int argCount, Value* args);
Value mathCeilNative(int argCount, Value* args);
Value mathRandomNative(int argCount, Value* args);
Value mathExpNative(int argCount, Value* args);
Value hexNative(int argCount, Value* args);
Value octNative(int argCount, Value* args);
Value binNative(int argCount, Value* args);
Value bitTestNative(int argCount, Value* args);
Value mathMinNative(int argCount, Value* args);
Value mathMaxNative(int argCount, Value* args);
Value mathParseNative(int argCount, Value* args);
Value fromHexNative(int argCount, Value* args);
Value fromBinNative(int argCount, Value* args);
Value mathRoundNative(int argCount, Value* args);
Value toNumberNative(int argCount, Value* args);
Value mathSinNative(int argCount, Value* args);
Value mathTanNative(int argCount, Value* args);
Value mathAtan2Native(int argCount, Value* args);
Value mathCosNative(int argCount, Value* args);
Value mathAcosNative(int argCount, Value* args);
void initMathLibrary();
Value arrayPushNative(int argCount, Value* args);
Value arrayPopNative(int argCount, Value* args);
Value arrayLenNative(int argCount, Value* args);
Value arrayMapNative(int argCount, Value* args);
Value arrayDupNative(int argCount, Value* args);
Value arrayIsEmptyNative(int argCount, Value* args);
Value arrayFilterNative(int argCount, Value* args);
Value arrayReduceNative(int argCount, Value* args);
Value arrayJoinNative(int argCount, Value* args);
Value arrayEachNative(int argCount, Value* args);
Value arrayFindNative(int argCount, Value* args);
Value arrayHasNative(int argCount, Value* args);
Value arraySliceNative(int argCount, Value* args);
Value arraySortNative(int argCount, Value* args);
Value arraySortSliceNative(int argCount, Value* args);
Value arrayReverseNative(int argCount, Value* args);
Value arrayFlattenNative(int argCount, Value* args);
Value arrayStringNative(int argCount, Value* args);
Value arrayFirstNative(int argCount, Value* args);
Value arrayRestNative(int argCount, Value* args);
Value arraySplitNative(int argCount, Value* args);
void debugPrintArrayMethods();
Value arrayNativeConstructor(int argCount, Value* args);
void initArrayClass();

#endif // NATIVE_H
