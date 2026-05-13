// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

static const struct of_device_id dev_id[] = {
	{ .compatible = "maxim,ds18b20-dev" },
	{}
};
MODULE_DEVICE_TABLE(of, dev_id);

static DEFINE_MUTEX(lock);

static dev_t device_nr;
static struct class *class;
static struct cdev device;
static struct gpio_desc *ds_sensor;
static int temp;

#define DRIVER_NAME		"DS18B20"
#define DS18B20_SKIP_ROM	0xCC
#define DS18B20_CONVERT_T	0x44
#define DS18B20_READ_SCRATCHPAD	0xBE

static int driver_open(struct inode *device_file, struct file *instance);
static ssize_t driver_read(struct file *file, char *user_buf, size_t len, loff_t *off);

/* Dallas/Maxim 1-Wire CRC-8, polynomial 0x31 (reflected: 0x8c) */
static u8 crc8_update(u8 crc, u8 data)
{
	u8 i;

	crc = crc ^ data;

	for (i = 0; i < 8; ++i) {
		if (crc & 0x01)
			crc = (crc >> 1) ^ 0x8c;
		else
			crc >>= 1;
	}
	return crc;
}

static u8 check_crc(u8 *cp, u8 length)
{
	u8 crc = 0x00;

	while (length--)
		crc = crc8_update(crc, *cp++);

	return crc;
}

static void write_byte(int value)
{
	for (int loop = 0; loop < 8; loop++) {
		gpiod_direction_output(ds_sensor, 0);
		if ((value & (1 << loop)) != 0) {
			udelay(1);
			gpiod_direction_input(ds_sensor);
			udelay(60);
		} else {
			udelay(60);
			gpiod_direction_input(ds_sensor);
			udelay(1);
		}
		udelay(60);
	}
	udelay(100);
}

static int read_byte(void)
{
	int b = 0;

	for (int loop = 0; loop < 8; loop++) {
		gpiod_direction_output(ds_sensor, 0);
		udelay(1);
		gpiod_direction_input(ds_sensor);
		udelay(2);
		if (gpiod_get_value(ds_sensor) == 1)
			b |= (1 << loop);
		udelay(60);
	}
	return b;
}

static int reset(void)
{
	int response;

	gpiod_direction_output(ds_sensor, 0);
	udelay(480);
	gpiod_direction_input(ds_sensor);
	udelay(60);

	response = gpiod_get_value(ds_sensor);
	udelay(420);

	return (response == 0) ? 1 : 0;
}

static int temp_read(void)
{
	u8 data[9];

	if (reset() == 1) {
		write_byte(DS18B20_SKIP_ROM);
		write_byte(DS18B20_CONVERT_T);
		mdelay(750);

		if (reset() == 1) {
			write_byte(DS18B20_SKIP_ROM);
			write_byte(DS18B20_READ_SCRATCHPAD);
			for (int i = 0; i < 9; i++)
				data[i] = read_byte();
			if (check_crc(data, 9) != 0) {
				pr_err("DS18B20: CRC check failed\n");
				return INT_MIN;
			}
		} else {
			pr_err("DS18B20: second reset failed\n");
			return INT_MIN;
		}
	} else {
		pr_err("DS18B20: initial reset failed - sensor not present\n");
		return INT_MIN;
	}
	return (int)(s16)((data[1] << 8) | data[0]) * 1000 / 16;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = driver_open,
	.read = driver_read
};

static char *ds18b20_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0444;
	return NULL;
}

static int dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pr_info("DS18B20: probe called\n");

	ds_sensor = devm_gpiod_get(dev, "ds18b20", GPIOD_OUT_LOW);
	if (!ds_sensor || IS_ERR(ds_sensor)) {
		pr_err("DS18B20: failed to get GPIO descriptor from device tree\n");
		return -ENODEV;
	}

	if (alloc_chrdev_region(&device_nr, 0, 1, DRIVER_NAME) < 0) {
		pr_err("DS18B20: failed to allocate device number\n");
		return -EBUSY;
	}
	pr_info("DS18B20: registered with major %d minor %d\n", MAJOR(device_nr), MINOR(device_nr));

	class = class_create(DRIVER_NAME);
	if (!class) {
		pr_err("DS18B20: failed to create device class\n");
		goto ClassError;
	}

	class->devnode = ds18b20_devnode;

	if (!device_create(class, NULL, device_nr, NULL, DRIVER_NAME)) {
		pr_err("DS18B20: failed to create device file\n");
		goto FileError;
	}

	cdev_init(&device, &fops);

	if (cdev_add(&device, device_nr, 1) < 0) {
		pr_err("DS18B20: failed to register device with kernel\n");
		goto AddError;
	}

	return 0;

AddError:
	device_destroy(class, device_nr);
FileError:
	class_destroy(class);
ClassError:
	unregister_chrdev_region(device_nr, 1);
	return -ENOMEM;
}

static int driver_open(struct inode *device_file, struct file *instance)
{
	if (mutex_lock_interruptible(&lock))
		return -EINTR;

	temp = temp_read();

	mutex_unlock(&lock);
	return 0;
}

static ssize_t driver_read(struct file *file, char *user_buf, size_t len, loff_t *off)
{
	int to_copy, not_copied, delta;
	static char buffer[32] = {0};

	if (*off > 0)
		return 0;

	if (temp == INT_MIN) {
		pr_err("DS18B20: temp_read failed\n");
		return -EIO;
	}

	snprintf(buffer, sizeof(buffer), "Temperature: %d.%02dC\n",
		 temp / 1000, abs(temp % 1000) / 10);

	to_copy = min_t(size_t, strlen(buffer), len);
	not_copied = copy_to_user(user_buf, buffer, to_copy);
	delta = to_copy - not_copied;
	*off += delta;
	return delta;
}

static void dev_remove(struct platform_device *pdev)
{
	cdev_del(&device);
	device_destroy(class, device_nr);
	class_destroy(class);
	unregister_chrdev_region(device_nr, 1);
	pr_info("DS18B20: driver removed\n");
}

static struct platform_driver dev_driver = {
	.probe = dev_probe,
	.remove = dev_remove,
	.driver = {
		.name = "DS18B20",
		.of_match_table = dev_id,
	}
};

module_platform_driver(dev_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Domantas Racys");
MODULE_VERSION("1.0.0");
MODULE_DESCRIPTION("DS18B20 temperature sensor driver");
