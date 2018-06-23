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

#ifndef NO_QSTR
#include "host/ble_hs.h"
#include "hal/hal_uart.h"
#endif // NO_QSTR

#include "py/ringbuf.h"

#include "ble.h"
#include "mphalport.h"

static uint8_t ble_nus_tx_ring_buf[20]; // increase size for higher speed
static ringbuf_t ble_nus_tx_ring = {ble_nus_tx_ring_buf, sizeof(ble_nus_tx_ring_buf)};
static struct os_sem ble_nus_sem;
static uint16_t ble_nus_conn_handle;
static uint16_t ble_nus_tx_char_handle;
static struct os_callout ble_nus_tx_timer;

static void ble_advertise();

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
        } else {
            ble_advertise();
        }
    case BLE_GAP_EVENT_DISCONNECT:
        ble_advertise();
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == ble_nus_tx_char_handle) {
            if (event->subscribe.cur_notify) {
                // subscribed
                ble_nus_conn_handle = event->subscribe.conn_handle;
            } else {
                // unsubscribed, lost connection, etc.
                ble_nus_conn_handle = 0;
            }
        }
    }

    return 0;
}

// Set a random address as the current BLE address.
static void ble_set_addr() {
    ble_addr_t addr;
    ble_hs_id_gen_rnd(1, &addr);
    ble_hs_id_set_rnd(addr.val);
}

// Start advertisement.
static void ble_advertise() {
    // Set advertisement packets. A good overview can be seen at:
    // https://www.silabs.com/community/wireless/bluetooth/knowledge-base.entry.html/2017/02/10/bluetooth_advertisin-hGsf
    // Not using the *_fields API here because it consumes a lot of code (~1.5kB).

    // Configure an eddystone URL beacon to be advertised.
    // https://goo.gl/F7fZ69 => https://aykevl.nl/apps/nus/
    static uint8_t eddystone_url_data[27] = {0x2, 0x1, 0x6,
                                             0x3, 0x3, 0xaa, 0xfe,
                                             19, 0x16, 0xaa, 0xfe, 0x10, 0xe7, 0x3, 'g', 'o', 'o', '.', 'g', 'l', '/', 'F', '7', 'f', 'Z', '6', '9'};
    ble_gap_adv_set_data(eddystone_url_data, sizeof(eddystone_url_data));

    // Scan response data. The structs are:
    //  - local name (0x09) of length 4
    //  - complete list of 128-bit UUIDs (0x07) of length 17
    static uint8_t scan_response_data[23] = {4, 0x09, 'M', 'P', 'Y',
                                             17, 0x07, 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
    ble_gap_adv_rsp_set_data(scan_response_data, sizeof(scan_response_data));

    struct ble_gap_adv_params adv_params = (struct ble_gap_adv_params){ 0 };
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

// Callback on TX characteristic read.
static int nus_tx(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    // Don't read directly. Instead, wait for a notification.
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

// Callback on RX characteristic write.
static int nus_rx(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    struct os_mbuf *om = ctxt->om;
    for (size_t i = 0; i < om->om_len; i++) {
        hal_rx_char_cb(NULL, om->om_data[i]);
    }
    return 0;
}

// Define the 3 different UUIDs used for Nordic UART Service.
static const ble_uuid128_t uuid_sv = BLE_UUID128_INIT(
        0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t uuid_rx = BLE_UUID128_INIT(
        0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t uuid_tx = BLE_UUID128_INIT(
        0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

static const struct ble_gatt_svc_def nus_service[] = {
    {
        // Nordic UART Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_sv.u,
        .characteristics = (struct ble_gatt_chr_def[]) { {
            // RX characteristic
            .uuid = &uuid_rx.u,
            .access_cb = nus_rx,
            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        }, {
            // TX characteristic
            .uuid = &uuid_tx.u,
            .val_handle = &ble_nus_tx_char_handle,
            .access_cb = nus_tx,
            .flags = BLE_GATT_CHR_F_NOTIFY,
        }, {
            0 // no more characteristics
        }, },
    }, {
        0 // no more services
    },
};

// BLE is ready callback, configure it.
static void ble_on_sync() {
    ble_set_addr();
    ble_advertise();
}

// Callback called when the tx timer fires.
static void ble_nus_tx_cb(struct os_event *ev) {
    if (ble_nus_conn_handle == 0) {
        return; // no device connected
    }

    // Fill a flat buffer.
    os_sr_t sr;
    OS_ENTER_CRITICAL(sr);
    uint8_t buf[sizeof(ble_nus_tx_ring_buf)];
    uint8_t len = 0;
    while (1) {
        int c = ringbuf_get(&ble_nus_tx_ring);
        if (c < 0) {
            break;
        }
        buf[len++] = c;
    }
    OS_EXIT_CRITICAL(sr);

    OS_ENTER_CRITICAL(sr);
    if (os_sem_get_count(&ble_nus_sem) == 0) {
        os_sem_release(&ble_nus_sem);
    }
    OS_EXIT_CRITICAL(sr);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (!om) {
        return; // error?
    }
    ble_gattc_notify_custom(ble_nus_conn_handle, ble_nus_tx_char_handle, om);
}


// Send a single character over the NUS connection.
void ble_nus_tx(uint8_t c) {
    if (ble_nus_conn_handle == 0) {
        return; // no device connected
    }

    bool start_tx = false;
    bool blocked = false;

    // Put the char in the buffer and check whether it's the first char in
    // the buffer.
    os_sr_t sr;
    OS_ENTER_CRITICAL(sr);
    if (ble_nus_tx_ring.iget == ble_nus_tx_ring.iput) {
        // First char in the buffer.
        start_tx = true;
    }
    if (ringbuf_put(&ble_nus_tx_ring, c) < 0) {
        blocked = true;
    }
    OS_EXIT_CRITICAL(sr);

    if (blocked) {
        // Wait until the last buffer has been sent.
        os_sem_pend(&ble_nus_sem, OS_TIMEOUT_NEVER);

        // Add the char to the (now empty) ringbuffer.
        OS_ENTER_CRITICAL(sr);
        ringbuf_put(&ble_nus_tx_ring, c);
        OS_EXIT_CRITICAL(sr);

        // Queue a send event as this is also the first char in the
        // buffer.
        os_callout_reset(&ble_nus_tx_timer, OS_TICKS_PER_SEC / 60 + 1);
    } else if (start_tx) {
        // First char in the buffer, queue a send event in ~17ms.
        os_callout_reset(&ble_nus_tx_timer, OS_TICKS_PER_SEC / 60 + 1);
    }
}


// Initialize the BLE subsystem.
void ble_init() {
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_gatts_count_cfg(nus_service);
    ble_gatts_add_svcs(nus_service);

    os_callout_init(&ble_nus_tx_timer, os_eventq_dflt_get(), ble_nus_tx_cb, NULL);

    os_sem_init(&ble_nus_sem, 1);
}
