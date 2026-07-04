#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

static void repl() {
    char* line = NULL;
    char* history_file = ".slox_history";
    using_history();
    read_history(history_file);

    for (;;) {
        if (!(line = readline("> ")))
            break;

        HISTORY_STATE* myhist = history_get_history_state();
        HIST_ENTRY **mylist = history_list();

        if (myhist->length == 0 || strcmp(line, mylist[myhist->length - 1]->line) != 0) {
            add_history(line);
        }

        interpret(line, "repl");
        free(line);
        free(myhist);
    }
    write_history(history_file);
}

char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        return NULL;
        //exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

char* locateAndReadStdlib() {
    const char* localPath = "./lib/stdlib.lox";
    if (access(localPath, R_OK) == 0) {
        return readFile(localPath);
    }

    const char* home = getenv("HOME");
    if (home != NULL) {
        char pathBuffer[PATH_MAX];

        snprintf(pathBuffer, sizeof(pathBuffer), "%s/.local/share/slox/lib/stdlib.lox", home);
        if (access(pathBuffer, R_OK) == 0) {
            return readFile(pathBuffer);
        }

        snprintf(pathBuffer, sizeof(pathBuffer), "%s/.local/slox/lib/stdlib.lox", home);
        if (access(pathBuffer, R_OK) == 0) {
            return readFile(pathBuffer);
        }
    }

    const char* systemPath = "/usr/local/lib/slox/stdlib.lox";
    if (access(systemPath, R_OK) == 0) {
        return readFile(systemPath);
    }

    return NULL;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    if (source == NULL) return;

    InterpretResult result = interpret(source, path);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main(int argc, const char* argv[], const char* env[]) {
    initVM(argc, argv, env);

    char* stdlibSource = locateAndReadStdlib();
    if (stdlibSource != NULL) {
        interpret(stdlibSource, "stdlib.lox");
        free(stdlibSource);
    } else {
        printf("Warning: stdlib.lox not found. Proceeding with clean environment.\n");
    }

    if (argc == 1) {
        repl();
    } else { /*if (argc == 2) {*/
        runFile(argv[1]);
    } /*else {
        fprintf(stderr, "Usage: slox [path]\n");
        exit(64);
    }
    */

    freeVM();
    return 0;
}
