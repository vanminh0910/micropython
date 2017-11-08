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

#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

typedef void (*func)(void);

extern void  _start(void) __attribute__((noreturn));
extern void SystemInit(void);

#if 0
void uart_write(char *s);

void Default_Handler(void) {
    uart_write("Default_Handler\r\n");
    while (1);
}

void HardFault_Handler(void) {
    uart_write("HardFault_Handler\r\n");
    while (1);
}
#else
void Default_Handler(void) {
    while (1);
}
#endif

void Reset_Handler(void) {
#if 0
    // RAMON and RAMONB registers. These are the default values (after
    // reset, but are retained), so don't change them. Saves 20 bytes.
    uint32_t * ram_on_addr   = (uint32_t *)0x40000524;
    uint32_t * ram_on_b_addr = (uint32_t *)0x40000554;
    // RAM on in on-mode
    *ram_on_addr   = 3; // block 0 and 1
    *ram_on_b_addr = 3; // block 2 and 3
#endif

#if 0
    // RAM on in off-mode
    ram_on_addr   = 1 << 16;
    ram_on_b_addr = 1 << 17;
#endif

#if BOOTLOADER_IN_MBR
    // Initialize .data segment. By avoiding non-zero non-const values, we
    // can also avoid this startup code. Saves 36 bytes.
    uint32_t * p_src  = &_sidata;
    uint32_t * p_dest = &_sdata;

    while (p_dest < &_edata) {
      *p_dest++ = *p_src++;
    }
#endif

    // Initialize .bss segment.
    uint32_t * p_bss     = &_sbss;
    uint32_t * p_bss_end = &_ebss;
    while (p_bss < p_bss_end) {
        *p_bss++ = 0ul;
    }

    _start();
}

void NMI_Handler            (void) __attribute__ ((weak, alias("Default_Handler")));
void HardFault_Handler      (void) __attribute__ ((weak, alias("Default_Handler")));
void SVC_Handler            (void) __attribute__ ((weak, alias("Default_Handler")));
void PendSV_Handler         (void) __attribute__ ((weak, alias("Default_Handler")));
void SysTick_Handler        (void) __attribute__ ((weak, alias("Default_Handler")));

void POWER_CLOCK_IRQHandler (void) __attribute__ ((weak, alias("Default_Handler")));
void RADIO_IRQHandler       (void) __attribute__ ((weak, alias("Default_Handler")));
void UART0_IRQHandler       (void) __attribute__ ((weak, alias("Default_Handler")));
void SPI0_TWI0_IRQHandler   (void) __attribute__ ((weak, alias("Default_Handler")));
void SPI1_TWI1_IRQHandler   (void) __attribute__ ((weak, alias("Default_Handler")));
void GPIOTE_IRQHandler      (void) __attribute__ ((weak, alias("Default_Handler")));
void ADC_IRQHandler         (void) __attribute__ ((weak, alias("Default_Handler")));
void TIMER0_IRQHandler      (void) __attribute__ ((weak, alias("Default_Handler")));
void TIMER1_IRQHandler      (void) __attribute__ ((weak, alias("Default_Handler")));
void TIMER2_IRQHandler      (void) __attribute__ ((weak, alias("Default_Handler")));
void RTC0_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void TEMP_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void RNG_IRQHandler         (void) __attribute__ ((weak, alias("Default_Handler")));
void ECB_IRQHandler         (void) __attribute__ ((weak, alias("Default_Handler")));
void CCM_AAR_IRQHandler     (void) __attribute__ ((weak, alias("Default_Handler")));
void WDT_IRQHandler         (void) __attribute__ ((weak, alias("Default_Handler")));
void RTC1_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void QDEC_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void LPCOMP_IRQHandler      (void) __attribute__ ((weak, alias("Default_Handler")));
void SWI0_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void SWI1_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void SWI2_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void SWI3_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void SWI4_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));
void SWI5_IRQHandler        (void) __attribute__ ((weak, alias("Default_Handler")));

const func __Vectors[] __attribute__ ((section(".isr_vector"),used)) = {
    (func)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    // Dirty hack to save space: the following IRQs aren't used by the
    // bootloader so we can put anything in this space. It saves 152
    // bytes.
    // TODO: make sure only even values get written here (e.g. by
    // selecting readonly literals). This to ensure that even if an
    // interrupt gets called here, the CPU will fault (as function
    // pointers must always have the lowest bit set in Thumb mode).
#if BOOTLOADER_IN_MBR
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    SVC_Handler,
    0,
    0,
    PendSV_Handler,
    SysTick_Handler,

    /* External Interrupts */
    POWER_CLOCK_IRQHandler,
    RADIO_IRQHandler,
    UART0_IRQHandler,
    SPI0_TWI0_IRQHandler,
    SPI1_TWI1_IRQHandler,
    0,
    GPIOTE_IRQHandler,
    ADC_IRQHandler,
    TIMER0_IRQHandler,
    TIMER1_IRQHandler,
    TIMER2_IRQHandler,
    RTC0_IRQHandler,
    TEMP_IRQHandler,
    RNG_IRQHandler,
    ECB_IRQHandler,
    CCM_AAR_IRQHandler,
    WDT_IRQHandler,
    RTC1_IRQHandler,
    QDEC_IRQHandler,
    LPCOMP_IRQHandler,
    SWI0_IRQHandler,
    SWI1_IRQHandler,
    SWI2_IRQHandler,
    SWI3_IRQHandler,
    SWI4_IRQHandler,
    SWI5_IRQHandler
#endif
};
