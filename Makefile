ifneq ($(KERNELRELEASE),)
 obj-m := ds18B20_driver.o
else
 KERNELDIR ?= /home/vboxuser/Linux_tools/linux-master
 CROSS_COMPILE ?= /home/vboxuser/Documents/x-tools/armv7-rpi2-linux-musleabihf/bin/armv7-rpi2-linux-musleabihf-
 PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) modules

dtbo:
	dtc -@ -I dts -O dtb -o ds18B20_dt_overlay.dtbo ds18B20_dt_overlay.dts

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f ds18B20_dt_overlay.dtbo

endif