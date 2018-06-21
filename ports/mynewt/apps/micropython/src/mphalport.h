
#include "mpconfigport.h"
#include "lib/utils/interrupt_char.h"

// Stub out some unimplemented functions.
static inline mp_uint_t mp_hal_ticks_ms(void) { return 0; }

void mp_hal_stdout_tx_str(const char *str);

void uart_init();
