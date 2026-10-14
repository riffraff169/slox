#ifndef slox_common_h
#define slox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#define UINT8_COUNT (UINT8_MAX + 1)

char* locateAndReadLoxFile(const char* name);

#endif
