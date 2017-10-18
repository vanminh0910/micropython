/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Damien P. George
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

#include <stdio.h>
#include <string.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/gc.h"
#include "lib/mp-readline/readline.h"
#include "lib/utils/pyexec.h"
#include "gccollect.h"
#include "user_interface.h"
#include "py/persistentcode.h"

STATIC char heap[36 * 1024];

STATIC void flash_init();

STATIC void mp_reset(void) {
    mp_stack_set_top((void*)0x40000000);
    mp_stack_set_limit(8192);
    mp_hal_init();
    gc_init(heap, heap + sizeof(heap));
    mp_init();
    flash_init();
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_)); // current dir (or base dir of the script)
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_));
    mp_obj_list_init(mp_sys_argv, 0);
    #if MICROPY_EMIT_XTENSA || MICROPY_EMIT_INLINE_XTENSA
    extern void esp_native_code_init(void);
    esp_native_code_init();
    #endif
    pin_init0();
    readline_init0();
    dupterm_task_init();
#if MICROPY_MODULE_FROZEN
    pyexec_frozen_module("_boot.py");
    pyexec_file("boot.py");
    if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL) {
        pyexec_file("main.py");
    }
#endif
}

void soft_reset(void) {
    mp_hal_stdout_tx_str("PYB: soft reboot\r\n");
    mp_hal_delay_us(10000); // allow UART to flush output
    mp_reset();
    #if MICROPY_REPL_EVENT_DRIVEN
    pyexec_event_repl_init();
    #endif
}

void init_done(void) {
    #if MICROPY_REPL_EVENT_DRIVEN
    uart_task_init();
    #endif
    mp_reset();
    mp_hal_stdout_tx_str("\r\n");
    #if MICROPY_REPL_EVENT_DRIVEN
    pyexec_event_repl_init();
    #endif

    #if !MICROPY_REPL_EVENT_DRIVEN
soft_reset:
    for (;;) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        } else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }
    soft_reset();
    goto soft_reset;
    #endif
}

void user_init(void) {
    system_init_done_cb(init_done);
}

#if !MICROPY_VFS
mp_lexer_t *mp_lexer_new_from_file(const char *filename) {
    mp_raise_OSError(MP_ENOENT);
}

mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

#endif

void MP_FASTCODE(nlr_jump_fail)(void *val) {
    printf("NLR jump failed\n");
    for (;;) {
    }
}

//void __assert(const char *file, int line, const char *func, const char *expr) {
void __assert(const char *file, int line, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    for (;;) {
    }
}

#define PAGESIZE (4096)
#define BEGIN_PAGE(p) (void*)(((uintptr_t)p) & ~(uintptr_t)(PAGESIZE - 1))
#define END_PAGE(p) (void*)((((uintptr_t)p) + PAGESIZE - 1) & ~(uintptr_t)(PAGESIZE - 1))
#define NEXT_PAGE_START(p) (void*)((((uintptr_t)p) + PAGESIZE) & ~(uintptr_t)(PAGESIZE - 1))
#define FLASH_ADDR(p) ((uint32_t)p - 0x40200000)
#define PAGE_NUM(p) (FLASH_ADDR(p) / PAGESIZE)

STATIC uint32_t *next_word_addr = 0;
extern uint32_t _firmware_size[];

STATIC void flash_init() {
    // Find the address of the first page after the ROM end.
    next_word_addr = END_PAGE(&_irom0_text_end);
}

STATIC void write_chunk(uint32_t *words, uint32_t *begin, uint32_t *end) {
    // Skip all words that are the same already.
    while (begin != end && *begin == *words) {
        begin++;
        words++;
    }

    if (begin == end) {
        // Nothing to do.
        return;
    }

    // Check whether the flash is cleared.
    bool needs_erase = false;
    for (uint32_t *p=begin; p<end; p++) {
        if (*p != 0xffffffff) {
            needs_erase = true;
        }
    }

    SpiFlashOpResult res;

    if (needs_erase) {
        // Backup existing data
        uint32_t *page_start = BEGIN_PAGE(begin);
        size_t existing_size = ((uint32_t)begin - (uint32_t)page_start) / 4;
        uint32_t backup[existing_size];
        for (uint32_t i=0; i<existing_size; i++) {
            backup[i] = page_start[i];
        }

        // Erase to be written page
        res = spi_flash_erase_sector(PAGE_NUM(page_start));
        if (res != SPI_FLASH_RESULT_OK) {
            mp_raise_OSError(MP_EIO);
        }

        // Write back backed-up data
        res = spi_flash_write(FLASH_ADDR(page_start), backup, existing_size*4);
        if (res != SPI_FLASH_RESULT_OK) {
            mp_raise_OSError(MP_EIO);
        }
    }

    // Now finally write the data.
    res = spi_flash_write(FLASH_ADDR(begin), words, (uint32_t)end - (uint32_t)begin);
    if (res != SPI_FLASH_RESULT_OK) {
        mp_raise_OSError(MP_EIO);
    }
}

void *mp_flash_write_words(uint32_t *words, size_t len) {
    uint32_t *start_addr = next_word_addr;
    uint32_t *end_addr = next_word_addr + len;

    if (FLASH_ADDR(end_addr) > (uint32_t)_firmware_size) {
        // _firmware_size really refers to the first flash address that is
        // used by the filesystem. So we can use the area between
        // _irom0_text_end and _firmware_size.
        mp_raise_OSError(MP_ENOSPC);
    }

    while (next_word_addr != end_addr) {
        // Calculate begin and end addresses of this chunk.
        uint32_t *end;
        if ((uint32_t)next_word_addr / PAGESIZE != (uint32_t)end_addr / PAGESIZE) {
            // area crosses page boundary
            end = NEXT_PAGE_START(next_word_addr);
        } else {
            // area is within one page
            end = end_addr;
        }
        write_chunk(words, next_word_addr, end);
        next_word_addr = end;
    }

    return start_addr;
}
