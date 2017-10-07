/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Ayke van Laethem
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
#include <stdint.h>
#include "py/runtime.h"
#include "hal/hal_nvmc.h"

#if MICROPY_PY_NRF

#include "mpconfigboard.h"

extern byte _flash_user_start[];
extern byte _flash_user_end[];

#if defined(NRF51)
#define FLASH_BLOCKSIZE 1024
#elif defined(NRF52)
#define FLASH_BLOCKSIZE 4096
#else
#error Unknown chip
#endif


/// \method flash_user_start()
/// Return start address of user flash (right behind the firmware).
mp_obj_t nrf_flash_user_start(void) {
    return MP_OBJ_NEW_SMALL_INT((uint32_t)_flash_user_start);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(nrf_flash_user_start_obj, nrf_flash_user_start);


/// \method flash_user_end()
/// Return end address of user flash.
mp_obj_t nrf_flash_user_end(void) {
    return MP_OBJ_NEW_SMALL_INT((uint32_t)_flash_user_end);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(nrf_flash_user_end_obj, nrf_flash_user_end);


/// \method flash_read()
/// Read the flash at address into buf.
mp_obj_t nrf_flash_read(mp_obj_t address_in, mp_obj_t buf_in) {
    mp_int_t address = mp_obj_get_int(address_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);

    byte *buf = bufinfo.buf;

    byte *p = (byte*)address;
    for (int i=0; i<bufinfo.len; i++) {
        buf[i] = p[i];
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(nrf_flash_read_obj, nrf_flash_read);


/// \method flash_erase()
/// Erase a single page pointed to by address.
mp_obj_t nrf_flash_erase(mp_obj_t address_in) {
    uint32_t address = mp_obj_get_int(address_in);
    hal_nvmc_erase_page(address);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(nrf_flash_erase_obj, nrf_flash_erase);


/// \method flash_write()
/// Write buf to address.
mp_obj_t nrf_flash_write(mp_obj_t address_in, mp_obj_t buf_in) {
    uint32_t address = mp_obj_get_int(address_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    if (address & 0x3 || bufinfo.len & 0x3) {
        mp_raise_ValueError("invalid address or buffer length");
    }

    hal_nvmc_write_buffer(address, bufinfo.buf, bufinfo.len/4);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(nrf_flash_write_obj, nrf_flash_write);


STATIC const mp_rom_map_elem_t nrf_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),         MP_ROM_QSTR(MP_QSTR_nrf) },
    { MP_ROM_QSTR(MP_QSTR_flash_user_start), MP_ROM_PTR(&nrf_flash_user_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_user_end),   MP_ROM_PTR(&nrf_flash_user_end_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_block_size), MP_ROM_INT(FLASH_BLOCKSIZE) },
    { MP_ROM_QSTR(MP_QSTR_flash_read),       MP_ROM_PTR(&nrf_flash_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_erase),      MP_ROM_PTR(&nrf_flash_erase_obj) },
    { MP_ROM_QSTR(MP_QSTR_flash_write),      MP_ROM_PTR(&nrf_flash_write_obj) },
};


STATIC MP_DEFINE_CONST_DICT(nrf_module_globals, nrf_module_globals_table);

const mp_obj_module_t nrf_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&nrf_module_globals,
};

#endif // MICROPY_PY_NRF
