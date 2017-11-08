# Bootloader/DFU

This is a small bootloader, currently only for the nRF51822 with SoftDevice
s110. It allows firmware updates over a BLE connection. The main advantage is
it's small size: it can be configured to be smaller than a single page (1024
bytes, currently about 800 bytes).

In normal operation, it immediately jumps to MicroPython. But from MicroPython
there is a call (machine.bootloader()) which resets with a flag to stay in
bootloader mode.

## Bluetooth API

It advertizes a service (`67fc0001-83ae-f58c-f84b-ba72efb822f4`) with two
characteristics: an info characteristic with various parameters (flash size,
page size, application start address, etc.) and a write/notify characteristic
for calls and return values. With that, an application can erase pages and write
new pages. Finally, it can call a reset to enter the bootloader.

There is no verification of any kind implemented yet. Security relies on the
fact that the bootloader can only be entered via a command in the running
firmware or as long as the first page of the firmware (the ISR vector) is
cleared.

## Optimizations

To get to this low size, some optimizations are implemented, some of which
are... let's say unconventional.

  * Code immediately follows the ISR vector.
  * The ISR vector is shortened to 4 words, saving a lot of space. This includes
    the initial stack pointer and the HardFault handler. Other interrupts should
    not happen, I hope.
  * There is no `.data` section. Instead, all global values should either be
    constant (stored in `.text` in flash) or zero (stored in `.bss` and
    initialized by the startup code). The `.bss` segment is initialized,
    however. Most of the time, avoiding gobal non-const data saves code size
    anyway.
  * Product anomalies are ignored. If they are relevant to the bootloader, they
    can be implemented.
  * Memory regions are not enabled by default. With the initial values (after
    reset) they are already enabled. This should be safe as long as no reset
    happens while memory regions are disabled.
