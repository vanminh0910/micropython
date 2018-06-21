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

#include "sysinit/sysinit.h"
#include "os/os.h"
#include "hal/hal_system.h"

#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "lib/utils/pyexec.h"
#include "mphalport.h"

static uint32_t heap[8192 / sizeof(uint32_t)]; // 8kB

static struct os_task main_task;
static os_stack_t main_stack[1024]; // 4kB on 32-bit MCUs

// Main task.
static void main_handler() {
    pyexec_friendly_repl();
    hal_system_reset();
}

int main(int argc, char **argv) {
    // init system
    sysinit();
    uart_init();

    // init MicroPython
    gc_init(heap, heap + MP_ARRAY_SIZE(heap));
    mp_init();

    // start main thread
    os_task_init(&main_task, "main", main_handler, NULL, 8,
            OS_WAIT_FOREVER, main_stack, sizeof(main_stack)/sizeof(main_stack[0]));

    // run OS scheduler
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}

void nlr_jump_fail(void *val) {
    while (1);
}

mp_lexer_t *mp_lexer_new_from_file(const char *filename) {
    mp_raise_OSError(MP_ENOENT);
}

mp_import_stat_t mp_import_stat(const char *path) {
    return MP_IMPORT_STAT_NO_EXIST;
}
