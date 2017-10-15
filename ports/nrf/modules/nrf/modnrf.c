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
#include "modules/nrf/modnrf.h"
#include "py/runtime.h"
#include "hal/hal_nvmc.h"
#include "extmod/vfs.h"
#include "mpconfigboard.h"

#if MICROPY_PY_NRF

extern byte _flash_user_start[];
extern byte _flash_user_end[];

#define FLASH_BLOCK_START (((uint32_t)_flash_user_start + (HAL_NVMC_PAGESIZE-1)) / HAL_NVMC_PAGESIZE)


mp_obj_t nrf_flashbdev_readblocks(mp_obj_t self_in, mp_obj_t n_in, mp_obj_t buf_in) {
    mp_int_t n = mp_obj_get_int(n_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);

    mp_int_t address = (FLASH_BLOCK_START + n) * HAL_NVMC_PAGESIZE;
    byte *buf = bufinfo.buf;
    byte *p = (byte*)address;
    for (int i=0; i<bufinfo.len; i++) {
        buf[i] = p[i];
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(nrf_flashbdev_readblocks_obj, nrf_flashbdev_readblocks);

mp_obj_t nrf_flashbdev_writeblocks(mp_obj_t self_in, mp_obj_t n_in, mp_obj_t buf_in) {
    mp_int_t n = mp_obj_get_int(n_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);

    mp_int_t address = (FLASH_BLOCK_START + n) * HAL_NVMC_PAGESIZE;
    if (address & 0x3 || bufinfo.len & 0x3) {
        mp_raise_ValueError("invalid address or buffer length");
    }

    // TODO: erase all blocks, not just the first.
    if (!hal_nvmc_erase_page(address)) {
        mp_raise_ValueError("could not erase block");
    }
    if (!hal_nvmc_write_words((uint32_t*)address, bufinfo.buf, bufinfo.len/4)) {
        mp_raise_ValueError("could not write block");
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(nrf_flashbdev_writeblocks_obj, nrf_flashbdev_writeblocks);


mp_obj_t nrf_flashbdev_ioctl(mp_obj_t self_in, mp_obj_t op_in, mp_obj_t arg_in) {
    mp_int_t op = mp_obj_get_int(op_in);
    if (op == BP_IOCTL_SEC_COUNT) {
        mp_int_t block_start = FLASH_BLOCK_START;
        mp_int_t block_end = (uint32_t)_flash_user_end / HAL_NVMC_PAGESIZE;
        mp_int_t num_blocks = block_end - block_start;
        return MP_OBJ_NEW_SMALL_INT(num_blocks);
    }
    if (op == BP_IOCTL_SEC_SIZE) {
        return MP_OBJ_NEW_SMALL_INT(HAL_NVMC_PAGESIZE);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(nrf_flashbdev_ioctl_obj, nrf_flashbdev_ioctl);


STATIC const mp_rom_map_elem_t nrf_flashbdev_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readblocks), MP_ROM_PTR(&nrf_flashbdev_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&nrf_flashbdev_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl), MP_ROM_PTR(&nrf_flashbdev_ioctl_obj) },
};

STATIC MP_DEFINE_CONST_DICT(nrf_flashbdev_locals_dict, nrf_flashbdev_locals_dict_table);

static const mp_obj_type_t nrf_flashbdev_type = {
    { &mp_type_type },
    .name = MP_QSTR_FlashBdev,
    .locals_dict = (mp_obj_dict_t*)&nrf_flashbdev_locals_dict,
};

const nrf_flashbdev_obj_t nrf_flashbdev_obj = {
    { &nrf_flashbdev_type },
};


STATIC const mp_rom_map_elem_t nrf_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),         MP_ROM_QSTR(MP_QSTR_nrf) },
    { MP_ROM_QSTR(MP_QSTR_flashbdev),        MP_ROM_PTR(&nrf_flashbdev_obj) },
};


STATIC MP_DEFINE_CONST_DICT(nrf_module_globals, nrf_module_globals_table);

const mp_obj_module_t nrf_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&nrf_module_globals,
};

#endif // MICROPY_PY_NRF
