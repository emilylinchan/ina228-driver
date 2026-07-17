/**
 * @file    ina228_port_stm32.c
 * @brief   STM32 HAL port for the platform-independent INA228 driver.
 *
 * Wires the driver's I2C function pointers to the STM32 HAL. The I2C handle
 * (e.g. &hi2c1) is passed through the driver's opaque ctx pointer, so
 * multiple sensors on different buses need no code changes.
 *
 * Note on addressing: the portable driver uses unshifted 7-bit addresses
 * (0x40..0x44). The STM32 HAL expects the address pre-shifted left by one, 
 * so the shift happens here in the port, not in application code.
 */

#include "ina228.h"
#include "i2c.h"   // provides &hi2c1 (STM32CubeIDE generated code file)

#define INA228_I2C_TIMEOUT_MS  100

// ======================================================================
// Internal Helpers                                                    
// ======================================================================

static int8_t stm32_i2c_read(void *ctx, uint8_t dev_addr, uint8_t reg,
                             uint8_t *data, uint16_t len)
{
    return (HAL_I2C_Mem_Read((I2C_HandleTypeDef *)ctx,
                             (uint16_t)(dev_addr << 1), reg,
                             I2C_MEMADD_SIZE_8BIT, data, len,
                             INA228_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static int8_t stm32_i2c_write(void *ctx, uint8_t dev_addr, uint8_t reg,
                              const uint8_t *data, uint16_t len)
{
    return (HAL_I2C_Mem_Write((I2C_HandleTypeDef *)ctx,
                              (uint16_t)(dev_addr << 1), reg,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, len,
                              INA228_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static void stm32_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx; // No context needed for STM32
    HAL_Delay(ms);
}

// ======================================================================
// Public API                                                            
// ======================================================================

/**
 * @brief Convenience constructor for a sensor on hi2c1. 
 * @return A fully-populated ina228_t ready to pass to ina228_init().
 * 
 * Example:
 *   ina228_t bus_sensor = ina228_stm32_make(0x40, 0.003f, 50.0f);
 *   ina228_init(&bus_sensor);
 */
ina228_t ina228_stm32_make(uint8_t addr_7bit, float shunt_ohms, float max_amps)
{
    ina228_t dev = {
        .i2c_read       = stm32_i2c_read,
        .i2c_write      = stm32_i2c_write,
        .delay_ms       = stm32_delay_ms,
        .ctx            = &hi2c1,
        .dev_addr       = addr_7bit,
        .shunt_resistor = shunt_ohms,
        .max_current    = max_amps,
    };
    return dev;
}