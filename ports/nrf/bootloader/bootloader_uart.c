

#include "device/nrf.h"
#include "bootloader_uart.h"


void uart_write_char(char ch) {
    NRF_UART0->TXD = ch;
    while (NRF_UART0->EVENTS_TXDRDY != 1) {}
    NRF_UART0->EVENTS_TXDRDY = 0;
}

void uart_write(char *s) {
    while (*s) {
        uart_write_char(*s++);
    }
}

void uart_enable() {
    // TODO: set correct GPIO configuration? Only necessary when system
    // goes to OFF state.
    NRF_UART0->ENABLE        = UART_ENABLE_ENABLE_Enabled;
    NRF_UART0->BAUDRATE      = UART_BAUDRATE_BAUDRATE_Baud115200;
    NRF_UART0->TASKS_STARTTX = 1;
    NRF_UART0->PSELTXD       = 2; // P0.02
}

void uart_disable() {
    NRF_UART0->ENABLE        = UART_ENABLE_ENABLE_Disabled;
    NRF_UART0->PSELTXD       = 0xffffffff;
}
