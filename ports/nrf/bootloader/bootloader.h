
#pragma once

#include "bootloader_uart.h"

#define DEBUG 0
#if DEBUG
#define LOG(s) uart_write(s "\r\n")
#else
#define LOG(s)
#endif

#define INPUT_CHECKS    (1)
#define ERROR_REPORTING (1)

#define BOOTLOADER_START_ADDR  (0x00039000)
#define APPLICATION_START_ADDR (0x00018000)
#define APPLICATION_END_ADDR   (0x0003b000)
#define FLASH_SIZE             (0x00040000)
#define PAGE_SIZE              (1024)
#define PAGE_SIZE_LOG2         (10)

#define COMMAND_RESET        (0x01) // do a reset
#define COMMAND_ERASE_PAGE   (0x02) // start erasing this page
#define COMMAND_ADD_BUFFER   (0x03) // add data to write buffer
#define COMMAND_WRITE_BUFFER (0x04) // start writing this page and reset buffer
#define COMMAND_PING         (0x10) // just ask a response (debug)
#define COMMAND_START        (0x11) // start the app (debug, unreliable)

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
        uint32_t buffer[4];
    } buffer; // COMMAND_ADD_BUFFER
    struct {
        uint8_t  command;
        uint8_t  flags; // or rather: padding
        uint16_t page;
        uint16_t n_words;
    } write; // COMMAND_WRITE_BUFFER
} ble_command_t;

void handle_command(uint16_t data_len, ble_command_t *data);

void sd_evt_handler(uint32_t evt_id);
