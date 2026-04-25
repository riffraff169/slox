#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_XOR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
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
} Local;

typedef struct {
    uint16_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;

    Local locals[8192];
    int localCount;
    Upvalue upvalues[8192];
    int scopeDepth;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool hasSuperclass;
} ClassCompiler;

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
    if (current->type == TYPE_INITIALIZER) {
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
    current = compiler;
    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start,
                parser.previous.length);
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

static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), function->name != NULL
                ? function->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return function;
}

static void beginScope() {
    current->scopeDepth++;
}

static void endScope() {
    current->scopeDepth--;

    int n = 0;
    while (current->localCount > 0 &&
            current->locals[current->localCount - 1].depth >
            current->scopeDepth) {
        if (current->locals[current->localCount -1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        //n++;
        current->localCount--;
    }
    
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
        bool isLocal) {
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
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (int)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (int)upvalue, false);
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

static void defineVariable(int global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    if (global < 256) {
        emitBytes(OP_DEFINE_GLOBAL, (uint8_t)global);
    } else {
        emitByte(OP_DEFINE_GLOBAL_LONG);
        emitByte((uint8_t)((global >> 16) & 0xff));
        emitByte((uint8_t)((global >> 8) & 0xff));
        emitByte((uint8_t)(global & 0xff));
    }
}

static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    int precedence = (operatorType == TOKEN_STAR_STAR)
        ? rule->precedence
        : (rule->precedence + 1);
    parsePrecedence((Precedence)precedence);

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
        case TOKEN_LESS:
            emitByte(OP_LESS);
            break;
        case TOKEN_LESS_EQUAL:
            emitBytes(OP_GREATER, OP_NOT);
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
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    int name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        if (name < 256) {
            emitBytes(OP_SET_PROPERTY, (uint8_t)name);
        } else {
            emitByte(OP_SET_PROPERTY_LONG);
            emitByte((uint8_t)((name >> 16) & 0xff));
            emitByte((uint8_t)((name >> 8) & 0xff));
            emitByte((uint8_t)(name & 0xff));
        }
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        if (name < 256) {
            emitBytes(OP_INVOKE, (uint8_t)name);
            emitByte(argCount);
        } else {
            emitByte(OP_INVOKE_LONG);
            emitByte((uint8_t)((name >> 16) & 0xff));
            emitByte((uint8_t)((name >> 8) & 0xff));
            emitByte((uint8_t)(name & 0xff));
            emitByte(argCount);
        }
    } else {
        if (name < 256) {
            emitBytes(OP_GET_PROPERTY, (uint8_t)name);
        } else {
            emitByte(OP_GET_PROPERTY_LONG);
            emitByte((uint8_t)((name >> 16) & 0xff));
            emitByte((uint8_t)((name >> 8) & 0xff));
            emitByte((uint8_t)(name & 0xff));
        }
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

static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void or_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_TRUE);

    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
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
    if (arg != -1) {
        if (arg > 255) {
            getOp = OP_GET_LOCAL_LONG;
            setOp = OP_SET_LOCAL_LONG;
        } else {
            getOp = OP_GET_LOCAL;
            setOp = OP_SET_LOCAL;
        }
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
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
        expression();
        if (setOp == OP_SET_GLOBAL_LONG || setOp == OP_SET_LOCAL_LONG) {
            emitByte(setOp);
            emitByte((uint8_t)((arg >> 16) & 0xff));
            emitByte((uint8_t)((arg >> 8) & 0xff));
            emitByte((uint8_t)(arg & 0xff));
        } else if (setOp == OP_SET_UPVALUE) {
            emitByte(setOp);
            emitByte((uint8_t)((arg >> 8) & 0x0ff));
            emitByte((uint8_t)(arg & 0xff));
        } else {
            emitBytes(setOp, (uint8_t)arg);
        }
    } else {
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
    int name = identifierConstant(&parser.previous);

    namedVariable(syntheticToken("this"), false);
    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_SUPER_INVOKE, name);
        emitByte(argCount);
    } else {
        namedVariable(syntheticToken("super"), false);
        emitBytes(OP_GET_SUPER, name);
    }
}

static void this_(bool canAssign) {
    if (currentClass == NULL) {
        error("Can't use 'this' outside of a class.");
        return;
    }

    variable(false);
}

static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    //compile
    parsePrecedence(PREC_UNARY);

    switch (operatorType) {
        case TOKEN_BANG:
            emitByte(OP_NOT);
            break;
        case TOKEN_MINUS:
            emitByte(OP_NEGATE);
            break;
        default:
            return;
    }
}

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
    [TOKEN_MINUS]            = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]             = {NULL,     binary, PREC_TERM},
    [TOKEN_PLUS_PLUS]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS_MINUS]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CARET]            = {NULL,     binary, PREC_XOR},
    [TOKEN_PERCENT]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]            = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]             = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR_STAR]        = {NULL,     binary, PREC_EXP},
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
    [TOKEN_NUMBER]           = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]              = {NULL,     and_,   PREC_AND},
    [TOKEN_AMPERSAND]        = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_PIPE]             = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_CLASS]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]             = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]            = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]              = {NULL,     NULL,   PREC_NONE},
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
    [TOKEN_INTERPOLATION]    = {interpolation, NULL, PREC_NONE},
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
    //printf("[FUNCTION] before expect\n");
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    //printf("[FUNCTION] after expect\n");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* function = endCompiler();

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
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    int constant = identifierConstant(&parser.previous);

    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 &&
            memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }

    function(type);
    if (constant < 256) {
        emitBytes(OP_METHOD, (uint8_t)constant);
    } else {
        emitByte(OP_METHOD_LONG);
        emitByte((uint8_t)((constant >> 16) & 0xff));
        emitByte((uint8_t)((constant >> 8) & 0xff));
        emitByte((uint8_t)(constant & 0xff));
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
        defineVariable(0);

        namedVariable(className, false);
        emitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }

    namedVariable(className, false);

    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        method();
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
    int global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON,
            "Expect ';' after variable declaration.");

    defineVariable(global);
}

static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

static void forStatement() {
    beginScope();
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
        consume(TOKEN_SEMICOLON, "Expect ';' after loop doncition.");

        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
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

    endScope();
}

static void switchStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before cases.");

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
    emitByte(OP_POP);
    for (int i = 0; i < jumpCount; i++) {
        patchJump(endJumps[i]);
    }
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

        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void whileStatement() {
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP);
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
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    if (parser.panicMode) synchronize();
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_IMPORT)) {
        importDeclaration();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_SWITCH)) {
        switchStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

ObjFunction* compile(const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF)) {
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
