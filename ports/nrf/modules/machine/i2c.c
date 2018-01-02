/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Glenn Ruben Bakke
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

#include "py/nlr.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "extmod/machine_i2c.h"
#include "i2c.h"
#include "nrfx_twi.h"

#if MICROPY_PY_MACHINE_I2C

STATIC const mp_obj_type_t machine_hard_i2c_type;

typedef struct _machine_hard_i2c_obj_t {
    mp_obj_base_t     base;
    uint8_t           id;     // TWI instance id
    nrfx_twi_t *      p_twi;  // Driver instance
    nrfx_twi_config_t config; // TWI config
} machine_hard_i2c_obj_t;

static nrfx_twi_t instance0 = NRFX_TWI_INSTANCE(0);
static nrfx_twi_t instance1 = NRFX_TWI_INSTANCE(1);

STATIC machine_hard_i2c_obj_t machine_hard_i2c_obj[] = {
    {{&machine_hard_i2c_type}, .p_twi = &instance0},
    {{&machine_hard_i2c_type}, .p_twi = &instance1}
};

void i2c_init0(void) {
    // reset the I2C handles
    memset(&machine_hard_i2c_obj[0].config, 0, sizeof(nrfx_twi_config_t));
    machine_hard_i2c_obj[0].id = 0;

    memset(&machine_hard_i2c_obj[1].config, 0, sizeof(nrfx_twi_config_t));
    machine_hard_i2c_obj[1].id = 1;
}

STATIC int i2c_find(mp_obj_t id) {
    // given an integer id
    int i2c_id = mp_obj_get_int(id);
    if (i2c_id >= 0 && i2c_id <= MP_ARRAY_SIZE(machine_hard_i2c_obj)
        && machine_hard_i2c_obj[i2c_id].p_twi != NULL) {
        return i2c_id;
    }
    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
        "I2C(%d) does not exist", i2c_id));
}

STATIC void machine_hard_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_hard_i2c_obj_t *self = self_in;
    mp_printf(print, "I2C(%u, scl_pin=%u, sda_pin=%u, frequency=%lu, irq_prio=%u)",
              self->id,
              self->config.scl,
              self->config.sda,
              self->config.frequency,
              self->config.interrupt_priority);
}

/******************************************************************************/
/* MicroPython bindings for machine API                                       */

// for make_new
enum {
    ARG_NEW_id,
    ARG_NEW_scl,
    ARG_NEW_sda,
    ARG_NEW_freq,
    ARG_NEW_timeout,
};

mp_obj_t machine_hard_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    static const mp_arg_t allowed_args[] = {
        { ARG_NEW_id,       MP_ARG_REQUIRED | MP_ARG_OBJ },
        { ARG_NEW_scl,      MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { ARG_NEW_sda,      MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // get static peripheral object
    int i2c_id = i2c_find(args[ARG_NEW_id].u_obj);
    machine_hard_i2c_obj_t *self = &machine_hard_i2c_obj[i2c_id];

    if (args[ARG_NEW_scl].u_obj != MP_OBJ_NULL) {
        const pin_obj_t * p_scl_obj = args[ARG_NEW_scl].u_obj;
        self->config.scl = p_scl_obj->pin;
    } else {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
                  "I2C SCL Pin not set"));
    }

    if (args[ARG_NEW_sda].u_obj != MP_OBJ_NULL) {
        const pin_obj_t * p_sda_obj = args[ARG_NEW_sda].u_obj;
        self->config.sda = p_sda_obj->pin;
    } else {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
                  "I2C SDA Pin not set"));
    }

    self->config.frequency = NRF_TWI_FREQ_100K;

    // Set context to this object.
    nrfx_twi_init(self->p_twi, &self->config, NULL, (void *)self);

    return MP_OBJ_FROM_PTR(self);
}

int machine_hard_i2c_readfrom(mp_obj_base_t *self_in, uint16_t addr, uint8_t *dest, size_t len, bool stop) {
    machine_hard_i2c_obj_t *self = (machine_hard_i2c_obj_t *)self_in;

    nrfx_err_t err_code = nrfx_twi_rx(self->p_twi, addr, dest, len);

    if (err_code != NRFX_SUCCESS) {
        return -err_code;
    }

    return 0;
}

int machine_hard_i2c_writeto(mp_obj_base_t *self_in, uint16_t addr, const uint8_t *src, size_t len, bool stop) {
    machine_hard_i2c_obj_t *self = (machine_hard_i2c_obj_t *)self_in;

    nrfx_err_t err_code = nrfx_twi_tx(self->p_twi, addr, src, len, !stop);

    if (err_code != NRFX_SUCCESS) {
         return -err_code;
    }

    return 0;
}

STATIC const mp_machine_i2c_p_t machine_hard_i2c_p = {
    .readfrom = machine_hard_i2c_readfrom,
    .writeto = machine_hard_i2c_writeto,
};

STATIC const mp_obj_type_t machine_hard_i2c_type = {
    { &mp_type_type },
    .name = MP_QSTR_I2C,
    .print = machine_hard_i2c_print,
    .make_new = machine_hard_i2c_make_new,
    .protocol = &machine_hard_i2c_p,
    .locals_dict = (mp_obj_dict_t*)&mp_machine_soft_i2c_locals_dict,
};

#endif // MICROPY_PY_MACHINE_I2C
