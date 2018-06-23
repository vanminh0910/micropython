# MicroPython port to Mynewt

This is a bare-bones port of [MicroPython](https://micropython.org/) to
[Apache Mynewt](https://mynewt.apache.org/), including its
[NimBLE](https://mynewt.apache.org/network/ble/ble_intro/) BLE stack.

As said, it is a very bare bones port. The REPL works but no hardware can be
accessed yet. There is also support for the NUS console (giving a usable REPL).

## Hardware support

At the moment, only the
[nRF52 DK](https://www.nordicsemi.com/eng/Products/Bluetooth-low-energy/nRF52-DK)
has been tested. It might work on other devices as well.

## Compile and flash

 1. Install the
    [newt](https://mynewt.apache.org/develop/get_started/native_install/index.html)
    command line tool.
 2. Run `newt install`.
 3. Run `make` and `make flash`.
