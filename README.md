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
Override defaults if needed:


make KERNELDIR=/path/to/kernel CROSS_COMPILE=/path/to/toolchain-
Installation
Copy the kernel module to the Pi:

cp ds18B20_driver.ko /root/
Merge the overlay into the base device tree:

fdtoverlay -i bcm2836-rpi-2-b.dtb -o bcm2836-rpi-2-b.dtb ds18B20_dt_overlay.dtbo
Load the module:

insmod /root/ds18B20_driver.ko
Read the temperature:

cat /dev/DS18B20
License
GPL-2.0


