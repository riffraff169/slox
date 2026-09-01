#ifndef slox_signal_h
#define slox_signal_h

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "value.h"
#include "object.h"
#include "vm.h"

Value lox_signal_trap(int argCount, Value* args);


#endif
