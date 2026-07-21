# INA228 Driver — STM32 Example

A minimal STM32CubeIDE project demonstrating the platform-independent
INA228 driver on an STM32F446RE (Nucleo-F446RE). The firmware streams
timestamped voltage, current, and power samples over UART; a companion
Python script (`data_log.py`) receives them and plots live.

Watch a [live demo](https://youtu.be/eXcZ65j4AdI?si=dodTMjaqQwvwUrAF).

## Hardware

- **MCU:** STM32F446RE (Nucleo-F446RE)
- **Sensor:** TI INA228 power monitor on I2C
- **Connections:**
  - INA228 SDA/SCL → I2C1 (see `data_log.ioc` for the exact pins)
  - USART2 → ST-Link virtual COM port (115200 baud, 8-N-1)

## Sensor Configuration

Set in the `Private define` block near the top of `Core/Src/main.c`:

| Define              | Value    | Meaning                          |
|---------------------|----------|----------------------------------|
| `INA228_ADDR`       | `0x40`   | 7-bit I2C address (0x40–0x4F)    |
| `INA228_SHUNT_OHMS` | `0.015f` | Shunt resistor value (Ohms)      |
| `INA228_MAX_AMPS`   | `1.0f`   | Max expected current (A)         |

The ADC is configured in `ina228_init` for continuous conversion of all
channels with 1.052 ms conversion times and 64× averaging, giving an
effective output rate of roughly **5 Hz** (~202 ms per sample). Reduce
the averaging or conversion time in the driver if you need faster
sampling (at the cost of noise/precision).

## Building

This example **does not include** the CubeIDE-generated `Drivers/`
folder (STM32 HAL + CMSIS). Regenerate it before building:

1. Open `data_log.ioc` in STM32CubeMX or STM32CubeIDE.
2. Click **Generate Code**. This repopulates `Drivers/` with the HAL
   and CMSIS sources matching the project configuration.
3. Build and flash from CubeIDE.

## Serial Protocol

The PC starts a capture by sending an ASCII command terminated with `\n`:

```text
START,<total_time_seconds>
```

The MCU replies `OK\n` (or `ERR\n` on a malformed command), then streams
one line per sample:

```text
<ms_since_start>,<voltage>,<current>,<power>
```

- `ms_since_start` — milliseconds since the capture began (MCU timestamp)
- `voltage` — voltage in volts
- `current` — current in amps (signed)
- `power` — power in watts

When the duration elapses the MCU sends `DONE\n`. If the sensor fails a
health check it sends `FAULT_SENSOR_COMM\n` and stops.

## Running the Logger

```bash
pip install pyserial matplotlib
python data_log.py
```

Edit `SERIAL_PORT` at the top of `data_log.py` to match your board's COM
port, then run it and enter a capture duration when prompted. Voltage,
current, and power appear in three live, independently-scaled plots.

## Files

| Path                        | Purpose                                     |
|-----------------------------|---------------------------------------------|
| `Core/`                     | Application + HAL init (`main.c`, etc.)     |
| `data_log.py`               | PC-side serial logger and live plotter      |
| `data_log.ioc`              | CubeMX configuration (regenerate `Drivers/`)|
| `STM32F446RETX_FLASH.ld`    | Flash linker script                         |
| `STM32F446RETX_RAM.ld`      | RAM linker script                           |
| `.project` / `.cproject`    | STM32CubeIDE project files                  |