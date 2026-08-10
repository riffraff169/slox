#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"
#include "object.h"

/*
typedef struct {
    const char* start;
    const char* current;
    int line;
    int interpolationDepth;
    const char* filename;
    bool atstartofline;
} Scanner;
*/

static Scanner scanner;

void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
    scanner.interpolationDepth = 0;
    scanner.atstartofline = true;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        c == '_';
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAtEnd() {
    return *scanner.current == '\0';
}

static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

static char peek() {
    return *scanner.current;
}

static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    token.filename = scanner.filename;
    return token;
}

static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static void skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                scanner.atstartofline = true;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else if (peekNext() == '*') {
                    // consume / *
                    advance();
                    advance();

                    while (!isAtEnd()) {
                        if (peek() == '*' && peekNext() == '/') {
                            advance();
                            advance();
                            break;
                        }

                        if (peek() == '\n') {
                            scanner.line++;
                        }

                        advance();
                    }
                } else {
                    return;
                }
                break;
            case '#':
                while (peek() != '\n' && !isAtEnd()) {
                    advance();
                }
                break;
            default:
                return;
        }
    }
}

static TokenType checkKeyword(int start, int length,
        const char* rest, TokenType type) {
    if (scanner.current - scanner.start == start + length &&
            memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    switch (scanner.start[0]) {
        case '_': return checkKeyword(1, 6, "_END__", TOKEN_END_MARKER);
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
        case 'c':
                  if (scanner.current - scanner.start > 1) {
                      switch (scanner.start[1]) {
                          case 'a': 
                              {
                                  int res = checkKeyword(2, 2, "se", TOKEN_CASE);
                                  if (res == TOKEN_CASE) return res;
                                  return checkKeyword(2, 3, "tch", TOKEN_CATCH);
                              }
                          case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                          case 'o': 
                              {
                                  int res = checkKeyword(2, 3, "nst", TOKEN_CONST);
                                  if (res == TOKEN_CONST) return res;
                                  return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
                              }
                      }
                  }
                  break;
        case 'd': return checkKeyword(1, 6, "efault", TOKEN_DEFAULT);
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
                  if (scanner.current - scanner.start > 1) {
                      switch (scanner.start[1]) {
                          case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                          case 'i': return checkKeyword(2, 5, "nally", TOKEN_FINALLY);
                          case 'o':
                                    {
                                        int res = checkKeyword(2, 5, "reach", TOKEN_FOREACH);
                                        if (res == TOKEN_FOREACH) return res;
                                        return checkKeyword(2, 1, "r", TOKEN_FOR);
                                    }
                          case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
                      }
                  }
                  break;
        case 'i': 
                  if (scanner.current - scanner.start > 1) {
                      switch (scanner.start[1]) {
                          case 'f': if (scanner.current - scanner.start == 2) return TOKEN_IF;
                          case 'm': return checkKeyword(2, 4, "port", TOKEN_IMPORT);
                          case 'n': 
                                    {
                                        int res = checkKeyword(2, 5, "clude", TOKEN_INCLUDE);
                                        if (res == TOKEN_INCLUDE) return res;
                                        if (scanner.current - scanner.start == 2)
                                            return TOKEN_IN;
                                    }
                      }
                  }
                  break;
        case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': 
                  if (scanner.current - scanner.start > 1 && scanner.start[1] == 'e') {
                      if (scanner.current - scanner.start > 2) {
                          switch (scanner.start[2]) {
                              case 't': return checkKeyword(3, 3, "urn", TOKEN_RETURN);
                          }
                      }
                  }
                  break;
        case 's':
                  if (scanner.current - scanner.start > 1) {
                      switch (scanner.start[1]) {
                          case 'u': return checkKeyword(2, 3, "per", TOKEN_SUPER);
                          case 'w': return checkKeyword(2, 4, "itch", TOKEN_SWITCH);
                      }
                  }
                  break;
        case 't':
                  if (scanner.current - scanner.start > 1) {
                      switch (scanner.start[1]) {
                          case 'h': 
                              {
                                  int res = checkKeyword(2, 3, "row", TOKEN_THROW);
                                  if (res == TOKEN_THROW) return res;
                                  return checkKeyword(2, 2, "is", TOKEN_THIS);
                              }
                          case 'r': 
                              {
                                  int res = checkKeyword(2, 1, "y", TOKEN_TRY);
                                  if (res == TOKEN_TRY) return res;
                                  return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                              }
                      }
                  }
                  break;
        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

static Token dot() {
    if (peek() == '.' && peekNext() == '.') {
        advance();
        advance();
        return makeToken(TOKEN_DOT_DOT_DOT);
    }
    return makeToken(TOKEN_DOT);
}

static bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F');
}

static bool isOctalDigit(char c) {
    return c >= '0' && c <= '7';
}

static Token number() {
    if (peek() == '0' && (peekNext() == 'x' || peekNext() == 'X')) {
        advance();
        advance();

        while (isHexDigit(peek())) advance();
        return makeToken(TOKEN_NUMBER);
    }

    if (peek() == '0' && (peekNext() == 'o' || peekNext() == 'O')) {
        advance();
        advance();
        
        while (isOctalDigit(peek())) advance();
        return makeToken(TOKEN_NUMBER);
    }

    while (isDigit(peek())) advance();

    if (peek() == '.' && isDigit(peekNext())) {
        advance();

        while (isDigit(peek())) advance();
    }

    if (peek() == 'e' || peek() == 'E') {
        char next = peekNext();
        if (isDigit(next) || ((next == '+' || next == '-'))) {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!isDigit(peek())) {
                return errorToken("Unterminated exponent.");
            }

            while (isDigit(peek())) advance();
        } else {
            return errorToken("Unterminated exponent.");
        }
    }

    return makeToken(TOKEN_NUMBER);
}

static Token continueString() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;

        if (peek() == '$' && peekNext() == '{') {
            Token token = makeToken(TOKEN_INTERPOLATION);
            advance();
            advance();
            scanner.interpolationDepth++;
            return token;
        }
        if (peek() == '\\') advance();

        advance();

    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    advance();
    return makeToken(TOKEN_STRING);
}

static Token heredocToken() {
    const char* tagStart = scanner.current;
    
    while (isAlpha(peek()) || isDigit(peek())) {
        advance();
    }
    int tagLength = (int)(scanner.current - tagStart);

    if (tagLength == 0) {
        return errorToken("Expect herdoc identifier after '<<<'.");
    }

    if (peek() == '\r') advance();
    if (peek() == '\n') advance();

    const char* bodyStart = scanner.current;

    for (;;) {
        if (isAtEnd()) {
            return errorToken("Unterminated heredoc.");
        }

        if (peek() == '\n' || peek() == '\r') {
            if (peek() == '\r') advance();
            advance();
            scanner.line++;

            if (strncmp(scanner.current, tagStart, tagLength) == 0) {
                char nextChar = scanner.current[tagLength];
                //if (nextChar == ';' || nextChar == '\n' || nextChar == '\r' || nextChar == ' ' || nextChar == '\0') {
                if (!isAlpha(nextChar) && !isDigit(nextChar) && nextChar != '_') {
                    //int bodyLength = (int)((scanner.current - 1) - bodyStart);
                    for (int i = 0; i < tagLength; i++) advance();

                    return makeToken(TOKEN_HEREDOC);
                }
            }
        } else {
            advance();
        }
    }
}

static Token string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;

        if (peek() == '\\') {
            advance();
        }

        if (peek() == '$' && peekNext() == '{') {
            Token token = makeToken(TOKEN_INTERPOLATION);
            advance();
            advance();
            scanner.interpolationDepth++;
            return token;
        }
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    advance();
    return makeToken(TOKEN_STRING);
}

static Token backtickString() {
    while (peek() != '`' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated shell command.");

    advance();
    return makeToken(TOKEN_BACKTICK_STRING);
}

Token peekNextToken() {
    const char* savedStart = scanner.start;
    const char* savedCurrent = scanner.current;
    int savedLine = scanner.line;

    Token nextToken = scanToken();

    scanner.start = savedStart;
    scanner.current = savedCurrent;
    scanner.line = savedLine;

    return nextToken;
}

static Token rawString() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') {
            scanner.line++;
        }
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated raw string.");

    advance();
    return makeToken(TOKEN_RAW_STRING);
}

Token scanToken() {
    skipWhitespace();
    scanner.start = scanner.current;

    bool isstart = scanner.atstartofline;

    scanner.atstartofline = false;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (scanner.interpolationDepth > 0 && c== '}') {
        scanner.interpolationDepth--;
        return continueString();
    }

    if (c == 'r' && peek() == '"') {
        advance();
        return rawString();
    }

    if (isAlpha(c)) {
        Token token = identifier();

        if (token.type == TOKEN_END_MARKER) {
            if (isstart) {
                token.start = scanner.current;
                token.length = (int)strlen(scanner.current);
                scanner.current += token.length;
                return token;
            } else {
                token.type = TOKEN_IDENTIFIER;
            }
        }
        return token;
    }

    if (isDigit(c)) {
        if (c == '0' && (peek() == 'x' || peek() == 'X')) {
            advance();
            while (isHexDigit(peek())) advance();
            return makeToken(TOKEN_NUMBER);
        }
        return number();
    }
    if (c == '\'') {
        char val;
        if (match('\\')) {
            char escape = advance();
            switch (escape) {
                case 'n': val = '\n'; break;
                case 't': val = '\t'; break;
                case 'r': val = '\r'; break;
                case '0': val = '\0'; break;
                case '\'': val = '\''; break;
                case '\\': val = '\\'; break;
                default: val = escape;
            }
        } else {
            val = advance();
        }

        if (!match('\'')) {
            return errorToken("Unterminated character literal.");
        }
        return makeToken(TOKEN_CHAR);
    }

    switch (c) {
        case '&':
            return makeToken(TOKEN_AMPERSAND);
        case '|':
            return makeToken(TOKEN_PIPE);
        case '(':
            return makeToken(TOKEN_LEFT_PAREN);
        case ')':
            return makeToken(TOKEN_RIGHT_PAREN);
        case '{':
            return makeToken(TOKEN_LEFT_BRACE);
        case '}':
            return makeToken(TOKEN_RIGHT_BRACE);
        case '[':
            return makeToken(TOKEN_LEFT_BRACKET);
        case ']':
            return makeToken(TOKEN_RIGHT_BRACKET);
        case ';':
            return makeToken(TOKEN_SEMICOLON);
        case ':':
            return makeToken(TOKEN_COLON);
        case ',':
            return makeToken(TOKEN_COMMA);
        case '.':
            return dot();
            //return makeToken(TOKEN_DOT);
        case '-':
            return makeToken(
                    match('=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS);
        case '+':
            return makeToken(
                    match('=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
        case '/':
            return makeToken(
                    match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
        case '*':
            if (match('*')) return makeToken(TOKEN_STAR_STAR);
            if (match('=')) return makeToken(TOKEN_STAR_EQUAL);
            return makeToken(TOKEN_STAR);
        case '^':
            return makeToken(TOKEN_CARET);
        case '%':
            return makeToken(
                    match('=') ? TOKEN_PERCENT_EQUAL : TOKEN_PERCENT);
        case '!':
            return makeToken(
                    match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(
                    match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            if (match('<')) {
                if (match('<')) {
                    return heredocToken();
                }
                return makeToken(TOKEN_2LEFT);
            }
            return makeToken(
                    match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            if (match('>')) return makeToken(TOKEN_2RIGHT);
            return makeToken(
                    match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '~':
            return makeToken(TOKEN_TILDE);
        case '"':
            return string();
        case '`':
            return backtickString();
        case '?':
            if (match('?')) return makeToken(TOKEN_QQ);
    }

    return errorToken("Unexpected character.");
}

Scanner currentScanner(void) {
    return scanner;
}

void restoreScanner(Scanner saved) {
    scanner = saved;
}
