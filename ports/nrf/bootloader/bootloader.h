
#pragma once

#include "bootloader_uart.h"

#define DEBUG 0
#if DEBUG
#define LOG(s) uart_write(s "\r\n")
#else
#define LOG(s)
#endif

#define INPUT_CHECKS           (1) // whether the received buffer is the correct length
#define FLASH_PAGE_CHECKS      (1) // check that flash pages are within the app area
#define ERROR_REPORTING        (1) // send error when something goes wrong (e.g. flash write fail)
#define PACKET_CHARACTERISTIC  (1) // add a separate transport characteristic - improves speed but costs 32 bytes

extern const uint32_t _stext[];

#define BOOTLOADER_START_ADDR  (_stext)
#define SOFTDEVICE_START_ADDR  (0x00001000)
#define APPLICATION_START_ADDR (0x00018000)
#define APPLICATION_END_ADDR   (0x0003b000)
#define MBR_VECTOR_TABLE       (0x20000000)
#define FLASH_SIZE             (0x00040000)
#define PAGE_SIZE              (1024)
#define PAGE_SIZE_LOG2         (10)

#define COMMAND_RESET        (0x01) // do a reset
#define COMMAND_ERASE_PAGE   (0x02) // start erasing this page
#define COMMAND_WRITE_BUFFER (0x03) // start writing this page and reset buffer
#define COMMAND_ADD_BUFFER   (0x04) // add data to write buffer
#define COMMAND_PING         (0x10) // just ask a response (debug)
#define COMMAND_START        (0x11) // start the app (debug, unreliable)

#if BOOTLOADER_IN_MBR
#define MBRCONST
#else
#define MBRCONST const
#endif

typedef union {
    struct {
        uint8_t  command;
    } any;
    struct {
        uint8_t  command;
        uint8_t  flags; // or rather: padding
        uint16_t page;
    } erase; // COMMAND_ERASE_PAGE
    struct {
        uint8_t  command;
        uint8_t  flags; // or rather: padding
        uint16_t padding;
        uint8_t  buffer[16];
    } buffer; // COMMAND_ADD_BUFFER
    struct {
        uint8_t  command;
        uint8_t  flags; // or rather: padding
        uint16_t page;
        uint16_t n_words;
    } write; // COMMAND_WRITE_BUFFER
} ble_command_t;

void handle_command(uint16_t data_len, ble_command_t *data);
void handle_buffer(uint16_t data_len, uint8_t *data);

void sd_evt_handler(uint32_t evt_id);
