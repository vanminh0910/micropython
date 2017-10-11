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
#include "mphalport.h"
#include "hal_nvmc.h"

#if BLUETOOTH_SD
#include "ble_drv.h"
#include "nrf_soc.h"
#endif

#ifdef HAL_NVMC_MODULE_ENABLED

#if BLUETOOTH_SD

bool hal_nvmc_erase_page(uint32_t pageaddr) {
    uint32_t result = sd_flash_page_erase(pageaddr / HAL_NVMC_PAGESIZE);
    if (ble_drv_stack_enabled() == 1) {
        // TODO wait for result
        return result == NRF_SUCCESS;
    } else {
        return result == NRF_SUCCESS;
    }
}

bool hal_nvmc_write_words(uint32_t *dest, const uint32_t *buf, size_t len) {
    uint32_t result = sd_flash_write(dest, buf, len);
    if (ble_drv_stack_enabled() == 1) {
        // TODO wait for result
        return result == NRF_SUCCESS;
    } else {
        return result == NRF_SUCCESS;
    }
}

bool hal_nvmc_write_byte(byte *dest_in, byte b) {
    uint32_t dest = (uint32_t)dest_in;
    uint32_t dest_aligned = dest & ~3;

    // Value to write - leave all bits that should not change at 0xff.
    uint32_t value = 0xffffff00 | b;

    // Rotate bits in value to an aligned position.
    for (int i = dest - dest_aligned; i; i--) {
        value = (value << 8) | 0xff;
    }

    uint32_t result = sd_flash_write((uint32_t*)dest_aligned, &value, 1);
    if (ble_drv_stack_enabled() == 1) {
        // TODO wait for result
        return result == NRF_SUCCESS;
    } else {
        return result == NRF_SUCCESS;
    }
}

#else // BLUETOOTH_SD

bool hal_nvmc_erase_page(uint32_t pageaddr) {
    // Configure NVMC to erase a page.
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Set the page to erase
    NRF_NVMC->ERASEPAGE = pageaddr;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Switch back to read-only.
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Operation succeeded.
    return true;
}

bool hal_nvmc_write_words(uint32_t *dest, const uint32_t *buf, size_t len) {
    // Note that we're writing 32-bit integers, not bytes. Thus the 'real'
    // length of the buffer is len*4.

    // Configure NVMC so that writes are allowed (anywhere).
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Write all integers to flash.
    for (int i=0; i<len; i++) {
        dest[i] = buf[i];
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
    }

    // Switch back to read-only.
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Operation succeeded.
    return true;
}

bool hal_nvmc_write_byte(byte *dest_in, byte b) {
    // This code can probably be optimized.

    // Configure NVMC so that writes are allowed (anywhere).
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // According to the nRF51 RM (chapter 6), only word writes to
    // word-aligned addresses are allowed.
    // https://www.nordicsemi.com/eng/nordic/Products/nRF51822/nRF51-RM/62725
    uint32_t dest = (uint32_t)dest_in;
    uint32_t dest_aligned = dest & ~3;

    // Value to write - leave all bits that should not change at 0xff.
    uint32_t value = 0xffffff00 | b;

    // Rotate bits in value to an aligned position.
    for (int i = dest - dest_aligned; i; i--) {
        value = (value << 8) | 0xff;
    }

    // Put the value at the right place.
    *(uint32_t*)dest_aligned = value;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Switch back to read-only.
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Operation succeeded.
    return true;
}

#endif // BLUETOOTH_SD

bool hal_nvmc_write_buffer(void *dest_in, const void *buf_in, size_t len) {
    byte *dest = dest_in;
    const byte *buf = buf_in;

    // Write first bytes to align the buffer.
    while (len && ((uint32_t)dest & 0b11)) {
        hal_nvmc_write_byte(dest, *buf);
        dest++;
        buf++;
        len--;
    }

    // Now the start of the buffer is aligned. Write as many words as
    // possible, as that's much faster than writing bytes.
    if (len / 4 && ((uint32_t)buf & 0b11) == 0) {
        hal_nvmc_write_words((uint32_t*)dest, (const uint32_t*)buf, len/4);
        dest += len & ~0b11;
        buf  += len & ~0b11;
        len &= 0b11;
    }

    // Write remaining unaligned bytes.
    while (len) {
        hal_nvmc_write_byte(dest, *buf);
        dest++;
        buf++;
        len--;
    }

    return true;
}

#endif // HAL_NVMC_MODULE_ENABLED
