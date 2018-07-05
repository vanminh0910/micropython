
#pragma once

#include <stdint.h>

void nimble_init();

extern uint8_t *nimble_current_task;
extern uint8_t nimble_started;

#if 1
#define bleprintf(...)
#else
#define bleprintf printf
#endif
