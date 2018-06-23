/*
 * This file is part of the MicroPython project, https://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Ayke van Laethem
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>

#include "mphalport.h"
#include "py/ringbuf.h"
#include "lib/utils/interrupt_char.h"

#ifndef NO_QSTR
#include "hal/hal_watchdog.h"
#include "uart/uart.h"
#endif // NO_QSTR

#include "ble.h"

static struct uart_dev *uart_dev;

static uint8_t uart_out_buf[20];
static ringbuf_t uart_out = {uart_out_buf, sizeof(uart_out)};

static uint8_t uart_in_buf[20];
static ringbuf_t uart_in = {uart_in_buf, sizeof(uart_in)};
static struct os_sem uart_in_sem;

// Send a single char. Block when the buffer has been filled.
void mp_hal_stdout_tx_chr(uint8_t c) {
    ble_nus_tx(c);
    while (ringbuf_put(&uart_out, c) < 0) {
        os_sched(NULL);
    }
    uart_start_tx(uart_dev);
}

// Send a C string.
void mp_hal_stdout_tx_str(const char *str) {
    for (size_t i = 0; str[i] != 0; i++) {
        mp_hal_stdout_tx_chr(str[i]);
    }
}

// Send a string of a defined length.
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\n') {
            mp_hal_stdout_tx_chr('\r');
        }
        mp_hal_stdout_tx_chr(str[i]);
    }
}

// Called to send a char, after uart_start_tx() has been called.
static int hal_tx_char_cb(void *arg) {
    // Returns the char, or -1 when finished. This neatly matches the
    // ringbuf API.
    return ringbuf_get(&uart_out);
}

// Pull one char from the input buffer. Blocks until a char is available.
int mp_hal_stdin_rx_chr(void) {
    uart_start_rx(uart_dev);
    while (1) {
        int c = ringbuf_get(&uart_in);
        if (c >= 0) {
            return c;
        }
        os_sem_pend(&uart_in_sem, OS_TIMEOUT_NEVER);
    }
}

// Called when a char arrives over serial.
int hal_rx_char_cb(void *arg, uint8_t c) {
    if (c == mp_interrupt_char) {
        mp_keyboard_interrupt();
    }
    if (os_sem_get_count(&uart_in_sem) == sizeof(uart_in_buf)) {
        return -1; // block input (when using flow control)
    }
    // signal char available
    os_sem_release(&uart_in_sem);
    return ringbuf_put(&uart_in, c);
}

void uart_init() {
    // Initialize a semaphore for the input with initially 0 tokens. The
    // number of tokens is always equal to the length of the input buffer,
    // so as long as it is non-zero there is at least one char availab.e
    os_sem_init(&uart_in_sem, 0);

    // Set up UART with standard configuration.
    struct uart_conf uc = {
        .uc_speed = 115200,
        .uc_databits = 8,
        .uc_stopbits = 1,
        .uc_parity = UART_PARITY_NONE,
        .uc_flow_ctl = MYNEWT_VAL(CONSOLE_UART_FLOW_CONTROL),
        .uc_tx_char = hal_tx_char_cb,
        .uc_rx_char = hal_rx_char_cb,
    };
    uart_dev = (struct uart_dev *)os_dev_open(MYNEWT_VAL(CONSOLE_UART_DEV), OS_TIMEOUT_NEVER, &uc);
}
