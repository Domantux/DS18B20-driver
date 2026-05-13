# DS18B20 Driver — Code Explanation

## Device Tree Overlay

The overlay (`ds18B20_dt_overlay.dts`) declares the sensor as a device node with the compatible string `maxim,ds18b20-dev` and assigns GPIO 26 as the data pin. The kernel uses this to match the device to the driver at boot.

## Driver Initialization (`dev_probe`)

When the kernel matches the compatible string, `dev_probe` is called:
1. Acquires GPIO 26 as output-low using `devm_gpiod_get` — the `devm_` prefix means the kernel automatically releases it when the driver is removed
2. Allocates a character device number with `alloc_chrdev_region`
3. Creates a device class and device file under `/dev/DS18B20` with read-only permissions (`0444`) via the `ds18b20_devnode` callback
4. Registers the character device with the kernel

## 1-Wire Protocol

The DS18B20 uses the Dallas/Maxim 1-Wire protocol — a single-wire bus where the master (Pi) communicates with the sensor by controlling line timing. The line is normally high via a pull-up resistor. The master communicates by pulling it low for specific durations.

### Reset pulse (`reset`)

- Master pulls the line low for 480µs, then releases
- Sensor responds by pulling the line low for 60–240µs (presence pulse)
- Master samples the line at 60µs after releasing — if low, sensor is present
- Total high time is 480µs (60µs sample + 420µs wait)

### Write bit (`write_byte`)

- **Write 0**: pull low for 60µs, release for 1µs recovery
- **Write 1**: pull low for 1µs, release for 60µs
- Sensor samples the line 15–30µs after the falling edge
- Bits are sent LSB first, 8 bits per byte

### Read bit (`read_byte`)

- Master pulls low for 1µs to initiate the read slot
- Releases the line, waits 2µs for the line to settle
- Samples: high = bit 1 (sensor released), low = bit 0 (sensor holding)
- Total slot duration: 60µs

## Temperature Reading (`temp_read`)

Follows the DS18B20 communication sequence for a single sensor (Skip ROM):

1. Reset + presence check
2. Send `0xCC` (Skip ROM) — addresses all sensors without ROM matching, works when only one sensor is on the bus
3. Send `0x44` (Convert T) — triggers temperature conversion
4. Wait 750ms (maximum conversion time at 12-bit resolution per datasheet)
5. Reset + presence check
6. Send `0xCC` (Skip ROM) + `0xBE` (Read Scratchpad)
7. Read 9 bytes from scratchpad
8. Verify CRC — if invalid, discard and return error

## CRC Validation

The DS18B20 appends a CRC byte (byte 9) computed over the first 8 scratchpad bytes using the Dallas/Maxim CRC-8 algorithm (polynomial 0x31, reflected: 0x8c). Running the same CRC over all 9 bytes including the CRC byte produces 0 for uncorrupted data. If non-zero, the data was corrupted in transmission.

## Temperature Conversion

The scratchpad bytes 0–1 contain a signed 16-bit two's complement value in 12-bit resolution mode, where each LSB = 0.0625°C (1/16°C). Converted to millidegrees:

```
millidegrees = (s16)raw * 1000 / 16
```

The `s16` cast is critical — without it, negative temperatures (below 0°C) would be interpreted as large positive numbers due to the unsigned nature of the bit shift. This method is identical to the Linux kernel's built-in `w1-therm` driver.

## Userspace Interface (`driver_read`)

When `cat /dev/DS18B20` is run:
1. `driver_open` acquires the mutex and calls `temp_read`
2. `driver_read` formats millidegrees: whole part = `temp / 1000`, fractional = `abs(temp % 1000) / 10`
3. Result is copied to userspace with `copy_to_user`
4. `*off` is updated — prevents `cat` from reading repeatedly in one call

## Mutex Protection

A mutex in `driver_open` ensures only one process can trigger a sensor read at a time, preventing concurrent 1-Wire bus access which would corrupt communication. `mutex_lock_interruptible` allows the process to be interrupted by a signal while waiting.

## Limitations

- Only supports a single DS18B20 on the bus (uses Skip ROM — no ROM matching)
- `udelay`/`mdelay` timing is not real-time — interrupts and scheduling can occasionally corrupt 1-Wire timing
- For production use, consider the kernel's built-in `w1-gpio` + `w1-therm` subsystem
