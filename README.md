# INA228 Driver

Platform-independent C99 driver for the TI INA228 — an 85 V, 20-bit
power/energy/charge monitor with an I2C interface.

Portability comes from using a generic `void *ctx` pointer, which represents the platform-specific context used to access the I²C peripheral. The driver never interprets `ctx` directly—it simply passes it to the platform-specific I²C read/write functions.

Typical contexts:

- **STM32:** `I2C_HandleTypeDef *`
- **Linux:** an I2C file descriptor (usually wrapped in a small struct)
- **ESP-IDF:** `i2c_master_bus_handle_t`

## Repository Structure

| Path | Description |
| --- | --- |
| [`inc/`](inc) | Public header (`ina228.h`) |
| [`src/`](src) | Driver implementation (`ina228.c`) |
| [`tests/`](tests) | Host-side unit tests (mock I2C bus) |
| [`examples/`](examples) | Platform port examples (see [stm32](examples/stm32)) |

> 🎬 **[Watch the video demo](https://www.youtube.com/watch?v=eXcZ65j4AdI)** with the implementation running on real hardware!

## Requirements

- CMake ≥ 3.15
- A C99 compiler (GCC/Clang/MSVC)

## Build & Test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with MSYS2/MinGW, pass a generator: `-G "MinGW Makefiles"`.

Run the test binary directly (add `.exe` on Windows):

```sh
./build/test_ina228
```

Remove compiled objects but keep the CMake cache with
`cmake --build build --target clean`; delete `build/` entirely for a clean
rebuild.

## Using the Driver

### Option 1: CMake Subdirectory

Copy the `ina228_driver` folder into your project, then in your top-level
`CMakeLists.txt`:

```cmake
add_subdirectory(ina228_driver)
target_link_libraries(your_target PRIVATE ina228)
```

### Option 2: Without CMake

Add `src/ina228.c` to your source list and `inc/` to your include paths.

### Wiring Up the Driver

1. Implement `ina228_i2c_read_fn`, `ina228_i2c_write_fn`, and
   `ina228_delay_ms_fn` for your platform.
2. Fill in an `ina228_t` descriptor (callbacks, `ctx`, `dev_addr`,
   `shunt_resistor`, `max_current`).
3. Call `ina228_init()`, then read measurements.

```c
#include "ina228.h"

ina228_t dev = { /* callbacks, ctx, dev_addr, shunt_resistor, max_current */ };
ina228_init(&dev);

float voltage, current, power;
ina228_read_voltage(&dev, &voltage);
ina228_read_current(&dev, &current);
ina228_read_power(&dev, &power);
```

> **NOTE**: The public header is wrapped in `extern "C"`, so it can be included directly
from C++ sources.

## License

[MIT](LICENSE) © 2026 Emily Chan