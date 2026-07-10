#ifndef NATIVE_H
#define NATIVE_H

/* Injected dependencies from native_includes.h.in */
#include "value.h"
#include "object.h"
//#include "table.h"
//#include "vm.h"

// Automatically extracted signatures
void defineNativeClassConstant(ObjClass* klass, const char* name, Value value);
Value clockNative(int argCount, Value* args);
Value strNative(int argCount, Value* args);
Value typeofNative(int argCount, Value* args);
Value chrNative(int argCount, Value* args);
Value evalNative(int argCount, Value* args);
Value createInstanceNative(int argCount, Value* args);
Value programNative(int argCount, Value* args);
Value isNumberNative(int argCount, Value* args);
Value isStringNative(int argCount, Value* args);
Value isBoolNative(int argCount, Value* args);
Value isNilNative(int argCount, Value* args);
Value isClassNative(int argCount, Value* args);
Value isInstanceNative(int argCount, Value* args);
Value listFieldsNative(int argCount, Value* args);
Value getFieldNative(int argCount, Value* args);
Value setFieldNative(int argCount, Value* args);
Value getMethodsNative(int argCount, Value* args);
Value hasMethodNative(int argCount, Value* args);
Value getSuperclassNative(int argCount, Value* args);
Value objectToStringNative(int argCount, Value* args);
Value objectClassMethod(int argCount, Value* args);
Value objectClassNameMethod(int argCount, Value* args);
void initCoreLibrary();
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
Value resultNativeConstructor(int argCount, Value* args);
Value resultUnwrapNative(int argCount, Value* args);
Value resultUnwrapOrNative(int argCount, Value* args);
Value optionNativeConstructor(int argCount, Value* args);
Value optionUnwrapNative(int argCount, Value* args);
Value optionUnwrapOrNative(int argCount, Value* args);
void initResultAndOptionClass();
Value regexNativeConstructor(int argCount, Value* args);
Value regexTestNative(int argCount, Value* args);
Value regexMatchNative(int argCount, Value* args);
Value regexGetPatternNative(int argCount, Value* args);
void regexDestructor(ObjInstance* inst);
void initRegexClass();

#endif // NATIVE_H
