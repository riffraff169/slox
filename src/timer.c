#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

#include "vm.h"
#include "value.h"
#include "object.h"
#include "memory.h"

#define MAX_TIMERS 16

typedef struct {
    timer_t timerid;
    Value callback;
    bool active;
    bool periodic;
    volatile sig_atomic_t fired;
} LoxTimer;

static LoxTimer timer_pool[MAX_TIMERS];
static bool signal_handler_installed = false;

// Async-signal-safe handler: sets flag for main thread to process
static void sigalrm_handler(int sig, siginfo_t* si, void* uc) {
    int timer_id = si->si_value.sival_int;
    if (timer_id >= 0 && timer_id < MAX_TIMERS && timer_pool[timer_id].active) {
        timer_pool[timer_id].fired++;
    }
}

static void ensure_signal_handler(void) {
    if (signal_handler_installed) return;

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = sigalrm_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    for (int i = 0; i < MAX_TIMERS; i++) {
        timer_pool[i].active = false;
        timer_pool[i].fired = 0;
    }
    signal_handler_installed = true;
}

// Native: Timer._start(delay_ms, interval_ms, callback) -> handle_id
Value timerStartNative(int argCount, Value* args) {
    if (argCount < 3 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError("Timer._start expects (delay_ms, interval_ms, callback).");
        return NUMBER_VAL(-1);
    }

    ensure_signal_handler();

    int slot = -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!timer_pool[i].active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        runtimeError("Maximum concurrent timers (%d) exceeded.", MAX_TIMERS);
        return NUMBER_VAL(-1);
    }

    double delay_ms = AS_NUMBER(args[0]);
    double interval_ms = AS_NUMBER(args[1]);

    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    sev.sigev_value.sival_int = slot;

    if (timer_create(CLOCK_MONOTONIC, &sev, &timer_pool[slot].timerid) != 0) {
        runtimeError("Failed to create POSIX timer.");
        return NUMBER_VAL(-1);
    }

    struct itimerspec its;
    // Initial expiry
    its.it_value.tv_sec = (time_t)(delay_ms / 1000.0);
    its.it_value.tv_nsec = (long)((delay_ms - (its.it_value.tv_sec * 1000.0)) * 1000000.0);

    // Repetitive interval
    its.it_interval.tv_sec = (time_t)(interval_ms / 1000.0);
    its.it_interval.tv_nsec = (long)((interval_ms - (its.it_interval.tv_sec * 1000.0)) * 1000000.0);

    timer_pool[slot].callback = args[2];
    timer_pool[slot].active = true;
    timer_pool[slot].periodic = (interval_ms > 0);
    timer_pool[slot].fired = 0;

    if (timer_settime(timer_pool[slot].timerid, 0, &its, NULL) != 0) {
        timer_delete(timer_pool[slot].timerid);
        timer_pool[slot].active = false;
        runtimeError("Failed to arm POSIX timer.");
        return NUMBER_VAL(-1);
    }

    return NUMBER_VAL(slot);
}

// Native: Timer._stop(handle_id)
Value timerStopNative(int argCount, Value* args) {
    if (argCount < 1 || !IS_NUMBER(args[0])) return BOOL_VAL(false);

    int slot = (int)AS_NUMBER(args[0]);
    if (slot < 0 || slot >= MAX_TIMERS || !timer_pool[slot].active) {
        return BOOL_VAL(false);
    }

    timer_delete(timer_pool[slot].timerid);
    timer_pool[slot].active = false;
    timer_pool[slot].fired = 0;
    return BOOL_VAL(true);
}

// GC Root Tracing Hook (memory.c)
void markTimerRoots(void) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timer_pool[i].active) {
            markValue(timer_pool[i].callback);
        }
    }
}
