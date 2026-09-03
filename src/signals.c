#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>

#include "vm.h"
#include "native.h"
#include "signals.h"
#include "value.h"
#include "memory.h"

volatile sig_atomic_t pending_signals[MAX_SIGNALS] = {0};
Value signal_callbacks[MAX_SIGNALS];

#ifdef USE_SIGNALFD
#include <sys/signalfd.h>


int setup_signal_fd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    sigprocmask(SIG_BLOCK, &mask, NULL);

    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    return sfd;
}
#endif

static void c_signal_handler(int sig) {
    if (sig > 0 && sig < MAX_SIGNALS) {
        pending_signals[sig] = 1;
    }
}

//@ Signal
//: trap
// Requires:
//   Number: signum
//   Callback: callback
// Returns:
//   Nil
Value lox_signal_trap(int argCount, Value* args) {
    int sig = AS_NUMBER(args[0]);
    Value callback = args[1];

    if (sig <= 0 || sig >= MAX_SIGNALS) {
        // error
        return NIL_VAL;
    }

    signal_callbacks[sig] = callback;

    // register host signal handler
    struct sigaction sa;
    sa.sa_handler = c_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(sig, &sa, NULL);

    return NIL_VAL;
}

bool has_pending_signals(void) {
    for (int i = 1; i < MAX_SIGNALS; i++) {
        if (pending_signals[i]) return true;
    }
    return false;
}

void process_pending_signals() {
    for (int sig = 1; sig < MAX_SIGNALS; sig++) {
        if (pending_signals[sig]) {
            pending_signals[sig] = 0;

            Value cb = signal_callbacks[sig];
            if (!IS_NIL(cb)) {
                push(cb);
                push(NUMBER_VAL(sig));
                callValue(cb, 1);
            }
        }
    }
}

TimerManager timerMgr = { .nextId = 1 };

uint64_t getCurrentTimeMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

//@ Timer
//: _start
// Start timer
// Requires:
//   Number: delayMs
//   Number: intervalMs
//   Value: callback
// Returns:
//   Number: id
Value timerStartNative(int argCount, Value* args) {
    int offset = IS_NUMBER(args[0]) ? 0 : 1;
    if (argCount < offset + 3 || !IS_NUMBER(args[offset]) || !IS_NUMBER(args[offset + 1])) {
        return NUMBER_VAL(-1);
    }

    double delayMs = AS_NUMBER(args[offset]);
    double intervalMs = AS_NUMBER(args[offset + 1]);
    Value callback = args[2];

    if (delayMs < 0) delayMs = 0;

    int slot = -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!timerMgr.entries[i].active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        return NUMBER_VAL(-1);
    }

    int id = timerMgr.nextId++;
    uint64_t now = getCurrentTimeMs();

    timerMgr.entries[slot].id = id;
    timerMgr.entries[slot].fireAtMs = now + (uint64_t)delayMs;
    timerMgr.entries[slot].intervalMs = (intervalMs > 0) ? (uint64_t)intervalMs : 0;
    timerMgr.entries[slot].callback = callback;
    timerMgr.entries[slot].active = true;

    return NUMBER_VAL(id);
}

//@ Timer
//: _stop
// Stop timer
// Requires:
//   Number: handle
// Returns:
//   Bool: Successful or not
Value timerStopNative(int argCount, Value* args) {
    if (argCount < 1) {
        return BOOL_VAL(false);
    }

    Value handleVal = IS_NUMBER(args[0]) ? args[0] : ((argCount > 1 && IS_NUMBER(args[1])) ? args[1] : NIL_VAL);

    if (!IS_NUMBER(handleVal)) {
        return BOOL_VAL(false);
    }

    int handle = (int)AS_NUMBER(handleVal);
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timerMgr.entries[i].active && timerMgr.entries[i].id == handle) {
            timerMgr.entries[i].active = false;
            return BOOL_VAL(true);
        }
    }

    return BOOL_VAL(false);
}

//@ Timer
//: _stopAll
// Stop all timers
Value timerStopAllNative(int argCount, Value* args) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        timerMgr.entries[i].active = false;
    }
    return NIL_VAL;
}

void processTimers(void) {
    uint64_t now = getCurrentTimeMs();

    for (int i = 0; i < MAX_TIMERS; i++) {
        SloxTimer* t = &timerMgr.entries[i];

        if (t->active && now >= t->fireAtMs) {
            int initialFrameCount = vm.frameCount;

            push(t->callback);
            if (callValue(t->callback, 0)) {
                if (vm.frameCount > initialFrameCount) {
                    CallFrame* frame = &vm.frames[vm.frameCount - 1];
                    frame->isTimer = true;
                    run();
                } else {
                    pop();
                }
            }
            //callValue(t->callback, 0);

            if (t->active && t->intervalMs > 0) {
                t->fireAtMs = now + t->intervalMs;
            } else {
                t->active = false;
            }

            break;
        }
    }
}

void initTimerClass(void) {
    ObjClass* timerClass = defineBuiltinClass("Timer", NULL, NULL, true);

    defineNativeMethod(timerClass->obj.klass, "_start", timerStartNative);
    defineNativeMethod(timerClass->obj.klass, "_stop", timerStopNative);
    defineNativeMethod(timerClass->obj.klass, "_stop_all", timerStopAllNative);
}

void markTimerRoots(void) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timerMgr.entries[i].active) {
            markValue(timerMgr.entries[i].callback);
        }
    }
}

uint64_t getNextTimerDelayMs(void) {
    uint64_t now = getCurrentTimeMs();
    uint64_t minFire = UINT64_MAX;

    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timerMgr.entries[i].active) {
            if (timerMgr.entries[i].fireAtMs <= now) return 0;
            if (timerMgr.entries[i].fireAtMs < minFire) {
                minFire = timerMgr.entries[i].fireAtMs;
            }
        }
    }

    return (minFire == UINT64_MAX) ? UINT64_MAX : (minFire - now);
}

bool hasActiveTimers(void) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timerMgr.entries[i].active) return true;
    }
    return false;
}

void awaitTimers(void) {
    while (hasActiveTimers()) {
        uint64_t delay = getNextTimerDelayMs();

        if (delay != UINT64_MAX && delay > 0) {
            if (delay > 20) delay = 20;

            struct timespec req = {
                .tv_sec = (time_t)(delay / 1000),
                .tv_nsec = (long)((delay % 1000) / 1000000)
            };
            nanosleep(&req, NULL);
        }
        processTimers();
    }
}

/*
void runBackend(void) {
    while (hasActiveTimers() || vm.keepBackendAlive) {
        process_pending_signals();
        processTimers();

        if (!hasActiveTimers() && !vm.keepBackendAlive) break;

        uint64_t delayMs = getNextTimerDelayMs();
        if (delayMs > 0 && delayMs != UINT64_MAX) {
            if (delayMs > 50) delayMs = 50;

            struct timespec req = {
                .tv_sec = (time_t)(delayMs / 1000),
                .tv_nsec = (long)((delayMs % 1000) * 1000000)
            };
            nanosleep(&req, NULL);
        }
    }
}
*/
