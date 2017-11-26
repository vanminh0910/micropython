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

#include "py/obj.h"
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/misc.h"

#if MICROPY_PY_UBLUEPY

#include "modubluepy.h"
#include "ble_drv.h"

STATIC void ubluepy_uuid_print(const mp_print_t *print, mp_obj_t o, mp_print_kind_t kind) {
    ubluepy_uuid_obj_t * self = (ubluepy_uuid_obj_t *)o;
    if (self->type == UBLUEPY_UUID_16_BIT) {
        mp_printf(print, "UUID(uuid: 0x" HEX2_FMT ")",
                  self->value);
    } else {
        mp_printf(print, "UUID(uuid: 0x" HEX2_FMT ", VS idx: " HEX2_FMT ")",
                  self->value, self->uuid_vs_idx);
    }
}

STATIC mp_obj_t ubluepy_uuid_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {

    enum { ARG_NEW_UUID };

    static const mp_arg_t allowed_args[] = {
        { ARG_NEW_UUID, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    ubluepy_uuid_obj_t *s = m_new_obj(ubluepy_uuid_obj_t);
    s->base.type = type;

    mp_obj_t uuid_obj = args[ARG_NEW_UUID].u_obj;

    if (uuid_obj == MP_OBJ_NULL) {
        return MP_OBJ_FROM_PTR(s);
    }

    if (MP_OBJ_IS_INT(uuid_obj)) {
        s->type = UBLUEPY_UUID_16_BIT;
        s->value = (uint8_t)mp_obj_get_int(uuid_obj);
    } else if (MP_OBJ_IS_STR(uuid_obj)) {
        GET_STR_DATA_LEN(uuid_obj, str_data, str_len);
        if (str_len == 6) { // Assume hex digit prefixed with 0x
            s->type = UBLUEPY_UUID_16_BIT;
            s->value  = unichar_xdigit_value(str_data[5]);
            s->value += unichar_xdigit_value(str_data[4]) << 4;
            s->value += unichar_xdigit_value(str_data[3]) << 8;
            s->value += unichar_xdigit_value(str_data[2]) << 12;
        } else if (str_len == 36) {
            s->type = UBLUEPY_UUID_128_BIT;
            uint8_t buffer[16];
            buffer[0]  = unichar_xdigit_value(str_data[35]);
            buffer[0] += unichar_xdigit_value(str_data[34]) << 4;
            buffer[1]  = unichar_xdigit_value(str_data[33]);
            buffer[1] += unichar_xdigit_value(str_data[32]) << 4;
            buffer[2]  = unichar_xdigit_value(str_data[31]);
            buffer[2] += unichar_xdigit_value(str_data[30]) << 4;
            buffer[3]  = unichar_xdigit_value(str_data[29]);
            buffer[3] += unichar_xdigit_value(str_data[28]) << 4;
            buffer[4]  = unichar_xdigit_value(str_data[27]);
            buffer[4] += unichar_xdigit_value(str_data[26]) << 4;
            buffer[5]  = unichar_xdigit_value(str_data[25]);
            buffer[5] += unichar_xdigit_value(str_data[24]) << 4;
            // 23 '-'
            buffer[6]  = unichar_xdigit_value(str_data[22]);
            buffer[6] += unichar_xdigit_value(str_data[21]) << 4;
            buffer[7]  = unichar_xdigit_value(str_data[20]);
            buffer[7] += unichar_xdigit_value(str_data[19]) << 4;
            // 18 '-'
            buffer[8]  = unichar_xdigit_value(str_data[17]);
            buffer[8] += unichar_xdigit_value(str_data[16]) << 4;
            buffer[9]  = unichar_xdigit_value(str_data[15]);
            buffer[9] += unichar_xdigit_value(str_data[14]) << 4;
            // 13 '-'
            buffer[10]  = unichar_xdigit_value(str_data[12]);
            buffer[10] += unichar_xdigit_value(str_data[11]) << 4;
            buffer[11]  = unichar_xdigit_value(str_data[10]);
            buffer[11] += unichar_xdigit_value(str_data[9]) << 4;
            // 8 '-'
            // 16-bit field
            s->value  = unichar_xdigit_value(str_data[7]);
            s->value += unichar_xdigit_value(str_data[6]) << 4;
            s->value += unichar_xdigit_value(str_data[5]) << 8;
            s->value += unichar_xdigit_value(str_data[4]) << 12;

            buffer[14]  = unichar_xdigit_value(str_data[3]);
            buffer[14] += unichar_xdigit_value(str_data[2]) << 4;
            buffer[15]  = unichar_xdigit_value(str_data[1]);
            buffer[15] += unichar_xdigit_value(str_data[0]) << 4;

            ble_drv_uuid_add_vs(buffer, &s->uuid_vs_idx);
        } else {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
                      "Invalid UUID string length"));
        }
    } else if (MP_OBJ_IS_TYPE(uuid_obj, &ubluepy_uuid_type)) {
        // deep copy instance
        ubluepy_uuid_obj_t * p_old = MP_OBJ_TO_PTR(uuid_obj);
        s->type  = p_old->type;
        s->value = p_old->value;
    } else {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
                  "Invalid UUID parameter"));
    }

    return MP_OBJ_FROM_PTR(s);
}

/// \method binVal()
/// Get binary value of the 16 or 128 bit UUID. Returned as bytearray type.
///
STATIC mp_obj_t uuid_bin_val(mp_obj_t self_in) {
    ubluepy_uuid_obj_t * self = MP_OBJ_TO_PTR(self_in);

    // TODO: Extend the uint16 byte value to 16 byte if 128-bit,
    //       also encapsulate it in a bytearray. For now, return
    //       the uint16_t field of the UUID.
    return MP_OBJ_NEW_SMALL_INT(self->value);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(ubluepy_uuid_bin_val_obj, uuid_bin_val);

STATIC const mp_rom_map_elem_t ubluepy_uuid_locals_dict_table[] = {
#if 0
    { MP_ROM_QSTR(MP_QSTR_getCommonName), MP_ROM_PTR(&ubluepy_uuid_get_common_name_obj) },
#endif
    // Properties
    { MP_ROM_QSTR(MP_QSTR_binVal), MP_ROM_PTR(&ubluepy_uuid_bin_val_obj) },
};

STATIC MP_DEFINE_CONST_DICT(ubluepy_uuid_locals_dict, ubluepy_uuid_locals_dict_table);

const mp_obj_type_t ubluepy_uuid_type = {
    { &mp_type_type },
    .name = MP_QSTR_UUID,
    .print = ubluepy_uuid_print,
    .make_new = ubluepy_uuid_make_new,
    .locals_dict = (mp_obj_dict_t*)&ubluepy_uuid_locals_dict
};

#endif // MICROPY_PY_UBLUEPY
