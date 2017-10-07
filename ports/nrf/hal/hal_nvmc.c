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

#ifdef HAL_NVMC_MODULE_ENABLED

#if !NRF51
#error NRF52 not yet implemented
#endif

#if BLUETOOTH_SD
#error NVMC with SoftDevice not yet implemented
#endif

void hal_nvmc_erase_page(uint32_t pageaddr) {
    // Configure NVMC to erase a page.
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Set the page to erase
    NRF_NVMC->ERASEPAGE = pageaddr;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    // Switch back to read-only.
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
}

void hal_nvmc_write_buffer(uint32_t pageaddr, uint32_t *buf, int len) {
    // Note that we're writing 32-bit integers, not bytes. Thus the 'real'
    // length of the buffer is len*4.

    // Configure NVMC so that writes are allowed (anywhere).
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

    uint32_t *flashbuf = (uint32_t*)pageaddr;

    // Write all integers to flash.
    for (int i=0; i<len; i++) {
        flashbuf[i] = buf[i];
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
    }

    // Switch back to read-only.
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
}

#endif // HAL_NVMC_MODULE_ENABLED
