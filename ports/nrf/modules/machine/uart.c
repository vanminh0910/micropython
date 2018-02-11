/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2015 Glenn Ruben Bakke
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

#include "py/nlr.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "pin.h"
#include "genhdr/pins.h"

#include "uart.h"
#include "mpconfigboard.h"
#include "nrf.h"
#include "mphalport.h"
#include "nrf_uart.h"
#include "lib/utils/interrupt_char.h"

typedef struct {
    bool            tx_started;
    volatile int8_t rxbuf_rpos;
    volatile int8_t rxbuf_wpos;
    uint8_t         rxbuf_buf[8];
} uart_data_t;

#if MICROPY_PY_MACHINE_UART

typedef struct _machine_hard_uart_obj_t {
    mp_obj_base_t   base;
    NRF_UART_Type * p_reg;
    uart_data_t   * data;
} machine_hard_uart_obj_t;

STATIC uart_data_t uart0_data;
#if NRF52840_XXAA
STATIC uart_data_t uart1_data;
#endif

STATIC const machine_hard_uart_obj_t machine_hard_uart_obj[] = {
    {{&machine_hard_uart_type}, .p_reg = NRF_UART0, .data=&uart0_data},
#if NRF52840_XXAA
    {{&machine_hard_uart_type}, .p_reg = NRF_UART1, .data=&uart1_data},
#endif
};

void uart_init0(void) {
}

STATIC int uart_find(mp_obj_t id) {
    // given an integer id
    int uart_id = mp_obj_get_int(id);
    if (uart_id >= 0 && uart_id < MP_ARRAY_SIZE(machine_hard_uart_obj)) {
        return uart_id;
    }
    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
              "UART(%d) does not exist", uart_id));
}

STATIC void uart_irq_handler(const machine_hard_uart_obj_t *self) {
    int8_t wpos = self->data->rxbuf_wpos;
    if (wpos < 0) {
        // Wait until the error is handled.
        // TODO: can it get stuck this way?
        return;
    }

    if (nrf_uart_event_check(self->p_reg, NRF_UART_EVENT_RXDRDY)) {
        nrf_uart_event_clear(self->p_reg, NRF_UART_EVENT_RXDRDY);

        int8_t wpos_next = (wpos + 1) % sizeof(self->data->rxbuf_buf);

        // Is the buffer full?
        if (self->data->rxbuf_rpos == wpos_next) {
            // Yes, drop the oldest byte in the buffer by moving the read
            // position one forward.
            self->data->rxbuf_rpos = (wpos_next + 1) % sizeof(self->data->rxbuf_buf);
        }

        uint8_t ch = nrf_uart_rxd_get(self->p_reg);

        #if MICROPY_KBD_EXCEPTION
        if (ch == mp_interrupt_char) {
            // Signal a KeyboardInterrupt
            mp_keyboard_interrupt();

            // clear ringbuffer
            self->data->rxbuf_rpos = wpos;
        } else
        #endif
        {
            // Add the received byte to the ringbuffer.
            self->data->rxbuf_buf[wpos] = ch;
            self->data->rxbuf_wpos = wpos_next;
        }
    }

    // Handle UART receive errors
    if (nrf_uart_event_check(self->p_reg, NRF_UART_EVENT_ERROR)) {
        nrf_uart_event_clear(self->p_reg, NRF_UART_EVENT_ERROR);
        self->data->rxbuf_wpos = -MP_EIO;
    }
    if (nrf_uart_event_check(self->p_reg, NRF_UART_EVENT_RXTO)) {
        nrf_uart_event_clear(self->p_reg, NRF_UART_EVENT_RXTO);
        self->data->rxbuf_wpos = -MP_ETIMEDOUT;
    }
}

void UART0_IRQHandler() {
    uart_irq_handler(&machine_hard_uart_obj[0]);
}

#if NRF52840_XXAA
void UART1_IRQHandler() {
    uart_irq_handler(&machine_hard_uart_obj[1]);
}
#endif

bool uart_rx_any(const machine_hard_uart_obj_t *uart_obj) {
    // TODO: uart will block for now.
    return true;
}

int uart_rx_char(const machine_hard_uart_obj_t * self) {
    int8_t rpos = self->data->rxbuf_rpos;
    while (1) {
        // Is there an error on the line?
        int8_t wpos = self->data->rxbuf_wpos;
        if (wpos < 0) {
            // Yes, clear and return it.
            self->data->rxbuf_wpos = rpos;
            return wpos;
        }

        // Is there a character ready?
        if (wpos != rpos) {
            int ch = self->data->rxbuf_buf[rpos];
            self->data->rxbuf_rpos = (rpos + 1) % sizeof(self->data->rxbuf_buf);
            return ch;
        }

        // Wait until the next interrupt.
        __WFE();
    }
}

STATIC void uart_tx_char(const machine_hard_uart_obj_t * self, int c) {
    // Start a transmission sequence.
    nrf_uart_task_trigger(self->p_reg, NRF_UART_TASK_STARTTX);

    // Wait until the previous char is sent.
    if (self->data->tx_started) {
        while (!nrf_uart_event_check(self->p_reg, NRF_UART_EVENT_TXDRDY)) { }
        nrf_uart_event_clear(self->p_reg, NRF_UART_EVENT_TXDRDY);
    }

    // Send this character.
    nrf_uart_txd_set(self->p_reg, c);
    self->data->tx_started = true;
}


void uart_tx_strn(const machine_hard_uart_obj_t *uart_obj, const char *str, uint len) {
    for (const char *top = str + len; str < top; str++) {
        uart_tx_char(uart_obj, *str);
    }
}

void uart_tx_strn_cooked(const machine_hard_uart_obj_t *uart_obj, const char *str, uint len) {
    for (const char *top = str + len; str < top; str++) {
        if (*str == '\n') {
            uart_tx_char(uart_obj, '\r');
        }
        uart_tx_char(uart_obj, *str);
    }
}

/******************************************************************************/
/* MicroPython bindings                                                      */

STATIC void machine_hard_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
}



/// \method init(baudrate, bits=8, parity=None, stop=1, *, timeout=1000, timeout_char=0, read_buf_len=64)
///
/// Initialise the UART bus with the given parameters:
///   - `id`is bus id.
///   - `baudrate` is the clock rate.
///   - `bits` is the number of bits per byte, 7, 8 or 9.
///   - `parity` is the parity, `None`, 0 (even) or 1 (odd).
///   - `stop` is the number of stop bits, 1 or 2.
///   - `timeout` is the timeout in milliseconds to wait for the first character.
///   - `timeout_char` is the timeout in milliseconds to wait between characters.
///   - `read_buf_len` is the character length of the read buffer (0 to disable).
STATIC mp_obj_t machine_hard_uart_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_baudrate, ARG_bits, ARG_parity, ARG_stop, ARG_flow, ARG_timeout, ARG_timeout_char, ARG_read_buf_len };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,       MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_baudrate, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 9600} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_parity, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_stop, MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_flow, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000} },
        { MP_QSTR_timeout_char, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_read_buf_len, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 64} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // get static peripheral object
    int uart_id = uart_find(args[ARG_id].u_obj);
    const machine_hard_uart_obj_t * self = &machine_hard_uart_obj[uart_id];

    // These baudrates are not supported, it seems.
    if (args[ARG_baudrate].u_int < 1200 || args[ARG_baudrate].u_int > 1000000) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError,
                  "UART baudrate not supported, %u", args[ARG_baudrate].u_int));
    }

    // Magic: calculate 'baudrate' register from the input number.
    // Every value listed in the datasheet will be converted to the
    // correct register value, except for 192600. I suspect the value
    // listed in the nrf52 datasheet (0x0EBED000) is incorrectly rounded
    // and should be 0x0EBEE000, as the nrf51 datasheet lists the
    // nonrounded value 0x0EBEDFA4.
    // Some background:
    // https://devzone.nordicsemi.com/f/nordic-q-a/391/uart-baudrate-register-values/2046#2046
    nrf_uart_baudrate_t baudrate = args[ARG_baudrate].u_int / 400 * (uint32_t)(400ULL * (uint64_t)UINT32_MAX / 16000000ULL);
    baudrate = (baudrate + 0x800) & 0xffffff000; // rounding

    // Configure TX and RX GPIO pins: tx as output (and initially high)
    // and rx as input.
    nrf_gpio_pin_set(MICROPY_HW_UART1_TX);
    nrf_gpio_cfg_output(MICROPY_HW_UART1_TX);
    nrf_gpio_cfg_input(MICROPY_HW_UART1_RX, NRF_GPIO_PIN_NOPULL);

    // Set the UART to use these tx/rx pins.
    nrf_uart_txrx_pins_set(self->p_reg, MICROPY_HW_UART1_TX, MICROPY_HW_UART1_RX);

#if MICROPY_HW_UART1_HWFC
    // Configure CTS and RTS pins: CTS as input and RTS as output (and
    // initially high).
    // TODO: currently using a workaround by pulling the CTS pin low.
    // Otherwise UART won't work properly on PCA10040 boards.
    nrf_gpio_cfg_input(MICROPY_HW_UART1_CTS, NRF_GPIO_PIN_NOPULL);
    nrf_gpio_pin_set(MICROPY_HW_UART1_RTS);
    nrf_gpio_cfg_output(MICROPY_HW_UART1_RTS);

    // Set the UART driver to use these RTS/CTS pins.
    nrf_uart_hwfc_pins_set(self->p_reg, MICROPY_HW_UART1_RTS, MICROPY_HW_UART1_CTS);

    nrf_uart_hwfc_t hwfc = NRF_UART_HWFC_ENABLED;

#else
    // Do not use flow control.
    nrf_uart_hwfc_t hwfc = NRF_UART_HWFC_DISABLED;
#endif

    // Other configuration: no parity and optional flow control.
    nrf_uart_configure(self->p_reg, NRF_UART_PARITY_EXCLUDED, hwfc);
    nrf_uart_baudrate_set(self->p_reg, baudrate);

    // Finally, enable the UART.
    nrf_uart_enable(self->p_reg);

    // Start a receive sequence. This will always be enabled.
    nrf_uart_task_trigger(self->p_reg, NRF_UART_TASK_STARTRX);

    NRFX_IRQ_PRIORITY_SET(nrfx_get_irq_number(self->p_reg), 3);
    NRFX_IRQ_ENABLE(nrfx_get_irq_number(self->p_reg));

    nrf_uart_int_enable(self->p_reg, NRF_UART_INT_MASK_RXDRDY |
                                     NRF_UART_INT_MASK_ERROR  |
                                     NRF_UART_INT_MASK_RXTO);

    return MP_OBJ_FROM_PTR(self);
}

/// \method writechar(char)
/// Write a single character on the bus.  `char` is an integer to write.
/// Return value: `None`.
STATIC mp_obj_t machine_hard_uart_writechar(mp_obj_t self_in, mp_obj_t char_in) {
    machine_hard_uart_obj_t *self = self_in;

    // get the character to write (might be 9 bits)
    uint16_t data = mp_obj_get_int(char_in);

    for (int i = 0; i < 2; i++) {
        uart_tx_char(self, (int)(&data)[i]);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_hard_uart_writechar_obj, machine_hard_uart_writechar);

/// \method readchar()
/// Receive a single character on the bus.
/// Return value: The character read, as an integer.  Returns -1 on timeout.
STATIC mp_obj_t machine_hard_uart_readchar(mp_obj_t self_in) {
    machine_hard_uart_obj_t *self = self_in;
    int ch = uart_rx_char(self);
    if (ch < 0) {
        mp_raise_OSError(ch);
    }
    return MP_OBJ_NEW_SMALL_INT(ch);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_hard_uart_readchar_obj, machine_hard_uart_readchar);

// uart.sendbreak()
STATIC mp_obj_t machine_hard_uart_sendbreak(mp_obj_t self_in) {
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_hard_uart_sendbreak_obj, machine_hard_uart_sendbreak);

STATIC const mp_rom_map_elem_t machine_hard_uart_locals_dict_table[] = {
    // instance methods
    /// \method read([nbytes])
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    /// \method readline()
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    /// \method readinto(buf[, nbytes])
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    /// \method writechar(buf)
    { MP_ROM_QSTR(MP_QSTR_writechar), MP_ROM_PTR(&machine_hard_uart_writechar_obj) },
    { MP_ROM_QSTR(MP_QSTR_readchar), MP_ROM_PTR(&machine_hard_uart_readchar_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendbreak), MP_ROM_PTR(&machine_hard_uart_sendbreak_obj) },

    // class constants
/*
    { MP_ROM_QSTR(MP_QSTR_RTS), MP_ROM_INT(UART_HWCONTROL_RTS) },
    { MP_ROM_QSTR(MP_QSTR_CTS), MP_ROM_INT(UART_HWCONTROL_CTS) },
*/
};

STATIC MP_DEFINE_CONST_DICT(machine_hard_uart_locals_dict, machine_hard_uart_locals_dict_table);

STATIC mp_uint_t machine_hard_uart_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    const machine_hard_uart_obj_t *self = self_in;
    byte *buf = buf_in;

    // make sure we want at least 1 char
    if (size == 0) {
        return 0;
    }

    // read the data
    byte * orig_buf = buf;
    for (;;) {
        int c = uart_rx_char(self);
        if (c < 0) {
            *errcode = MP_EIO;
            return MP_STREAM_ERROR;
        }

        *buf++ = c;

        if (--size == 0) {
            // return number of bytes read
            return buf - orig_buf;
        }
    }
}

STATIC mp_uint_t machine_hard_uart_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    machine_hard_uart_obj_t *self = self_in;
    const byte *buf = buf_in;

    for (int i = 0; i < size; i++) {
        uart_tx_char(self, (int)((uint8_t *)buf)[i]);
    }

    return size;
}

STATIC mp_uint_t machine_hard_uart_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    machine_hard_uart_obj_t *self = self_in;
    (void)self;
    return MP_STREAM_ERROR;
}

STATIC const mp_stream_p_t uart_stream_p = {
    .read = machine_hard_uart_read,
    .write = machine_hard_uart_write,
    .ioctl = machine_hard_uart_ioctl,
    .is_text = false,
};

const mp_obj_type_t machine_hard_uart_type = {
    { &mp_type_type },
    .name = MP_QSTR_UART,
    .print = machine_hard_uart_print,
    .make_new = machine_hard_uart_make_new,
    .getiter = mp_identity_getiter,
    .iternext = mp_stream_unbuffered_iter,
    .protocol = &uart_stream_p,
    .locals_dict = (mp_obj_dict_t*)&machine_hard_uart_locals_dict,
};

#endif // MICROPY_PY_MACHINE_UART
