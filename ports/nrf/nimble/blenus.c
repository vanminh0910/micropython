/*
 * This file is part of the MicroPython project, http://micropython.org/
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

#include "blenus.h"
#include "mphalport.h"

static int nus_tx(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int nus_rx(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static uint16_t ble_nus_conn_handle;
static uint16_t ble_nus_tx_char_handle;


// Define the 3 different UUIDs used for Nordic UART Service.
static const ble_uuid128_t uuid_sv = BLE_UUID128_INIT(
        0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t uuid_rx = BLE_UUID128_INIT(
        0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t uuid_tx = BLE_UUID128_INIT(
        0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

// Declaration of the NUS service.
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


static void check_rc(int rc, char *msg) {
    if (rc != 0) {
        printf("error: returned %d from %s\n", rc, msg);
    }
}

void nus_init(void) {
    int rc;
    rc = ble_gatts_count_cfg(nus_service);
    check_rc(rc, "count cfg");
    rc = ble_gatts_add_svcs(nus_service);
    check_rc(rc, "add cfg");
}

static int nus_gap_event(struct ble_gap_event *event, void *arg) {
    printf("event: %d\n", event->type);
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        printf("  connect: %d\n", event->connect.status);
        if (event->connect.status != 0) {
            nus_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("  disconnect\n");
        nus_advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        printf("  subscribe\n");
        if (event->subscribe.attr_handle == ble_nus_tx_char_handle) {
            if (event->subscribe.cur_notify) {
                // subscribed
                ble_nus_conn_handle = event->subscribe.conn_handle;
            } else {
                // unsubscribed, lost connection, etc.
                ble_nus_conn_handle = 0;
            }
        }
        break;
    }

    return 0;
}

// Start advertisement.
void nus_advertise(void) {
    printf("nus_advertise\n");
    int rc;
    // Set advertisement packets. A good overview can be seen at:
    // https://www.silabs.com/community/wireless/bluetooth/knowledge-base.entry.html/2017/02/10/bluetooth_advertisin-hGsf
    // Not using the *_fields API here because it consumes a lot of code (~1.5kB).

    // Configure an eddystone URL beacon to be advertised.
    // https://goo.gl/F7fZ69 => https://aykevl.nl/apps/nus/
    static const uint8_t eddystone_url_data[27] = {0x2, 0x1, 0x6,
                                             0x3, 0x3, 0xaa, 0xfe,
                                             19, 0x16, 0xaa, 0xfe, 0x10, 0xe7, 0x3, 'g', 'o', 'o', '.', 'g', 'l', '/', 'F', '7', 'f', 'Z', '6', '9'};
    rc = ble_gap_adv_set_data(eddystone_url_data, sizeof(eddystone_url_data));
    check_rc(rc, "adv set data");

    // Scan response data. The structs are:
    //  - local name (0x09) of length 4
    //  - complete list of 128-bit UUIDs (0x07) of length 17
    static const uint8_t scan_response_data[23] = {4, 0x09, 'M', 'P', 'Y',
                                             17, 0x07, 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
    rc = ble_gap_adv_rsp_set_data(scan_response_data, sizeof(scan_response_data));
    check_rc(rc, "adv rsp set data");

    struct ble_gap_adv_params adv_params = (struct ble_gap_adv_params){ 0 };
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 100;
    adv_params.itvl_max = 100;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv_params, nus_gap_event, NULL);
    check_rc(rc, "adv start");
}

// Callback on TX characteristic read.
static int nus_tx(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    // Don't read directly. Instead, wait for a notification.
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

// Callback on RX characteristic write.
static int nus_rx(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    printf("!");
    struct os_mbuf *om = ctxt->om;
    for (size_t i = 0; i < om->om_len; i++) {
        //hal_rx_char_cb(NULL, om->om_data[i]);
    }
    return 0;
}
