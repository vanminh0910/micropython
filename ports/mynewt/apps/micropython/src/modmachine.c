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

#include "py/obj.h"
#include "hal/hal_system.h"

STATIC mp_obj_t machine_reset(void) {
    hal_system_reset();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(machine_reset_obj, machine_reset);

STATIC mp_obj_t machine_reset_cause(void) {
    return MP_OBJ_NEW_SMALL_INT(hal_reset_cause());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(machine_reset_cause_obj, machine_reset_cause);

STATIC const mp_rom_map_elem_t machine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_umachine) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&machine_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_cause), MP_ROM_PTR(&machine_reset_cause_obj) },

    // reset causes
    { MP_ROM_QSTR(MP_QSTR_PWRON_RESET), MP_ROM_INT(HAL_RESET_POR) },
    { MP_ROM_QSTR(MP_QSTR_HARD_RESET), MP_ROM_INT(HAL_RESET_PIN) },
    { MP_ROM_QSTR(MP_QSTR_WDT_RESET), MP_ROM_INT(HAL_RESET_WATCHDOG) },
    { MP_ROM_QSTR(MP_QSTR_SOFT_RESET), MP_ROM_INT(HAL_RESET_SOFT) },
    { MP_ROM_QSTR(MP_QSTR_BROWNOUT_RESET), MP_ROM_INT(HAL_RESET_BROWNOUT) },
};

STATIC MP_DEFINE_CONST_DICT(machine_module_globals, machine_module_globals_table);

const mp_obj_module_t mp_module_machine = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&machine_module_globals,
};
