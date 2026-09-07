#ifndef slox_chunk_h
#define slox_chunk_h

#include "common.h"
#include "value.h"

#define OPCODE_LIST(X) \
    X(OP_CONSTANT) \
    X(OP_CONSTANT_LONG) \
    X(OP_DUP) \
    X(OP_SWAP) \
    X(OP_INSTANCEOF) \
    X(OP_NIL) \
    X(OP_TRUE) \
    X(OP_FALSE) \
    X(OP_POP) \
    X(OP_POPN) \
    X(OP_ARRAY) \
    X(OP_ARRAY_FILL) \
    X(OP_MAP) \
    X(OP_GET_INDEX) \
    X(OP_SET_INDEX) \
    X(OP_GET_LOCAL) \
    X(OP_GET_LOCAL_LONG) \
    X(OP_SET_LOCAL) \
    X(OP_SET_LOCAL_LONG) \
    X(OP_GET_GLOBAL) \
    X(OP_GET_GLOBAL_LONG) \
    X(OP_DEFINE_CLASS_CONST) \
    X(OP_DEFINE_CLASS_CONST_LONG) \
    X(OP_DEFINE_GLOBAL) \
    X(OP_DEFINE_GLOBAL_CONST) \
    X(OP_DEFINE_GLOBAL_LONG) \
    X(OP_DEFINE_GLOBAL_CONST_LONG) \
    X(OP_SET_GLOBAL) \
    X(OP_SET_GLOBAL_LONG) \
    X(OP_GET_UPVALUE) \
    X(OP_SET_UPVALUE) \
    X(OP_GET_SUPER) \
    X(OP_EQUAL) \
    X(OP_GET_PROPERTY) \
    X(OP_GET_PROPERTY_LONG) \
    X(OP_SET_PROPERTY) \
    X(OP_SET_PROPERTY_LONG) \
    X(OP_GREATER) \
    X(OP_LESS) \
    X(OP_ADD) \
    X(OP_SUBTRACT) \
    X(OP_MULTIPLY) \
    X(OP_DIVIDE) \
    X(OP_NOT) \
    X(OP_BITWISE_AND) \
    X(OP_BITWISE_OR) \
    X(OP_BITWISE_NOT) \
    X(OP_SHL) \
    X(OP_SHR) \
    X(OP_POW) \
    X(OP_XOR) \
    X(OP_MOD) \
    X(OP_NEGATE) \
    X(OP_STR) \
    X(OP_PRINT) \
    X(OP_TRY) \
    X(OP_END_TRY) \
    X(OP_THROW) \
    X(OP_JUMP) \
    X(OP_JUMP_IF_NIL) \
    X(OP_JUMP_IF_FALSE) \
    X(OP_JUMP_IF_TRUE) \
    X(OP_LOOP) \
    X(OP_CALL) \
    X(OP_TAIL_CALL) \
    X(OP_INVOKE) \
    X(OP_INVOKE_LONG) \
    X(OP_TAIL_INVOKE) \
    X(OP_TAIL_INVOKE_LONG) \
    X(OP_SUPER_INVOKE) \
    X(OP_SUPER_INVOKE_LONG) \
    X(OP_TAIL_SUPER_INVOKE) \
    X(OP_TAIL_SUPER_INVOKE_LONG) \
    X(OP_CLOSURE) \
    X(OP_CLOSURE_LONG) \
    X(OP_CLOSE_UPVALUE) \
    X(OP_RETURN) \
    X(OP_CLASS) \
    X(OP_CLASS_LONG) \
    X(OP_INHERIT) \
    X(OP_METHOD) \
    X(OP_METHOD_LONG) \
    X(OP_STATIC_METHOD) \
    X(OP_STATIC_METHOD_LONG) \
    X(OP_GETTER) \
    X(OP_GETTER_LONG) \
    X(OP_SETTER) \
    X(OP_SETTER_LONG) \
    X(OP_IMPORT) \
    X(OP_IMPORT_LONG) \
    X(OP_INCLUDE) \
    X(OP_SPLAT) \
    X(OP_CALL_SPLAT) \
    X(OP_INVOKE_SPLAT) \
    X(OP_INVOKE_SPLAT_LONG) \
    X(OP_TAIL_INVOKE_SPLAT) \
    X(OP_TAIL_INVOKE_SPLAT_LONG) \
    X(OP_SUPER_INVOKE_SPLAT) \
    X(OP_SUPER_INVOKE_SPLAT_LONG) \
    X(OP_TAIL_SUPER_INVOKE_SPLAT) \
    X(OP_TAIL_SUPER_INVOKE_SPLAT_LONG) \
    X(OP_UNPACK) \
    X(OP_END_FINALLY)

#define ENUM_ELEMENT(op) op,
typedef enum {
    OPCODE_LIST(ENUM_ELEMENT)
} OpCode;

typedef struct {
    int line;
    int count;
} LineStart;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int lineCount;
    int lineCapacity;
    LineStart* lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
