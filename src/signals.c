#include <signal.h>
#include <stdio.h>
#include <stdbool.h>

#include "signals.h"

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
