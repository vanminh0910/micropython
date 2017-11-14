
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mpconfigport.h"
#include "device/nrf.h"
#include "nrf_sdm.h"
#include "nrf_mbr.h"
#include "nrf_soc.h"

#include "bootloader.h"
#include "bootloader_ble.h"

#if !BOOTLOADER_IN_MBR
__attribute__((section(".bootloaderaddr"),used))
const uint32_t *bootloaderaddr = BOOTLOADER_START_ADDR;
#endif

#if DEBUG
static void softdevice_assert_handler(uint32_t pc, uint16_t line_number, const uint8_t * p_file_name) {
    LOG("ERROR: SoftDevice assert!!!");
    while (1);
}
#else
void HardFault_Handler(void);
#define softdevice_assert_handler ((softdevice_assertion_handler_t)HardFault_Handler)
#endif

static void jump_to_app() {
#if DEBUG
    uart_disable();
#endif
    // Note that the SoftDevice needs to be disabled before calling this
    // function.

#if BOOTLOADER_IN_MBR
    *(uint32_t*)MBR_VECTOR_TABLE = SOFTDEVICE_START_ADDR;
#endif

    // The ISR vector contains these entries (among others):
    // 0: pointer to the end of the stack (_estack)
    // 1: the Reset_Handler
    // Note that we can't just jump to the app, we have to 'reset' the
    // stack pointer to the beginning of the stack (e.g. the highest
    // address).
    uint32_t *sd_isr = (uint32_t*)SOFTDEVICE_START_ADDR;
    uint32_t new_sp = sd_isr[0]; // load end of stack (_estack)
    uint32_t new_pc = sd_isr[1]; // load Reset_Handler
    __asm__ __volatile__(
            "mov sp, %[new_sp]\n" // set stack pointer to initial stack pointer
            "mov pc, %[new_pc]\n" // jump to SoftDevice Reset_Vector
            :
            : [new_sp]"r" (new_sp),
              [new_pc]"r" (new_pc));
    __builtin_unreachable();
}


uint8_t flash_buf[PAGE_SIZE];
uint8_t *flash_buf_ptr;

void _start(void) {
#if DEBUG
    uart_enable();
#endif

    LOG("");

    // Set the vector table. This may be used by the SoftDevice.
    LOG("init MBR vector table");
#if BOOTLOADER_IN_MBR
    *(uint32_t*)MBR_VECTOR_TABLE = 0;
#else
    *(uint32_t*)MBR_VECTOR_TABLE = SOFTDEVICE_START_ADDR;
#endif


    // Check whether there is something that looks like a reset handler at
    // the app ISR vector. If the page has been cleared, it will be
    // 0xffffffff.
    // Also, check whether GPREGRET is set: if it is, the application has
    // requested we stay in DFU mode.
    uint32_t *app_isr = (uint32_t*)APPLICATION_START_ADDR;
    uint32_t reset_handler = app_isr[1];
    if (reset_handler != 0xffffffff && NRF_POWER->GPREGRET == 0) {
        // There is a valid application and the application hasn't
        // requested for DFU mode.
        LOG("valid Reset_Handler");
        jump_to_app();
    } else {
        LOG("invalid Reset_Handler or GPREGRET was set");
    }

    // Make sure a reset won't jump to the bootloader again. This only
    // matters if the application requested to go to the bootloader by
    // setting this register (it defaults to 0).
    NRF_POWER->GPREGRET = 0;

    // This always uses the internal clock. Which takes more power, but
    // DFU mode isn't meant to be enabled for long periods anyway. It
    // avoids having to configure internal/external clocks.
    LOG("enable sd");
    sd_softdevice_enable(NRF_CLOCK_LFCLKSRC_RC_250_PPM_250MS_CALIBRATION, softdevice_assert_handler);

    // Enable IRQ for SoftDevice.
    // Disabled as it is not necessary as all events are handled in
    // ble_run().
    //LOG("enable irq");
    //sd_nvic_EnableIRQ(SWI2_IRQn);

    flash_buf_ptr = flash_buf;

    ble_init();

    LOG("waiting...");
    ble_run();
}

void handle_command(uint16_t data_len, ble_command_t *cmd) {
    // Format: command (1 byte), payload (any length, up to 19 bytes with
    // default MTU)
    if (INPUT_CHECKS && data_len == 0) return;
    if (cmd->any.command == COMMAND_RESET) {
        LOG("command: reset");
        sd_nvic_SystemReset();
    } else if (cmd->any.command == COMMAND_ERASE_PAGE) {
        if (INPUT_CHECKS && data_len < 4) return;
        LOG("command: erase page");
        uint32_t err_code = sd_flash_page_erase(cmd->erase.page);
        if (ERROR_REPORTING && err_code != 0) {
            // Error: the erase command wasn't scheduled.
            ble_send_reply(1);
        }
        if (err_code == NRF_ERROR_INTERNAL) {
            LOG("! internal error");
        } else if (err_code == NRF_ERROR_BUSY) {
            LOG("! busy");
        } else if (err_code != 0) {
            LOG("! could not start erase of page");
        }
    } else if (cmd->any.command == COMMAND_WRITE_BUFFER) {
        LOG("command: do write");
#if FLASH_PAGE_CHECKS
        if (cmd->write.page < APPLICATION_START_ADDR / PAGE_SIZE || (!BOOTLOADER_IN_MBR && cmd->write.page >= (uint32_t)BOOTLOADER_START_ADDR / PAGE_SIZE)) {
            if (ERROR_REPORTING) {
                ble_send_reply(1);
            }
            return;
        }
#endif
        if (sd_flash_write((uint32_t*)((uintptr_t)cmd->write.page * PAGE_SIZE), (uint32_t*)flash_buf, cmd->write.n_words) != 0) {
            LOG("could not start page write");
            if (ERROR_REPORTING) {
                ble_send_reply(1);
            }
        }
        flash_buf_ptr = flash_buf;
#if !PACKET_CHARACTERISTIC
    } else if (cmd->any.command == COMMAND_ADD_BUFFER) {
        const uint8_t *in_start = cmd->buffer.buffer;
        uint8_t *out_end = flash_buf_ptr + (data_len - 4);
        if (INPUT_CHECKS && out_end > flash_buf + PAGE_SIZE) {
            out_end = flash_buf + PAGE_SIZE;
        }
        while (flash_buf_ptr != out_end) {
            *flash_buf_ptr++ = *in_start++;
        }
#endif
#if DEBUG
    } else if (cmd->any.command == COMMAND_PING) {
        // Only for debugging
        LOG("command: ping");
        ble_send_reply(0);
    } else if (cmd->any.command == COMMAND_START) {
        // Not implementing this saves ~22 bytes.
        // Note that it doesn't always work. That has probably something
        // to do with the SoftDevice.
        LOG("command: start app");
        if (sd_softdevice_disable() != 0) {
            LOG("could not disable SoftDevice");
        }
        jump_to_app();
#endif
    } else {
        LOG("command: ???");
    }
}

void handle_buffer(uint16_t data_len, uint8_t *data) {
    const uint8_t *in_start = data;
    uint8_t *out_end = flash_buf_ptr + data_len;
    if (INPUT_CHECKS && out_end > flash_buf + PAGE_SIZE) {
        return;
    }
    while (flash_buf_ptr != out_end) {
        *flash_buf_ptr++ = *in_start++;
    }
}

void sd_evt_handler(uint32_t evt_id) {
    switch (evt_id) {
        case NRF_EVT_FLASH_OPERATION_SUCCESS:
            //LOG("sd evt: flash operation finished");
            ble_send_reply(0);
            break;
        case NRF_EVT_FLASH_OPERATION_ERROR:
            LOG("sd evt: flash operation error");
            if (ERROR_REPORTING) {
                ble_send_reply(1);
            }
            break;
        default:
            LOG("sd evt: unknown SD evt");
            break;
    }
}
