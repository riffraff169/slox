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

Value lox_signal_trap(int argCount, Value* args);
void process_pending_signals();
bool has_pending_signals(void);

#endif
