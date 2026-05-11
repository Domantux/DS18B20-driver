# DS18B20 Linux Kernel Driver

A Linux kernel character device driver for the DS18B20 1-Wire temperature sensor, targeting Raspberry Pi 2.

## Features

- Implements 1-Wire protocol via GPIO using the kernel `gpiod` API
- Device tree overlay for hardware description
- Exposes temperature readings via `/dev/DS18B20`
- Cross-compiled for ARMv7 (Raspberry Pi 2)

## Hardware Requirements

- Raspberry Pi 2 Model B
- DS18B20 temperature sensor
- 4.7kΩ pull-up resistor on the data line
- Data line connected to BCM GPIO 26

## Dependencies

- Linux kernel source tree
- ARMv7 cross-compiler toolchain
- `device-tree-compiler` (`dtc`)
- `u-boot-tools` (`mkimage`)

## Building

```bash
make                  # build kernel module
make dtbo             # compile device tree overlay
make clean            # clean build artifacts
