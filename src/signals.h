#ifndef slox_signal_h
#define slox_signal_h

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "value.h"
#include "object.h"
#include "vm.h"

#define MAX_SIGNALS 32

extern volatile sig_atomic_t pending_signals[MAX_SIGNALS];
extern Value signal_callbacks[MAX_SIGNALS];

#define MAX_TIMERS 256

typedef struct {
    int id;
    uint64_t fireAtMs;
    uint64_t intervalMs;
    Value callback;
    bool active;
} SloxTimer;

typedef struct {
    SloxTimer entries[MAX_TIMERS];
    int nextId;
} TimerManager;

Value lox_signal_trap(int argCount, Value* args);
void process_pending_signals();
bool has_pending_signals(void);
Value timerStopNative(int argCount, Value* args);
Value timerStartNative(int argCount, Value* args);
uint64_t getCurrentTimeMs(void);
void processTimers(void);
void initTimerClass(void);
void markTimerRoots(void);
bool hasActiveTimers(void);
void awaitTimers(void);

#endif
