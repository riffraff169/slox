#include <stdio.h>
#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lineCount = 0;
    chunk->lineCapacity = 0; 
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(LineStart, chunk->lines, chunk->lineCapacity);
    freeValueArray(&chunk->constants);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code,
                oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;

    if (chunk->lineCount > 0 &&
            chunk->lines[chunk->lineCount - 1].line == line) {
        chunk->lines[chunk->lineCount - 1].count++;
    } else {
        if (chunk->lineCapacity < chunk->lineCount + 1) {
            int oldCapacity = chunk->lineCapacity;
            chunk->lineCapacity = GROW_CAPACITY(oldCapacity);
            chunk->lines = GROW_ARRAY(LineStart, chunk->lines,
                    oldCapacity, chunk->lineCapacity);
        }
        chunk->lines[chunk->lineCount].line = line;
        chunk->lines[chunk->lineCount].count = 1;
        chunk->lineCount++;
    }
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value) {
    push(value);
    writeValueArray(&chunk->constants, value);
    pop();
    return chunk->constants.count - 1;
}

