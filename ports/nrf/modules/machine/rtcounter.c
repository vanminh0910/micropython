/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Glenn Ruben Bakke
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
#include "rtcounter.h"
#include "nrfx_rtc.h"

#if MICROPY_PY_MACHINE_RTCOUNTER

#define LFCLK_FREQ (32768UL)
#define RTC_FREQ (10UL)
#define RTC_COUNTER_PRESCALER ((LFCLK_FREQ/RTC_FREQ)-1)

typedef struct _machine_rtc_obj_t {
    mp_obj_base_t      base;
    uint8_t            id;     // RTC (Real-Time Counter) instance id
    nrfx_rtc_t *       p_rtc;  // Driver instance
    nrfx_rtc_config_t  config; // RTC configuration
    nrfx_rtc_handler_t rtc_interrupt_cb;
    mp_obj_t           callback;
    mp_int_t           period;
    mp_int_t           mode;
} machine_rtc_obj_t;

static nrfx_rtc_t instance0 = NRFX_RTC_INSTANCE(0);
static nrfx_rtc_t instance1 = NRFX_RTC_INSTANCE(1);
#if NRF52
static nrfx_rtc_t instance2 = NRFX_RTC_INSTANCE(2);
#endif

// Forward declare interrupt handlers for the instances
STATIC void rtc_interrupt_handle0(uint8_t instance);
STATIC void rtc_interrupt_handle1(uint8_t instance);
STATIC void rtc_interrupt_handle2(uint8_t instance);

STATIC machine_rtc_obj_t machine_rtc_obj[] = {
    {{&machine_rtcounter_type}, .p_rtc = &instance0, .rtc_interrupt_cb = rtc_interrupt_handle0},
    {{&machine_rtcounter_type}, .p_rtc = &instance1, .rtc_interrupt_cb = rtc_interrupt_handle1},
#if NRF52
    {{&machine_rtcounter_type}, .p_rtc = &instance2, .rtc_interrupt_cb = rtc_interrupt_handle2},
#endif
};

STATIC void rtc_interrupt_handle0(uint8_t instance) {
    machine_rtc_obj_t * self = &machine_rtc_obj[0];

    mp_call_function_1(self->callback, self);

    if (self != NULL) {
        nrfx_rtc_disable(self->p_rtc);
        if (self->mode == 1) {
            nrfx_rtc_enable(self->p_rtc);
        }
    }
}

STATIC void rtc_interrupt_handle1(uint8_t instance) {
    machine_rtc_obj_t * self = &machine_rtc_obj[1];

    mp_call_function_1(self->callback, self);

    if (self != NULL) {
        nrfx_rtc_disable(self->p_rtc);
        if (self->mode == 1) {
            nrfx_rtc_enable(self->p_rtc);
        }
    }
}

STATIC void rtc_interrupt_handle2(uint8_t instance) {
    machine_rtc_obj_t * self = &machine_rtc_obj[2];

    mp_call_function_1(self->callback, self);

    if (self != NULL) {
        nrfx_rtc_disable(self->p_rtc);
        if (self->mode == 1) {
            nrfx_rtc_enable(self->p_rtc);
        }
    }
}

void rtc_init0(void) {
}

STATIC int rtc_find(mp_obj_t id) {
    // given an integer id
    int rtc_id = mp_obj_get_int(id);
    if (rtc_id >= 0 && rtc_id <= MP_ARRAY_SIZE(machine_rtc_obj)
        && machine_rtc_obj[rtc_id].p_rtc != NULL) {
        return rtc_id;
    }
    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
        "RTCounter(%d) does not exist", rtc_id));
}

STATIC void rtc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_rtc_obj_t *self = self_in;
    mp_printf(print, "RTCounter(%u)", self->id);
}

/******************************************************************************/
/* MicroPython bindings for machine API                                       */

STATIC mp_obj_t machine_rtc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,       MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(-1)} },
        { MP_QSTR_period,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000} },
        { MP_QSTR_mode,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_callback, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // get static peripheral object
    int rtc_id = rtc_find(args[0].u_obj);

    // unconst machine object in order to set a callback.
    machine_rtc_obj_t * self = (machine_rtc_obj_t *)&machine_rtc_obj[rtc_id];

    nrfx_rtc_config_t * config = &self->config;

    config->prescaler    = RTC_COUNTER_PRESCALER;
    config->reliable     = 0;
    config->tick_latency = NRFX_RTC_US_TO_TICKS(7000, 32768UL);

#ifdef NRF51
    config->interrupt_priority = 3;
#else
    config->interrupt_priority = 6;
#endif

    // Periodic or one-shot
    self->mode = args[2].u_int;

    // Period between the intervals
    self->period = args[1].u_int;

    if (args[3].u_obj != mp_const_none) {
        self->callback = args[3].u_obj;
    }

    nrfx_rtc_init(self->p_rtc, config, self->rtc_interrupt_cb);
    nrfx_rtc_cc_set(self->p_rtc, 0 /*channel*/, self->period, true /*enable irq*/);

    return MP_OBJ_FROM_PTR(self);
}

/// \method start(period)
/// Start the RTCounter. Timeout occurs after number of periods
/// in the configured frequency has been reached.
///
STATIC mp_obj_t machine_rtc_start(mp_obj_t self_in) {
    machine_rtc_obj_t * self = MP_OBJ_TO_PTR(self_in);

    nrfx_rtc_enable(self->p_rtc);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_rtc_start_obj, machine_rtc_start);

/// \method stop()
/// Stop the RTCounter.
///
STATIC mp_obj_t machine_rtc_stop(mp_obj_t self_in) {
    machine_rtc_obj_t * self = MP_OBJ_TO_PTR(self_in);

    nrfx_rtc_disable(self->p_rtc);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_rtc_stop_obj, machine_rtc_stop);


STATIC const mp_rom_map_elem_t machine_rtc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&machine_rtc_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),  MP_ROM_PTR(&machine_rtc_stop_obj) },

    // constants
    { MP_ROM_QSTR(MP_QSTR_ONESHOT),  MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_PERIODIC), MP_ROM_INT(1) },
};

STATIC MP_DEFINE_CONST_DICT(machine_rtc_locals_dict, machine_rtc_locals_dict_table);

const mp_obj_type_t machine_rtcounter_type = {
    { &mp_type_type },
    .name = MP_QSTR_RTCounter,
    .print = rtc_print,
    .make_new = machine_rtc_make_new,
    .locals_dict = (mp_obj_dict_t*)&machine_rtc_locals_dict
};

#endif // MICROPY_PY_MACHINE_RTCOUNTER
