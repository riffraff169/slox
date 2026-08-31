#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#include "debug.h"

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_TERNARY,
    PREC_OR,
    PREC_NULLISH,
    PREC_XOR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_SHIFT,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_EXP,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
    bool isConst;
} Local;

typedef struct {
    uint16_t index;
    bool isLocal;
    bool isConst;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_STATIC_METHOD,
    TYPE_GETTER,
    TYPE_SETTER,
    TYPE_SCRIPT,
    TYPE_MODULE
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;

    Local locals[8192];
    int localCount;
    Upvalue upvalues[8192];
    int scopeDepth;

    bool inTailPosition;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool hasSuperclass;
} ClassCompiler;

typedef struct Loop {
    struct Loop* enclosing;
    int scopeDepth;
    int continueTarget;
    int firstLocalSlot;
    int* breakJumps;
    int breakCount;
} Loop;

Loop* currentLoop = NULL;

typedef struct {
    int totalSlots;
    //int splatAt;
    bool hasSplat;
} ArgResult;

Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;

static Chunk* currentChunk() {
    return &current->function->chunk;;
}

static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void emitPops(uint8_t count) {
    if (count == 1) {
        emitByte(OP_POP);
    } else if (count > 1) {
        emitBytes(OP_POPN, count);
    }
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void emitReturn() {
    if (current->type == TYPE_INITIALIZER || current->type == TYPE_MODULE) {
        emitBytes(OP_GET_LOCAL, 0);
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

static int makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    //if (constant > UINT8_MAX) {
    if (constant > 16777214) { // 2^24 - 1
        error("Too many constants in one chunk.");
        return 0;
    }

    return (int)constant;
}

static void emitGetHelper(uint8_t getOp, int nameArg) {
    if (nameArg > 255) {
        emitByte(getOp);
        emitByte((uint8_t)((nameArg >> 16) & 0xff));
        emitByte((uint8_t)((nameArg >> 8) & 0xff));
        emitByte((uint8_t)(nameArg & 0xff));
    } else {
        emitBytes(getOp, (uint8_t)nameArg);
    }
}

static void emitSetHelper(uint8_t setOp, int nameArg) {
    if (nameArg > 255) {
        emitByte(setOp);
        emitByte((uint8_t)((nameArg >> 16) & 0xff));
        emitByte((uint8_t)((nameArg >> 8) & 0xff));
        emitByte((uint8_t)(nameArg & 0xff));
    } else {
        emitBytes(setOp, (uint8_t)nameArg);
    }
}

static void emitGetProp(int nameArg) {
    if (nameArg > 255) {
        emitByte(OP_GET_PROPERTY_LONG);
        emitByte((uint8_t)((nameArg >> 16) & 0xff));
        emitByte((uint8_t)((nameArg >> 8) & 0xff));
        emitByte((uint8_t)(nameArg & 0xff));
    } else {
        emitBytes(OP_GET_PROPERTY, (uint8_t)nameArg);
    }
}

static void emitSetProp(int nameArg) {
    if (nameArg > 255) {
        emitByte(OP_SET_PROPERTY_LONG);
        emitByte((uint8_t)((nameArg >> 16) & 0xff));
        emitByte((uint8_t)((nameArg >> 8) & 0xff));
        emitByte((uint8_t)(nameArg & 0xff));
    } else {
        emitBytes(OP_SET_PROPERTY, (uint8_t)nameArg);
    }
}


static void emitInvokeHelper(int nameArg, uint8_t argCount) {
    if (nameArg > 255) {
        emitByte(OP_INVOKE_LONG);
        emitByte((uint8_t)((nameArg >> 16) & 0xff));
        emitByte((uint8_t)((nameArg >> 8) & 0xff));
        emitByte((uint8_t)(nameArg & 0xff));
        emitByte(argCount);
    } else {
        emitBytes(OP_INVOKE, (uint8_t)nameArg);
        emitByte(argCount);
    }
}

static void emitGetVar(uint8_t getOp, int arg) {
    if (getOp == OP_GET_GLOBAL_LONG || getOp == OP_GET_LOCAL_LONG) {
        emitByte(getOp);
        emitByte((uint8_t)((arg >> 16) & 0xff));
        emitByte((uint8_t)((arg >> 8) & 0xff));
        emitByte((uint8_t)(arg & 0xff));
    } else if (getOp == OP_GET_UPVALUE) {
        emitByte(getOp);
        emitByte((uint8_t)((arg >> 8) & 0xff));
        emitByte((uint8_t)(arg & 0xff));
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void emitSetVar(uint8_t setOp, int arg) {
    if (setOp == OP_SET_GLOBAL_LONG || setOp == OP_SET_LOCAL_LONG) {
        emitByte(setOp);
        emitByte((uint8_t)((arg >> 16) & 0xff));
        emitByte((uint8_t)((arg >> 8) & 0xff));
        emitByte((uint8_t)(arg & 0xff));
    } else if (setOp == OP_SET_UPVALUE) {
        emitByte(setOp);
        emitByte((uint8_t)((arg >> 8) & 0xff));
        emitByte((uint8_t)(arg & 0xff));
    } else {
        emitBytes(setOp, (uint8_t)arg);
    }
}

static void emitConstant(Value value) {
    int constant = makeConstant(value);

    if (constant < 256) {
        emitBytes(OP_CONSTANT, (uint8_t)constant);
    } else {
        emitByte(OP_CONSTANT_LONG);
        emitByte((uint8_t)((constant >> 16) & 0xff));
        emitByte((uint8_t)((constant >> 8) & 0xff));
        emitByte((uint8_t)(constant & 0xff));
    }
}

static void patchJumpInChunk(Chunk* chunk, int offset) {
    int jump = chunk->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    chunk->code[offset] = (jump >> 8) & 0xff;
    chunk->code[offset + 1] = jump & 0xff;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();
    compiler->inTailPosition = false;

    if (current != NULL) {
        compiler->function->filename = current->function->filename;
    } else {
        //compiler->function->filename = "
    }

    current = compiler;
    if (type != TYPE_SCRIPT) {
        if (parser.previous.type == TOKEN_IDENTIFIER) {
            current->function->name = copyString(parser.previous.start,
                    parser.previous.length);
        } else {
            current->function->name = copyString("<anonymous>", 11);
        }
    }

    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    if (type != TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

/*
// resolves the ultimate target address if target instruct is an unconditional jump
static int resolveJumpTarget(Chunk* chunk, int target) {
    // chain resolution limit prevents infinite loops from self-referential jumps
    int depth = 0;
    while (target < chunk->count && depth < 16) {
        if (chunk->code[target] == OP_JUMP) {
            uint16_t offset = (chunk->code[target + 1] << 8) | chunk->code[target + 2];
            target = target + 3 + offset; // follow jump to next location
            depth++;
        } else {
            break;
        }
    }
    return target;

}
*/
/*
void optimizeJumps(Chunk* chunk) {
    int i = 0;
    while (i < chunk->count) {
        uint8_t op = chunk->code[i];

        if (op == OP_JUMP || op == OP_JUMP_IF_FALSE) {
            int currentTarget = i + 3 + ((chunk->code[i + 1] << 8) | chunk->code[i + 2]);
            int finalTarget = resolveJumpTarget(chunk, currentTarget);
            if (finalTarget != currentTarget) {
                int newOffset = finalTarget - i - 3;
                if (newOffset <= UINT16_MAX) {
                    chunk->code[i + 1] = (newOffset >> 8) & 0xff;
                    chunk->code[i + 2] = newOffset & 0xff;
                }
            }
            i += 3; // advance past OP_JUMP + 2 byte operand
        } else {
            // advance offset by instruct length
            i += getInstructLength(chunk->code, i);
        }
    }
}
*/

static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;

    if (vm.debugPrintCode) {
        if (!parser.hadError) {
            disassembleChunk(currentChunk(), function->name != NULL
                    ? function->name->chars : "<script>");
        }
    }

    current = current->enclosing;
    return function;
}

static void beginScope() {
    current->scopeDepth++;
}

static void endScope() {
    current->scopeDepth--;
    uint8_t popCount = 0;

    int n = 0;
    while (current->localCount > 0 &&
            current->locals[current->localCount - 1].depth >
            current->scopeDepth) {
        if (current->locals[current->localCount -1].isCaptured) {
            if (popCount > 0) {
                popCount = 0;
            }
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
            //popCount++;
        }
        current->localCount--;
    }
    
    /*
    if (popCount > 0) {
        emitPops(popCount);
    }
    */
    /*
    if (n > 1) {
        emitBytes(OP_POPN, n);
    } else if (n == 1) {
        emitByte(OP_POP);
    }
    */ 
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static int identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start,
                    name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

static int addUpvalue(Compiler* compiler, int index,
        bool isLocal, bool isConst) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == 16777215) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    compiler->upvalues[upvalueCount].isConst = isConst;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (int)local, true, compiler->enclosing->locals[local].isConst);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (int)upvalue, false, compiler->enclosing->upvalues[upvalue].isConst);
    }

    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
    local->isConst = false;
}

static void declareVariable() {
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static void emitInvoke(const char* name, int argCount) {
    Token token = { TOKEN_IDENTIFIER, name, (int)strlen(name), parser.previous.line };
    int nameArg = identifierConstant(&token);

    emitInvokeHelper(nameArg, argCount);
}

static int parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void markInitialized() {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth =
        current->scopeDepth;
}

static void defineVariableExt(int global, bool isConst) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    if (global < 256) {
        if (!isConst) {
            emitBytes(OP_DEFINE_GLOBAL, (uint8_t)global);
        } else {
            emitBytes(OP_DEFINE_GLOBAL_CONST, (uint8_t)global);
        }
    } else {
        if (!isConst) {
            emitByte(OP_DEFINE_GLOBAL_LONG);
        } else {
            emitByte(OP_DEFINE_GLOBAL_CONST_LONG);
        }
        emitByte((uint8_t)((global >> 16) & 0xff));
        emitByte((uint8_t)((global >> 8) & 0xff));
        emitByte((uint8_t)(global & 0xff));
    }
}

static void defineVariable(int global) {
    defineVariableExt(global, false);
}

static ArgResult argumentList() {
    ArgResult result = {0, false};

    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            bool wasTail = current->inTailPosition;
            current->inTailPosition = false; // arguments are not in tail position

            if (match(TOKEN_STAR)) {
                result.hasSplat = true;
                expression();
                emitByte(OP_SPLAT);
            } else {
                expression();
                result.totalSlots++;
            }

            current->inTailPosition = wasTail;

            if (result.totalSlots >= 255) {
                error("Can't have more than 255 arguments.");
            }
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");

    return result;
}

static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static bool checkConstantPair(Chunk* chunk, int len1, int len2, Value* outVal1, Value* outVal2) {
    int totalLen = len1 + len2;
    if (chunk->count < totalLen) return false;

    int op1Start = chunk->count - totalLen;
    int op2Start = chunk->count - len2;

    uint8_t op1Code = (len1 == 2) ? OP_CONSTANT : OP_CONSTANT_LONG;
    uint8_t op2Code = (len2 == 2) ? OP_CONSTANT : OP_CONSTANT_LONG;

    if (chunk->code[op1Start] != op1Code || chunk->code[op2Start] != op2Code) {
        return false;
    }

    uint32_t index1 = (len1 == 2)
        ? chunk->code[op1Start + 1]
        : (chunk->code[op1Start + 1] << 16) | (chunk->code[op1Start + 2] << 8) | chunk->code[op1Start + 3];

    uint32_t index2 = (len2 == 2)
        ? chunk->code[op2Start + 1]
        : (chunk->code[op2Start + 1] << 16) | (chunk->code[op2Start + 2] << 8) | chunk->code[op2Start + 3];

    if (index1 >= (uint32_t)chunk->constants.count || index2 >= (uint32_t)chunk->constants.count) {
        return false;
    }

    *outVal1 = chunk->constants.values[index1];
    *outVal2 = chunk->constants.values[index2];
    return true;
}

static bool tryFoldBinary(TokenType operatorType) {
    Chunk* chunk = currentChunk();
    Value val1, val2;
    int totalLen = 0;

    // evaluate instruction pair variants in order of size
    if (checkConstantPair(chunk, 4, 4, &val1, &val2)) totalLen = 8;
    else if (checkConstantPair(chunk, 4, 2, &val1, &val2)) totalLen = 6;
    else if (checkConstantPair(chunk, 2, 4, &val1, &val2)) totalLen = 6;
    else if (checkConstantPair(chunk, 2, 2, &val1, &val2)) totalLen = 4;
    else return false;

    // must have at least 4 bytes emitted: [OP_CONSTANT] [index1] [OP_CONSTANT] [index2]
    /*
    if (chunk->count < 4) return false;

    uint8_t op1 = chunk->code[chunk->count - 4];
    uint8_t op2 = chunk->code[chunk->count - 2];

    // verify both operands were emitted as OP_CONSTANT
    if (op1 != OP_CONSTANT || op2 != OP_CONSTANT) return false;

    uint8_t index1 = chunk->code[chunk->count - 3];
    uint8_t index2 = chunk->code[chunk->count - 1];

    Value val1 = chunk->constants.values[index1];
    Value val2 = chunk->constants.values[index2];
    */

    // ensure both constants are numbers
    if (!IS_NUMBER(val1) || !IS_NUMBER(val2)) return false;

    double a = AS_NUMBER(val1);
    double b = AS_NUMBER(val2);
    double result;

    switch (operatorType) {
        case TOKEN_PLUS: result  = a + b; break;
        case TOKEN_MINUS: result = a - b; break;
        case TOKEN_STAR: result = a * b; break;
        case TOKEN_SLASH:
                         if (b == 0.0) return false; // leave div by zero for runtime handling
                         result = a / b;
                         break;
        default: return false;
    }

    // rewind bytecode emitter by 4 bytes (strips both OP_CONSTANT byte pairs)
    chunk->count -= totalLen;

    // emit the single folded constant
    emitConstant(NUMBER_VAL(result));
    //emitBytes(OP_CONSTANT, makeConstant(NUMBER_VAL(result)));
    return true;
}

static bool tryFoldUnary(TokenType operatorType) {
    Chunk* chunk = currentChunk();
    Value operand;
    int len = 0;
    uint32_t index = 0;

    //if (chunk->count < 2) return false;
    
    if (chunk->count >= 4 && chunk->code[chunk->count - 4] == OP_CONSTANT_LONG) {
        len = 4;
        index = (chunk->code[chunk->count - 3] << 16) |
            (chunk->code[chunk->count - 2] << 8) |
            chunk->code[chunk->count - 1];
    } else if (chunk->count >= 2 && chunk->code[chunk->count - 2] == OP_CONSTANT) {
        len = 2;
        index = chunk->code[chunk->count - 1];
    } else {
        return false;
    }

    /*
    uint8_t op = chunk->code[chunk->count - 2];
    if (op != OP_CONSTANT) return false;

    uint8_t index = chunk->code[chunk->count - 1];
    Value operand = chunk->constants.values[index];
    */
    if (index >= (uint32_t)chunk->constants.count) return false;
    operand = chunk->constants.values[index];

    if (operatorType == TOKEN_MINUS && IS_NUMBER(operand)) {
        chunk->count -= len;
        emitConstant(NUMBER_VAL(-AS_NUMBER(operand)));
        //emitBytes(OP_CONSTANT, makeConstant(NUMBER_VAL(-AS_NUMBER(operand))));
        return true;
    }

    if (operatorType == TOKEN_BANG && IS_BOOL(operand)) {
        chunk->count -= len;
        emitConstant(BOOL_VAL(!AS_BOOL(operand)));
        //emitBytes(OP_CONSTANT, makeConstant(BOOL_VAL(!AS_BOOL(operand))));
        return true;
    }
    return false;
}

static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);

    bool wasTail = current->inTailPosition;
    current->inTailPosition = false;

    int precedence = (operatorType == TOKEN_STAR_STAR)
        ? rule->precedence
        : (rule->precedence + 1);
    parsePrecedence((Precedence)precedence);

    current->inTailPosition = wasTail;

    if (tryFoldBinary(operatorType)) return;

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:
            emitBytes(OP_EQUAL, OP_NOT);
            break;
        case TOKEN_EQUAL_EQUAL:
            emitByte(OP_EQUAL);
            break;
        case TOKEN_GREATER:
            emitByte(OP_GREATER);
            break;
        case TOKEN_GREATER_EQUAL:
            emitBytes(OP_LESS, OP_NOT);
            break;
        case TOKEN_2RIGHT:
            emitByte(OP_SHR);
            break;
        case TOKEN_LESS:
            emitByte(OP_LESS);
            break;
        case TOKEN_LESS_EQUAL:
            emitBytes(OP_GREATER, OP_NOT);
            break;
        case TOKEN_2LEFT:
            emitByte(OP_SHL);
            break;
        case TOKEN_PLUS:
            emitByte(OP_ADD);
            break;
        case TOKEN_MINUS:
            emitByte(OP_SUBTRACT);
            break;
        case TOKEN_STAR:
            emitByte(OP_MULTIPLY);
            break;
        case TOKEN_STAR_STAR:
            emitByte(OP_POW);
            break;
        case TOKEN_SLASH:
            emitByte(OP_DIVIDE);
            break;
        case TOKEN_CARET:
            emitByte(OP_XOR);
            break;
        case TOKEN_PERCENT:
            emitByte(OP_MOD);
            break;
        case TOKEN_AMPERSAND:
            emitByte(OP_BITWISE_AND);
            break;
        case TOKEN_PIPE:
            emitByte(OP_BITWISE_OR);
        default:
            return;
    }
}

static void call(bool canAssign) {
    // check if this call is in tail position before parsing argument expresions
    bool isTail = current->inTailPosition;

    ArgResult args = argumentList();

    isTail = isTail && check(TOKEN_SEMICOLON);

    if (args.hasSplat) {
        emitBytes(OP_CALL_SPLAT, (uint8_t)args.totalSlots);
    } else if (isTail) {
        emitBytes(OP_TAIL_CALL, (uint8_t)args.totalSlots);
    } else {
        emitBytes(OP_CALL, (uint8_t)args.totalSlots);
    }
}

static void ternary(bool canAssign) {
    int elseJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_TERNARY);

    consume(TOKEN_COLON, "Expect ':' after 'then' branch of ternary operator.");

    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);
    parsePrecedence(PREC_TERNARY);

    patchJump(endJump);
}

static void dot(bool canAssign) {
    // check if the next token is a standard identifier or our 'class' keyword
    if (!match(TOKEN_IDENTIFIER) && !match(TOKEN_CLASS)) {
        error("Expect property name after '.'.");
        return;
    }

    uint16_t name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitSetProp(name);
    } else if (canAssign && match(TOKEN_QQ_EQUAL)) {
        emitByte(OP_DUP);
        emitGetProp(name);

        int nilJump = emitJump(OP_JUMP_IF_NIL);

        emitByte(OP_SWAP);
        emitByte(OP_POP);
        int endJump = emitJump(OP_JUMP);

        patchJump(nilJump);
        emitByte(OP_POP);

        expression();
        emitSetProp(name);

        patchJump(endJump);
    } else if (canAssign && (match(TOKEN_PLUS_EQUAL) || match(TOKEN_MINUS_EQUAL) ||
                match(TOKEN_STAR_EQUAL) || match(TOKEN_SLASH_EQUAL) ||
                match(TOKEN_PERCENT_EQUAL))) {
        TokenType opType = parser.previous.type;

        emitByte(OP_DUP);
        emitGetProp(name);

        expression();

        switch (opType) {
            case TOKEN_PLUS_EQUAL: emitByte(OP_ADD); break;
            case TOKEN_MINUS_EQUAL: emitByte(OP_SUBTRACT); break;
            case TOKEN_STAR_EQUAL: emitByte(OP_MULTIPLY); break;
            case TOKEN_SLASH_EQUAL: emitByte(OP_DIVIDE); break;
            case TOKEN_PERCENT_EQUAL: emitByte(OP_MOD); break;
            default: return;
        }
        emitSetProp(name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        bool isTail = current->inTailPosition;

        ArgResult args = argumentList();

        isTail = isTail && check(TOKEN_SEMICOLON);

        if (name < 256) {
            if (args.hasSplat) {
                emitBytes(isTail ? OP_TAIL_INVOKE_SPLAT : OP_INVOKE_SPLAT, (uint8_t)name);
                emitByte((uint8_t)args.totalSlots);
            } else {
                emitBytes(isTail ? OP_TAIL_INVOKE : OP_INVOKE, (uint8_t)name);
                emitByte((uint8_t)args.totalSlots);
            }
        } else {
            uint8_t op = args.hasSplat
                ? (isTail ? OP_TAIL_INVOKE_SPLAT_LONG : OP_INVOKE_SPLAT_LONG)
                : (isTail ? OP_TAIL_INVOKE_LONG : OP_INVOKE_LONG);

            emitByte(op);
            emitByte((uint8_t)((name >> 16) & 0xff));
            emitByte((uint8_t)((name >> 8) & 0xff));
            emitByte((uint8_t)(name & 0xff));
            emitByte(args.totalSlots);
        }
    } else {
        emitGetProp(name);
    }
}

static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE:
            emitByte(OP_FALSE);
            break;
        case TOKEN_NIL:
            emitByte(OP_NIL);
            break;
        case TOKEN_TRUE:
            emitByte(OP_TRUE);
            break;
        default:
            return;
    }
}

static void subscript(bool canAssign) {
    parsePrecedence(PREC_OR);
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_SET_INDEX);
    } else {
        emitByte(OP_GET_INDEX);
    }
}

static void array(bool canAssign) {
    if (match(TOKEN_RIGHT_BRACKET)) {
        emitByte(OP_ARRAY);
        emitByte(0);
        return;
    }

    expression();

    if (match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array size.");
        emitByte(OP_ARRAY_FILL);
    } else {
        int count = 1;
        while (match(TOKEN_COMMA)) {
            expression();
            count++;
            if (count > 255) error("Too many elements in array literal.");
        }
        consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array size.");
        emitByte(OP_ARRAY);
        emitByte(count);
    }
}

static void map(bool canAssign) {
    int item_count = 0;

    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            parsePrecedence(PREC_OR);
            consume(TOKEN_COLON, "Expect ':' after map key.");
            parsePrecedence(PREC_OR);
            item_count++;

            if (item_count > 255) {
                error("Cannot have more than 255 items in a map literal.");
            }
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after map items.");
    emitBytes(OP_MAP, (uint8_t)item_count);
}

static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void character(bool canAssign) {
    uint8_t value = (uint8_t)parser.previous.start[1];
    if (value == '\\') {
        switch (parser.previous.start[2]) {
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            case '0': value = '\0'; break;
            case '\'': value = '\''; break;
            case '\\': value = '\\'; break;
            default: value = parser.previous.start[2];
        }
    }

    emitConstant(NUMBER_VAL((double)value));
}

static void number(bool canAssign) {
    const char* start = parser.previous.start;
    int length = parser.previous.length;
    double value;
    char* endptr;

    bool hasDot = false;
    for (int i = 0; i < length; i++) {
        if (start[i] == '.') {
            hasDot = true;
            break;
        }
    }

    if (length > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        value = (double)strtoul(start, &endptr, 16);
        if (endptr != start + length) {
            error("Invalid hexadecimal literal");
            return;
        }
    } else if (length > 2 && start[0] == '0' && (start[1] == 'o' || start[1] == 'O')) {
        value = (double)strtoul(start + 2, &endptr, 8);
        if (endptr != start + length) {
            error("Invalid octal literal.");
            return;
        }
    } else if (length > 1 && start[0] == '0' && !hasDot) {
        value = (double)strtoul(start, &endptr, 8);
        if (endptr != start + length) {
            error("Invalid octal digit in literal.");
            return;
        }
    } else {
        value = strtod(start, &endptr);
    }

    emitConstant(NUMBER_VAL(value));
}

static void optionalDot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '?.");
    int name = identifierConstant(&parser.previous);

    int nilJump = emitJump(OP_JUMP_IF_NIL);

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Cannot assign to optional chaining expression.");
    } else if (match(TOKEN_LEFT_PAREN)) {
        ArgResult args = argumentList();
        if (name < 256) {
            if (args.hasSplat) {
                emitBytes(OP_INVOKE_SPLAT, (uint8_t)name);
                emitByte((uint8_t)args.totalSlots);
            } else {
                emitBytes(OP_INVOKE, (uint8_t)name);
                emitByte((uint8_t)args.totalSlots);
            }
        } else {
            emitByte(OP_INVOKE_LONG);
            emitByte((uint8_t)((name >> 16) & 0xff));
            emitByte((uint8_t)((name >> 8) & 0xff));
            emitByte((uint8_t)(name & 0xff));
            emitByte(args.totalSlots);
        }
    } else {
        emitGetProp(name);
    }

    patchJump(nilJump);
}

static void nullish(bool canAssign) {
    int nilJump = emitJump(OP_JUMP_IF_NIL);
    int endJump = emitJump(OP_JUMP);

    patchJump(nilJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_NULLISH + 1);
    patchJump(endJump);
}

static void or_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_TRUE);

    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void heredoc(bool canAssign) {
    Token token = parser.previous;

    const char* start = token.start + 3;
    while (*start != '\n' && *start != '\r') {
        start++;
    }
    if (*start == '\r') start++;
    start++;

    const char* end = token.start + token.length;
    while (end > start && *end != '\n' && *end != '\r') {
        end--;
    }
    if (end > start && *end == '\n' && *(end - 1) == '\r') {
        end--;
    }

    int length = (int)(end - start);
    if (length < 0) length = 0;

    emitConstant(OBJ_VAL(copyString(start, length)));
}

static void rawstring(bool canAssign) {
    // skip r"
    const char* source = parser.previous.start + 2;
    int length = parser.previous.length - 3;

    emitConstant(OBJ_VAL(copyString(source, length)));
}

static void string(bool canAssign) {
    const char* source = parser.previous.start + 1;
    int length = parser.previous.length;

    if (parser.previous.type == TOKEN_INTERPOLATION) {
        if (*source == '"') {
            source++;
            length -= 1;
        } else {
            length -= 1;
        }
    } else {
        //source++;
        length -= 2;
    }

    char* buffer = malloc(length + 1);
    if (buffer == NULL) {
        error("Unable to allocate memory.");
        exit(1);
    }

    int j = 0;

    for (int i = 0; i < length; i++) {
        if (source[i] == '\\' && i + 1 < length) {
            switch(source[++i]) {
                case 'n': buffer[j++] = '\n'; break;
                case 'r': buffer[j++] = '\r'; break;
                case 't': buffer[j++] = '\t'; break;
                case '\\': buffer[j++] = '\\'; break;
                case '"': buffer[j++] = '"'; break;
                default: buffer[j++] = source[i]; break;
            }
        } else {
            buffer[j++] = source[i];
        }
    }
    buffer[j] = '\0';

    emitConstant(OBJ_VAL(copyString(buffer, j)));
    free(buffer);
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    bool isConst = false;

    if (arg != -1) {
        if (arg > 255) {
            getOp = OP_GET_LOCAL_LONG;
            setOp = OP_SET_LOCAL_LONG;
        } else {
            getOp = OP_GET_LOCAL;
            setOp = OP_SET_LOCAL;
        }
        isConst = current->locals[arg].isConst;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
        isConst = current->upvalues[arg].isConst;
    } else {
        arg = identifierConstant(&name);
        if (arg > 255) {
            getOp = OP_GET_GLOBAL_LONG;
            setOp = OP_SET_GLOBAL_LONG;
        } else {
            getOp = OP_GET_GLOBAL;
            setOp = OP_SET_GLOBAL;
        }
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        if (isConst) {
            error("Cannot reassign to a constant variable.");
        }
        expression();
        emitSetVar(setOp, arg);
    } else if (canAssign && match(TOKEN_QQ_EQUAL)) {
        if (isConst) {
            error("Cannot reassign to a constant variable.");
        }

        emitGetVar(getOp, arg);

        int nilJump = emitJump(OP_JUMP_IF_NIL);
        int skipJump = emitJump(OP_JUMP);

        patchJump(nilJump);
        emitByte(OP_POP);

        expression();
        emitSetVar(setOp, arg);

        patchJump(skipJump);
    } else if (canAssign && (match(TOKEN_PLUS_EQUAL) || match(TOKEN_MINUS_EQUAL) ||
                match(TOKEN_STAR_EQUAL) || match(TOKEN_SLASH_EQUAL) ||
                match(TOKEN_PERCENT_EQUAL))) {
        if (isConst) {
            error("Cannot mutate or reassign to a constant variable.");
        }

        TokenType opType = parser.previous.type;

        emitGetVar(getOp, arg);
        expression();
        switch(opType) {
            case TOKEN_PLUS_EQUAL: emitByte(OP_ADD); break;
            case TOKEN_MINUS_EQUAL: emitByte(OP_SUBTRACT); break;
            case TOKEN_STAR_EQUAL: emitByte(OP_MULTIPLY); break;
            case TOKEN_SLASH_EQUAL: emitByte(OP_DIVIDE); break;
            case TOKEN_PERCENT_EQUAL: emitByte(OP_MOD); break;
            default: return;
        }
        emitSetVar(setOp, arg);
    } else {
        emitGetVar(getOp, arg);
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static Token syntheticToken(const char* text) {
    Token token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

static void super_(bool canAssign) {
    if (currentClass == NULL) {
        error("Can't use 'super' outside of a class.");
    } else if (!currentClass->hasSuperclass) {
        error("Can't use 'super' in a class with no superclass.");
    }

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint16_t name = identifierConstant(&parser.previous);

    namedVariable(syntheticToken("this"), false);

    if (match(TOKEN_LEFT_PAREN)) {
        bool isTail = current->inTailPosition;

        ArgResult args = argumentList();

        isTail = isTail && check(TOKEN_SEMICOLON);

        namedVariable(syntheticToken("super"), false);

        if (name < 256) {
            if (args.hasSplat) {
                emitBytes(isTail ? OP_TAIL_SUPER_INVOKE_SPLAT : OP_SUPER_INVOKE_SPLAT, (uint8_t)name);
                emitByte((uint8_t)args.totalSlots);
            } else {
                emitBytes(isTail ? OP_TAIL_SUPER_INVOKE : OP_SUPER_INVOKE, (uint8_t)name);
                emitByte(args.totalSlots);
            }
        } else {
            uint8_t op = args.hasSplat
                ? (isTail ? OP_TAIL_SUPER_INVOKE_SPLAT_LONG : OP_SUPER_INVOKE_SPLAT_LONG)
                : (isTail ? OP_TAIL_SUPER_INVOKE_LONG : OP_SUPER_INVOKE_LONG);

            emitByte(op);
            emitByte((uint8_t)((name >> 16) & 0xff));
            emitByte((uint8_t)((name >> 8) & 0xff));
            emitByte((uint8_t)(name & 0xff));
            emitByte((uint8_t)args.totalSlots);
        }
    } else {
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_GET_SUPER, name);
    }
}

static void this_(bool canAssign) {
    bool inModule = false;
    Compiler* comp = current;
    while (comp != NULL) {
        if (comp->type == TYPE_MODULE) {
            inModule = true;
            break;
        }
        comp = comp->enclosing;
    }

    if (currentClass == NULL && !inModule) {
        error("Can't use 'this' outside of a class.");
        return;
    }

    variable(false);
}

static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    //compile
    parsePrecedence(PREC_UNARY);

    if (tryFoldUnary(operatorType)) return;

    switch (operatorType) {
        case TOKEN_BANG:
            emitByte(OP_NOT);
            break;
        case TOKEN_MINUS:
            emitByte(OP_NEGATE);
            break;
        case TOKEN_TILDE:
            emitByte(OP_BITWISE_NOT);
            break;
        default:
            return;
    }
}

/*
static void interpolation(bool canAssign) {
    int partCount = 2;

    // 1. Handle the leading string and the first expression
    string(false);
    expression();
    emitByte(OP_STR);

    // 2. Loop through mid-string interpolation segments
    while (match(TOKEN_INTERPOLATION)) {
        string(false);
        expression();
        emitByte(OP_STR);
        patCount += 2;
    }

    // 3. Handle the final terminating string segment
    if (match(TOKEN_STRING)) {
        string(false);
        partCount++;
    }

    // 4. Tell the VM to pull 'partCount' values off the stack and merge them in one shot
    emitByte(OP_INTERPOLATE);
    emitByte(partCount);
}
*/

static void interpolation(bool canAssign) {
    string(false);

    expression();

    emitByte(OP_STR);
    emitByte(OP_ADD);

    if (match(TOKEN_INTERPOLATION)) {
        interpolation(false);
        emitByte(OP_ADD);
    } else if (match(TOKEN_STRING)) {
        string(false);
        emitByte(OP_ADD);
    }
}

static void importDeclaration() {
    consume(TOKEN_STRING, "Expect module name after 'import'.");

    ObjString* nameString = copyString(
            parser.previous.start + 1,
            parser.previous.length - 2);

    int nameConstant = makeConstant(OBJ_VAL(nameString));

    if (nameConstant < 256) {
        emitBytes(OP_IMPORT, nameConstant);
    } else {
        emitByte(OP_IMPORT_LONG);
        emitByte((uint8_t)((nameConstant >> 16) & 0xff));
        emitByte((uint8_t)((nameConstant >> 8) & 0xff));
        emitByte((uint8_t)(nameConstant & 0xff));
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after import path.");
}

static void backtick(bool canAssign) {
    ObjString* string = copyString(
            parser.previous.start + 1,
            parser.previous.length - 2);

    Token processToken = { TOKEN_IDENTIFIER, "Process", 7, parser.previous.line };
    int processConstant = identifierConstant(&processToken);

    if (processConstant < 256) {
        emitBytes(OP_GET_GLOBAL, processConstant);
    } else {
        emitByte(OP_GET_GLOBAL_LONG);
        emitByte((uint8_t)((processConstant >> 16) & 0xff));
        emitByte((uint8_t)((processConstant >> 8) & 0xff));
        emitByte((uint8_t)(processConstant & 0xff));
    }

    emitConstant(OBJ_VAL(string));
    emitInvoke("capture", 1);
}

static void lambda(bool canAssign);

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]       = {grouping, call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]       = {map,      NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACKET]     = {array,    subscript,  PREC_CALL},
    [TOKEN_RIGHT_BRACKET]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]              = {NULL,     dot,    PREC_CALL},
    [TOKEN_DOT_DOT_DOT]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]            = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]             = {NULL,     binary, PREC_TERM},
    [TOKEN_PLUS_PLUS]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS_MINUS]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CARET]            = {NULL,     binary, PREC_XOR},
    [TOKEN_PERCENT]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SEMICOLON]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]            = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]             = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR_STAR]        = {NULL,     binary, PREC_EXP},
    [TOKEN_TILDE]            = {unary,    NULL,   PREC_UNARY},
    [TOKEN_2LEFT]            = {NULL,     binary, PREC_SHIFT},
    [TOKEN_2RIGHT]           = {NULL,     binary, PREC_SHIFT},
    [TOKEN_BANG]             = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]       = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]      = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]             = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]       = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]           = {string,   NULL,   PREC_NONE},
    [TOKEN_RAW_STRING]       = {rawstring, NULL,   PREC_NONE},
    [TOKEN_HEREDOC]          = {heredoc,  NULL,   PREC_NONE},
    [TOKEN_NUMBER]           = {number,   NULL,   PREC_NONE},
    [TOKEN_CHAR]             = {character,NULL,   PREC_NONE},
    [TOKEN_AND]              = {NULL,     and_,   PREC_AND},
    [TOKEN_AMPERSAND]        = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_PIPE]             = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_CLASS]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]             = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]            = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]              = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FOREACH]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]              = {lambda,   NULL,   PREC_NONE},
    [TOKEN_IF]               = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]              = {literal,  NULL,   PREC_NONE},
    [TOKEN_OR]               = {NULL,     or_,    PREC_OR},
    [TOKEN_PRINT]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]            = {super_,   NULL,   PREC_NONE},
    [TOKEN_THIS]             = {this_,    NULL,   PREC_NONE},
    [TOKEN_TRUE]             = {literal,  NULL,   PREC_NONE},
    [TOKEN_VAR]              = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]              = {NULL,     NULL,   PREC_NONE},
    [TOKEN_QQ]               = {NULL,     nullish,PREC_NULLISH},
    [TOKEN_Q_DOT]            = {NULL,     optionalDot, PREC_CALL},
    [TOKEN_QUESTION]         = {NULL,     ternary,PREC_TERNARY},
    [TOKEN_INTERPOLATION]    = {interpolation, NULL, PREC_NONE},
    [TOKEN_BACKTICK_STRING]  = {backtick, NULL,   PREC_NONE},
//    [TOKEN_IMPORT]           = {import,   NULL, PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static Value parseConstant() {
    if (match(TOKEN_NUMBER)) {
        return NUMBER_VAL(strtod(parser.previous.start, NULL));
    } else if (match(TOKEN_STRING)) {
        return OBJ_VAL(copyString(parser.previous.start + 1,
                    parser.previous.length - 2));
    } else if (match(TOKEN_TRUE)) {
        return BOOL_VAL(true);
    } else if (match(TOKEN_FALSE)) {
        return BOOL_VAL(false);
    } else if (match(TOKEN_NIL)) {
        return NIL_VAL;
    } else {
        errorAtCurrent("Default parameter must be a constant (number, string, bool, or nil).");
    }
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);

    beginScope();

    //printf("[FUNCTION] begin argument parsing\n");
    if (type == TYPE_GETTER) {
        // getters have 0 parameters implicitly
        // no '(' or ')' to parse.
    } else if (type == TYPE_SETTER) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after setter.");

        int constant = parseVariable("Expect setter parameter name.");
        defineVariable(constant);
        current->function->arity = 1;

        consume(TOKEN_RIGHT_PAREN, "Expect ')' after setter parameter.");
    } else {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

        if (!check(TOKEN_RIGHT_PAREN)) {
            bool isOptional = false;
            do {
                if (match(TOKEN_DOT_DOT_DOT)) {
                    current->function->isVariadic = true;
                    int constant = parseVariable("Expect rest parameter name.");
                    defineVariable(constant);

                    current->function->arity++;

                    if (check(TOKEN_COMMA)) {
                        error("Cannot have parameters after a rest parameter.");
                    }

                    break;
                }

                current->function->arity++;
                if (current->function->arity > 255) {
                    errorAtCurrent("Can't have more than 255 parameters.");
                }
                //printf("get constant\n");
                int constant = parseVariable("Expect parameter name.");
                //printf("define variable\n");
                defineVariable(constant);

                //printf("looking for default value\n");
                if (match(TOKEN_EQUAL)) {
                    //printf("getting default value\n");
                    isOptional = true;
                    Value defaultValue = parseConstant();
                    writeValueArray(&current->function->defaults, defaultValue);
                } else if (isOptional) {
                    error("Cannot have a require parameter after an optional one.");
                }
                if (!isOptional) current->function->minArity++;
            } while (match(TOKEN_COMMA));
        }
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    }
    //printf("[FUNCTION] before expect\n");
    //consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    //printf("[FUNCTION] after expect\n");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();
    if (type == TYPE_METHOD || type == TYPE_INITIALIZER || type == TYPE_STATIC_METHOD) {
        function->isfree = false;
    }

    int constant = makeConstant(OBJ_VAL(function));
    if (constant < 256) {
        emitByte(OP_CLOSURE);
        emitByte((uint8_t)constant);
    } else {
        emitByte(OP_CLOSURE_LONG);
        emitByte((uint8_t)((constant >> 16) & 0xff));
        emitByte((uint8_t)((constant >> 8) & 0xff));
        emitByte((uint8_t)(constant & 0xff));
    }

    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte((uint8_t)((compiler.upvalues[i].index >> 8) & 0xff));
        emitByte((uint8_t)(compiler.upvalues[i].index & 0xff));
    }
}

static void parseFunction(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'fun.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        bool isOptional = false;
        do {
            if (match(TOKEN_DOT_DOT_DOT)) {
                current->function->isVariadic = true;

                int constant = parseVariable("Expect rest parameter name.");
                defineVariable(constant);

                current->function->arity++;

                if (check(TOKEN_COMMA)) {
                    error("Cannot have parameters after a rest parameter.");
                }

                break;
            }

            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            int constant = parseVariable("Expect parameter name.");
            defineVariable(constant);

            if (match(TOKEN_EQUAL)) {
                isOptional = true;
                Value defaultValue = parseConstant();
                writeValueArray(&current->function->defaults, defaultValue);
            } else if (isOptional) {
                error("Can't have a required parameter after an optional one.");
            }
            if (!isOptional) current->function->minArity++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();

    int constant = makeConstant(OBJ_VAL(function));
    if (constant < 256) {
        emitBytes(OP_CLOSURE, constant);
    } else {
        emitByte(OP_CLOSURE_LONG);
        emitByte((uint8_t)((constant >> 16) & 0xff));
        emitByte((uint8_t)((constant >> 8) & 0xff));
        emitByte((uint8_t)(constant & 0xff));
    }

    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte((uint8_t)(compiler.upvalues[i].index >> 8) & 0xff);
        emitByte((uint8_t)(compiler.upvalues[i].index & 0xff));
    }
}

static void lambda(bool canAssign) {
    parseFunction(TYPE_FUNCTION);
}

static void method() {
    bool isStatic = match(TOKEN_STATIC);

    consume(TOKEN_IDENTIFIER, "Expect method name.");
    Token nameToken = parser.previous;
    int constant = identifierConstant(&nameToken);

    FunctionType type = TYPE_METHOD;
    OpCode shortOp = OP_METHOD;
    OpCode longOp = OP_METHOD_LONG;

    if (check(TOKEN_LEFT_BRACE)) {
        if (isStatic) {
            error("Cannot declare static getters.");
        }
        // getter: property { ... }
        type = TYPE_GETTER;
        shortOp = OP_GETTER;
        longOp = OP_GETTER_LONG;
    } else if (match(TOKEN_EQUAL)) {
        if (isStatic) {
            error("Cannot declare static setters.");
            return;
        }
        // setter: property=(val) { ... }
        type = TYPE_SETTER;
        shortOp = OP_SETTER;
        longOp = OP_SETTER_LONG;
    } else {
        if (isStatic) {
            type = TYPE_STATIC_METHOD;
            shortOp = OP_STATIC_METHOD;
            longOp = OP_STATIC_METHOD_LONG;
        } else if (nameToken.length == 4 &&
                memcmp(nameToken.start, "init", 4) == 0) {
            type = TYPE_INITIALIZER;
        }
    }

    function(type);
    if (constant < 256) {
        emitBytes(shortOp, (uint8_t)constant);
    } else {
        emitByte(longOp);
        emitByte((uint8_t)((constant >> 16) & 0xff));
        emitByte((uint8_t)((constant >> 8) & 0xff));
        emitByte((uint8_t)(constant & 0xff));
    }
}

static void classConstant() {
    // 1. consume the contant identifiier name
    consume(TOKEN_IDENTIFIER, "Expect class constant name.");
    Token constantName = parser.previous;

    // store the name in the chunks constant pool
    int nameIndex = identifierConstant(&constantName);

    // 2. expect the '=' assignment operator
    consume(TOKEN_EQUAL, "Expect '=' after class constant name.");

    // 3. compile the expression value
    expression();

    // 4. expect the closing semicolon
    consume(TOKEN_SEMICOLON, "Expect ';' after class constant declaration.");

    // 5. emit the instruction to bind the value to the class namespace
    if (nameIndex > 255) {
        emitByte(OP_DEFINE_CLASS_CONST_LONG);
        emitByte((uint8_t)((nameIndex >> 16) & 0xff));
        emitByte((uint8_t)((nameIndex >> 8) & 0xff));
        emitByte((uint8_t)(nameIndex & 0xff));
    } else {
        emitBytes(OP_DEFINE_CLASS_CONST, (uint8_t)nameIndex);
    }
}

static void classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    int nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    if (nameConstant < 256) {
        emitBytes(OP_CLASS, nameConstant);
    } else {
        emitByte(OP_CLASS_LONG);
        emitByte((uint8_t)((nameConstant >> 16) & 0xff));
        emitByte((uint8_t)((nameConstant >> 8) & 0xff));
        emitByte((uint8_t)(nameConstant & 0xff));
    }

    defineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;

    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);

        if (identifiersEqual(&className, &parser.previous)) {
            error("A class can't inherit from itself.");
        }

        beginScope();
        addLocal(syntheticToken("super"));
        current->locals[current->localCount - 1].isConst = true;
        defineVariable(0);

        namedVariable(className, false);
        emitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }

    namedVariable(className, false);

    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (match(TOKEN_INCLUDE)) {
            consume(TOKEN_IDENTIFIER, "Expect class name after 'include'.");

            namedVariable(parser.previous, false);
            emitByte(OP_INCLUDE);
            consume(TOKEN_SEMICOLON, "Expect ';' after include statement.");
        } else if (match(TOKEN_CONST)) {
            classConstant();
        } else { 
            method();
        }
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

    emitByte(OP_POP);

    if (classCompiler.hasSuperclass) {
        endScope();
    }

    currentClass = currentClass->enclosing;
}

static void funDeclaration() {
    int global = parseVariable("Expect function name.");
    markInitialized();
    parseFunction(TYPE_FUNCTION);
    defineVariable(global);
}

static void varDeclaration() {
    bool isConst = (parser.previous.type == TOKEN_CONST);

    //int global = parseVariable("Expect variable name.");

    int globals[64];
    int varCount = 0;

    // 1. loop through all comma-separated identifiers
    do {
        if (varCount >= 64) {
            error("Too many variables in a single declaration statement.");
            break;
        }

        globals[varCount] = parseVariable("Expect variable name.");;

        if (current->scopeDepth > 0 && isConst) {
            current->locals[current->localCount - 1].isConst = true;
        }
        varCount++;
    } while (match(TOKEN_COMMA));

    if (match(TOKEN_EQUAL)) {
        expression();

        if (varCount > 1) {
            emitBytes(OP_UNPACK, (uint8_t)varCount);
        }
    } else {
        if (isConst) {
            error("Constant declarations must be initialized.");
        }
        for (int i = 0; i < varCount; i++) {
            emitByte(OP_NIL);
        }
    }
    consume(TOKEN_SEMICOLON,
            "Expect ';' after variable declaration.");

    for (int i = 0; i < varCount; i++) {
        defineVariableExt(globals[i], isConst);
    }
}

static void assignToVariable(Token name) {
    int arg = resolveLocal(current, &name);

    if (arg != -1) {
        if (current->locals[arg].isConst) {
            error("Cannot assign to a constant variable");
        }

        if (arg > 255) {
            emitSetVar(OP_SET_LOCAL_LONG, arg);
        } else {
            emitSetVar(OP_SET_LOCAL, arg);
        }
    } else {
        int nameIndex = identifierConstant(&name);
        if (arg > 255) {
            emitSetVar(OP_SET_GLOBAL_LONG, nameIndex);
        } else {
            emitSetVar(OP_SET_GLOBAL, nameIndex);
        }
    }
}

static void destructuringAssignment() {
    Token targets[64];
    int varCount = 0;

    do {
        if (varCount >= 64) {
            error("Too many targets in destructuring assignment.");
            break;
        }
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        targets[varCount++] = parser.previous;
    } while (match(TOKEN_COMMA));

    consume(TOKEN_EQUAL, "Expect '=' after assignment targets.");

    expression();

    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");

    emitBytes(OP_UNPACK, (uint8_t)varCount);

    for (int i = 0; i < varCount; i++) {
        assignToVariable(targets[i]);
        emitByte(OP_POP);
    }
}

static void expressionStatement() {
    if (check(TOKEN_IDENTIFIER) && peekNextToken().type == TOKEN_COMMA) {
        destructuringAssignment();
        return;
    }

    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

static void foreachStatement() {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'foreach'.");

    consume(TOKEN_IDENTIFIER, "Expect first loop variable name.");
    Token var1 = parser.previous;
    
    bool isDual = false;
    Token var2;

    if (match(TOKEN_COMMA)) {
        consume(TOKEN_IDENTIFIER, "Expect second loop variable name.");
        var2 = parser.previous;
        isDual = true;
    }

    consume(TOKEN_IN, "Expect 'in' after loop variables.");

    // 1. evaluate the collection expression
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after foreach clauses.");

    // 2. turn the collection into an iterator object
    emitInvoke("iter", 0);

    // 3. inject a hidden local variable to keep track of the iter
    Token iterToken = { TOKEN_IDENTIFIER, "iter ", 5, var1.line };
    Local* iterLocal = &current->locals[current->localCount++];
    iterLocal->name = iterToken;
    iterLocal->depth = current->scopeDepth;
    iterLocal->isCaptured = false;

    int loopStart = currentChunk()->count;

    // 4. check condition. call iter.next()
    namedVariable(iterToken, false);
    emitInvoke("next", 0);

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    // 5. scope 2: intter block scope for the users's loop body and variables
    beginScope();

    if (isDual) {
        // fetch key and bind to var1
        namedVariable(iterToken, false);
        emitInvoke("key", 0);
        Local* l1 = &current->locals[current->localCount++];
        l1->name = var1;
        l1->depth = current->scopeDepth;
        l1->isCaptured = false;

        // fetch value and bind to var2
        namedVariable(iterToken, false);
        emitInvoke("val", 0);
        Local* l2 = &current->locals[current->localCount++];
        l2->name = var2;
        l2->depth = current->scopeDepth;
        l2->isCaptured = false;
    } else {
        // single variable syntax: fetch value and bind to var 1
        namedVariable(iterToken, false);
        emitInvoke("val", 0);
        Local* l1 = &current->locals[current->localCount++];
        l1->name = var1;
        l1->depth = current->scopeDepth;
        l1->isCaptured = false;
    }

    // 6 compile loop and body
    statement();

    // clears user variables (var1, var2)
    endScope();

    // 7. loop back up to run iter.next() again
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP);

    endScope();
}

static void patchTryOffset(int slot, int target, int offsetFromEnd) {
    if (target == 0) {
        currentChunk()->code[slot] = 0;
        currentChunk()->code[slot + 1] = 0;
        return;
    }
    int jump = target - (slot + offsetFromEnd);
    currentChunk()->code[slot] = (jump >> 8) & 0xff;
    currentChunk()->code[slot + 1] = jump & 0xff;
}

static void throwStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after throw value.");

    emitByte(OP_THROW);
}

static void tryStatement() {
    int tryLocalCount = current->localCount;

    consume(TOKEN_LEFT_BRACE, "Expect '{' before try body.");

    emitByte(OP_TRY);
    int catchJumpPlaceholder = currentChunk()->count;
    emitBytes(0xff, 0xff);
    int finallyJumpPlaceholder = currentChunk()->count;
    emitBytes(0xff, 0xff);

    beginScope();
    block();
    endScope();

    int endTryJump = emitJump(OP_END_TRY);

    int catchTarget = 0;
    int catchSuccessJumps[256];
    int catchSuccessCount = 0;
    int lastMismatchJump = -1;

    while (match(TOKEN_CATCH)) {
        if (lastMismatchJump != -1) {
            patchJump(lastMismatchJump);
            emitByte(OP_POP);
            lastMismatchJump = -1;
        }

        int currentCatchStart = currentChunk()->count;
        if (catchTarget == 0) {
            catchTarget = currentCatchStart;
        }

        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'catch'.");

        consume(TOKEN_IDENTIFIER, "Expect exception type name.");
        Token first = parser.previous;

        Token exceptionVar;
        bool hasTypeCheck = false;
        Token typeName;

        if (check(TOKEN_RIGHT_PAREN)) {
            exceptionVar = first;
        } else {
            hasTypeCheck = true;
            typeName = first;
            consume(TOKEN_IDENTIFIER, "Expect exception variable name.");
            exceptionVar = parser.previous;
        }

        consume(TOKEN_RIGHT_PAREN, "Expect ')' after exception variable.");
        consume(TOKEN_LEFT_BRACE, "Expect '{' before catch body.");

        current->localCount = tryLocalCount;

        if (hasTypeCheck) {
            emitByte(OP_DUP);
            namedVariable(typeName, false);
            emitByte(OP_INSTANCEOF);

            lastMismatchJump = emitJump(OP_JUMP_IF_FALSE);
            emitByte(OP_POP);

            beginScope();
            addLocal(exceptionVar);
            markInitialized();
            block();
            endScope();

            catchSuccessJumps[catchSuccessCount++] = emitJump(OP_JUMP);
    
            /*
            patchJump(mismatchJump);
            emitByte(OP_POP);
            emitByte(OP_THROW);
            */
        } else {
            beginScope();
            addLocal(exceptionVar);
            markInitialized();
            block();
            endScope();

            catchSuccessJumps[catchSuccessCount++] = emitJump(OP_JUMP);
        }
    }

    if (lastMismatchJump != -1) {
        patchJump(lastMismatchJump);
        emitByte(OP_POP);
        emitByte(OP_THROW);
    }

    int finallyTarget = 0;
    bool hasFinally = false;

    if (match(TOKEN_FINALLY)) {
        hasFinally = true;
        finallyTarget = currentChunk()->count;
        consume(TOKEN_LEFT_BRACE, "Expect '{' before finally body.");

        //current->localCount = tryLocalCount;

        beginScope();
        block();
        endScope();

        emitByte(OP_END_FINALLY);
    }

    int finalDestination = currentChunk()->count;

    /*
    if (catchTarget == 0) {
        catchTarget = hasFinally ? finallyTarget : finalDestination;
    }
    */
    int successTarget = hasFinally ? finallyTarget : finalDestination;

    patchTryOffset(endTryJump, successTarget, 2);

    for (int i = 0; i < catchSuccessCount; i++) {
        patchTryOffset(catchSuccessJumps[i], successTarget, 2);
    }

    patchTryOffset(catchJumpPlaceholder, catchTarget, 4);
    patchTryOffset(finallyJumpPlaceholder, hasFinally ? finallyTarget: 0, 2);
}

static void forStatement() {
    beginScope();
    int jumps[255];

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TOKEN_SEMICOLON)) {
        // none
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        expressionStatement();
    }

    int loopStart = currentChunk()->count;

    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    Loop loop;
    loop.scopeDepth = current->scopeDepth;
    loop.firstLocalSlot = current->localCount;
    loop.enclosing = currentLoop;
    loop.continueTarget = loopStart;
    loop.breakCount = 0;
    loop.breakJumps = jumps;
    currentLoop = &loop;

    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        loop.continueTarget = incrementStart;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    for (int i = 0; i < loop.breakCount; i++) {
        patchJump(loop.breakJumps[i]);
    }
    currentLoop = loop.enclosing;

    endScope();
}

static void switchStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before cases.");

    beginScope();
    Token dummyToken = {.start = "", .length = 0};
    addLocal(dummyToken);
    markInitialized();

    int endJumps[255];
    int jumpCount = 0;

    while (match(TOKEN_CASE)) {
        if (jumpCount > 255) {
            error("Too many cases in switch statement.");
        }

        emitByte(OP_DUP);
        expression();
        consume(TOKEN_COLON, "Expect ':' after case.");
        emitByte(OP_EQUAL);

        int skipCaseJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);

        while (!check(TOKEN_CASE) && !check(TOKEN_EOF) && !check(TOKEN_DEFAULT) && !check(TOKEN_RIGHT_BRACE)) {
            statement();
        }

        endJumps[jumpCount++] = emitJump(OP_JUMP);

        patchJump(skipCaseJump);

        emitByte(OP_POP);
    }

    if (match(TOKEN_DEFAULT)) {
        consume(TOKEN_COLON, "Expect ':' after 'default'.");

        while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
            statement();
        }
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after last case or default.");

    for (int i = 0; i < jumpCount; i++) {
        patchJump(endJumps[i]);
    }
    endScope();
}

static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void printStatement() {
    int argCount = 0;
    do {
        expression();
        argCount++;
    } while (match(TOKEN_COMMA));

    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitBytes(OP_PRINT, argCount);
}

static void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        if (current->type == TYPE_INITIALIZER) {
            error("Can't return a value from an initializer.");
        }

        // mark that the upcoming expression tree starts in tail position
        bool wasTail = current->inTailPosition;
        current->inTailPosition = true;

        expression();

        current->inTailPosition = wasTail;

        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void continueStatement() {
    if (currentLoop == NULL) {
        error("Can't use 'continue' outside of a loop.");
        return;
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue.");

    for (int i = current->localCount - 1; i >= currentLoop->firstLocalSlot; i--) {
        if (current->locals[i].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
    }

    emitLoop(currentLoop->continueTarget);
}

static void breakStatement() {
    if (currentLoop == NULL) {
        error("Can't use 'break' outside of a loop.");
        return;
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");

    /*
    int locals = current->localCount - currentLoop->firstLocalSlot;
    if (locals > 0) {
        emitBytes(OP_POPN, locals);
    }
    */
    for (int i = current->localCount - 1; i >= currentLoop->firstLocalSlot; i--) {
        if (current->locals[i].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
    }

    currentLoop->breakJumps[currentLoop->breakCount++] = emitJump(OP_JUMP);
}

static void whileStatement() {
    int jumps[255];

    int loopStart = currentChunk()->count;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    Loop loop;
    loop.scopeDepth = current->scopeDepth;
    loop.firstLocalSlot = current->localCount;
    loop.continueTarget = loopStart;
    loop.enclosing = currentLoop;
    loop.breakCount = 0;
    loop.breakJumps = jumps;
    currentLoop = &loop;

    statement();
    emitLoop(loopStart);

    patchJump(exitJump);

    emitByte(OP_POP);

    for (int i = 0; i < loop.breakCount; i++) {
        patchJump(loop.breakJumps[i]);
    }
    currentLoop = loop.enclosing;

}

static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.previous.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_FOREACH:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default:
                ;
        }

        advance();
    }
}

static void declaration() {
    if (match(TOKEN_CLASS)) {
        classDeclaration();
    } else if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (match(TOKEN_VAR) | match(TOKEN_CONST)) {
        varDeclaration();
    } else {
        statement();
    }

    if (parser.panicMode) synchronize();
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_SEMICOLON)) {
        // do nothing, blank statement
        return;
    } else if (match(TOKEN_IMPORT)) {
        importDeclaration();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_FOREACH)) {
        foreachStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_SWITCH)) {
        switchStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_BREAK)) {
        breakStatement();
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    } else if (match(TOKEN_TRY)) {
        tryStatement();
    } else if (match(TOKEN_THROW)) {
        throwStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

ObjFunction* compileModule(const char* source, ObjString* filename) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_MODULE);
    current->function->filename = filename;

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}

static void compileModuleMethod(ObjClass* klass) {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    Token nameToken = parser.previous;

    ObjString* methodName = copyString(nameToken.start, nameToken.length);
    push(OBJ_VAL(methodName));

    FunctionType type = TYPE_METHOD;

    if (check(TOKEN_LEFT_BRACE)) {
        type = TYPE_GETTER;
    } else if (match(TOKEN_EQUAL)) {
        type = TYPE_SETTER;
    } else {
        if (nameToken.length == 4 && memcmp(nameToken.start, "init", 4) == 0) {
            type = TYPE_INITIALIZER;
        }
    }

    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    if  (type == TYPE_GETTER) {
        // none
    } else if (type == TYPE_SETTER) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after setter.");

        int constant = parseVariable("Expect setter parameter name.");
        defineVariable(constant);
        current->function->arity = 1;
        current->function->minArity = 1;

        consume(TOKEN_RIGHT_PAREN, "Expect ')' after setter parameter.");
    } else {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
        if (!check(TOKEN_RIGHT_PAREN)) {
            bool isOptional = false;
            do {
                if (match(TOKEN_DOT_DOT_DOT)) {
                    current->function->isVariadic = true;
                    int constant = parseVariable("Expect rest parameter name.");
                    defineVariable(constant);

                    current->function->arity++;

                    if (check(TOKEN_COMMA)) {
                        error("Cannot have parameters after a rest paramenter.");
                    }

                    break;
                }

                current->function->arity++;
                if (current->function->arity > 255) {
                    errorAtCurrent("Can't have more than 255 parameters.");
                }

                int constant = parseVariable("Expect paameter name.");
                defineVariable(constant);

                if (match(TOKEN_EQUAL)) {
                    isOptional = true;
                    Value defaultValue = parseConstant();
                    writeValueArray(&current->function->defaults, defaultValue);
                } else if (isOptional)  {
                    error("Cannot have a require parameter after an optional one.");
                }

                if (!isOptional) current->function->minArity++;
            } while (match(TOKEN_COMMA));
        }
        consume(TOKEN_RIGHT_PAREN, "Expect '{' before function body.");
    }

    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* methodFunc = endCompiler();

    if (type == TYPE_METHOD || type == TYPE_INITIALIZER || type == TYPE_STATIC_METHOD) {
        methodFunc->isfree = false;
    }

    ObjClosure* methodClosure = newClosure(methodFunc);
    push(OBJ_VAL(methodClosure));

    if (type == TYPE_GETTER) {
        tableSet(&klass->getters, methodName, OBJ_VAL(methodClosure));
    } else if (type == TYPE_SETTER) {
        tableSet(&klass->setters, methodName, OBJ_VAL(methodClosure));
    } else {
        tableSet(&klass->methods, methodName, OBJ_VAL(methodClosure));
    }

    pop();
    pop();
}

bool compileClassModule(const char* source, ObjClass* klass) {
    initScanner(source);

    parser.hadError = false;
    parser.panicMode = false;
    advance();

    ClassCompiler classCompiler;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;
    //initCompiler(&compiler, TYPE_SCRIPT);

    while (!check(TOKEN_EOF)) {
        compileModuleMethod(klass);
    }

    consume(TOKEN_EOF, "Expect end of file.");

    currentClass = classCompiler.enclosing;
    
    return !parser.hadError;
}

ObjFunction* compile(const char* source, ObjString* filename) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);
    current->function->filename = filename;

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    // 1. include phase
    // the compiler ensure includes only happen at the top, but assumes the host
    // environment already ran them
    /*
    while (match(TOKEN_INCLUDE)) {
        consume(TOKEN_STRING, "Expect string after 'include'.");
        consume(TOKEN_SEMICOLON, "Expect ';' after include path.");
    }
    */

    // 2. main compilation phase
    while (!match(TOKEN_EOF)) {
        if (match(TOKEN_END_MARKER)) {
            Token dataToken = parser.previous;
            
            ObjString* dataStr = copyString(dataToken.start, dataToken.length);

            tableSet(&vm.globals, copyString("DATA", 4), OBJ_VAL(dataStr));
            break;
        }
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
    Compiler* compiler = current;
    while (compiler != NULL) {
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
