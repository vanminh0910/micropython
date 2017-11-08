# Bootloader/DFU

This is a small bootloader, currently only for the nRF51822 with SoftDevice
s110. It allows firmware updates over a BLE connection. The main advantage is
it's small size: it can be configured to be smaller than a single page (1024
bytes, currently about 800 bytes). Aside from being a bootloader (just like the
[Nordic DFU example](https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.sdk5.v14.1.0%2Fexamples_bootloader.html&cp=4_0_0_4_4),
it can also be used in place of the SoftDevice MBR.

In normal operation, it immediately jumps to MicroPython. But from MicroPython
there is a call (`machine.reset(1)`) which resets with a flag to stay in DFU
mode.

## Bootloader in MBR

A feature (which as far as I'm aware doesn't exist in other bootloaders) is that
it can also be stored in the MBR region. In this mode, it receives all
interrupts but forwards all of them to the SoftDevice (except for the reset
handler). MBR SVCalls and other bootloaders are not (yet) supported when
installed this way. The advantage is that it doesn't take up any extra space as
there is always an MBR with current SoftDevices.

## Installing

For the bootloader mode:

    make BOARD=foo SD=s1XX bootloader

For installation in the MBR region:

    make BOARD=foo SD=s1XX MBR=1 bootloader

Then flash the resulting bootloader.hex file using your JTAG/SWD programmer.

  * For MBR mode, you have to flash it *after* flashing the SoftDevice.
  * For bootloader mode, it needs to set an UICR register. This register cannot
    be changed afterwards, so if you adjust the start address (e.g. for
    debugging) you have to run a mass erase before flashing it. And of course it
    needs a SoftDevice to run.

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
  * The ISR vector is shortened to 4 words in bootloader mode, saving a lot of
    space. This includes the initial stack pointer and the HardFault handler.
    Other interrupts should not happen, I hope.
  * There is no `.data` section in bootloader mode. Instead, all global values
    should either be constant (stored in `.text` in flash) or zero (stored in
    `.bss` and initialized by the startup code). The `.bss` segment is
    initialized, however. Most of the time, avoiding gobal non-const data saves
    code size anyway. For MBR mode, it cannot use data in flash as the
    SoftDevice won't accept addresses in that area (< 0x1000), so they all have
    to be stored in RAM.
  * Product anomalies are ignored. If they are relevant to the bootloader, they
    can be implemented.
  * Memory regions are not enabled by default. With the initial values (after
    reset) they are already enabled. This should be safe as long as no reset
    happens while memory regions are disabled.
