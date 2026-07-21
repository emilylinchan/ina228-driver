/**
 * @file    ina228.c
 * @brief   Platform-independent driver for the TI INA228 power monitor.
 *
 * All hardware access goes through the function pointers in ina228_t.
 * Endianness note: the INA228 transmits registers MSB-first (big-endian);
 * this driver reconstructs values byte-by-byte, so it is correct on both
 * little- and big-endian hosts.
 */

#include "ina228.h"
#include <stddef.h>

// ======================================================================
// Internal Helpers
// ======================================================================

/** Validate that the descriptor has the required callbacks set. */
static ina228_status_t check_dev(const ina228_t *dev)
{
    if (dev == NULL || dev->i2c_read == NULL || dev->i2c_write == NULL) {
        return INA228_ERR_NULL;
    }
    return INA228_OK;
}

/** Write a 16-bit register (MSB first). */
static ina228_status_t write_reg16(ina228_t *dev, uint8_t reg, uint16_t value)
{
    uint8_t data[2];
    data[0] = (uint8_t)((value >> 8) & 0xFF);
    data[1] = (uint8_t)(value & 0xFF);

    if (dev->i2c_write(dev->ctx, dev->dev_addr, reg, data, 2) != 0) {
        return INA228_ERR_I2C;
    }
    return INA228_OK;
}

/** Read a 16-bit register (MSB first). */
static ina228_status_t read_reg16(ina228_t *dev, uint8_t reg, uint16_t *value)
{
    uint8_t data[2];

    if (dev->i2c_read(dev->ctx, dev->dev_addr, reg, data, 2) != 0) {
        return INA228_ERR_I2C;
    }
    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return INA228_OK;
}

/**
 * Read a 24-bit register that carries 20-bit signed data in bits [23:4]
 * (VBUS, VSHUNT, CURRENT). Bits [3:0] are reserved and discarded.
 * The 20-bit value is sign-extended to 32 bits (two's complement).
 */
static ina228_status_t read_reg24_20bit(ina228_t *dev, uint8_t reg, int32_t *value)
{
    uint8_t data[3];

    if (dev->i2c_read(dev->ctx, dev->dev_addr, reg, data, 3) != 0) {
        return INA228_ERR_I2C;
    }

    int32_t raw = ((int32_t)data[0] << 16) |
                  ((int32_t)data[1] << 8)  |
                  (int32_t)data[2];

    // Discard reserved bits [3:0]
    raw >>= 4;

    // Sign-extend from 20 bits: if bit 19 is set, fill the upper 12 bits
    if (raw & 0x00080000) {
        raw |= (int32_t)0xFFF00000;
    }

    *value = raw;
    return INA228_OK;
}

/** Read a 24-bit register that uses all 24 bits, unsigned (POWER). */
static ina228_status_t read_reg24_full(ina228_t *dev, uint8_t reg,
                                       uint32_t *value)
{
    uint8_t data[3];

    if (dev->i2c_read(dev->ctx, dev->dev_addr, reg, data, 3) != 0) {
        return INA228_ERR_I2C;
    }

    *value = ((uint32_t)data[0] << 16) |
             ((uint32_t)data[1] << 8)  |
             (uint32_t)data[2];
    return INA228_OK;
}

/** Calculate SHUNT_CAL register value (datasheet 8.1.2 Eq. 2, ADCRANGE = 0). */
static uint16_t calc_shunt_cal(float current_lsb, float shunt_resistor)
{
    float cal = 13107200000.0f * current_lsb * shunt_resistor;
    return (uint16_t)cal;
}

// ======================================================================
// Public API
// ======================================================================

ina228_status_t ina228_init(ina228_t *dev)
{
    ina228_status_t status = check_dev(dev);
    if (status != INA228_OK) return status;

    if (dev->shunt_resistor <= 0.0f || dev->max_current <= 0.0f) {
        return INA228_ERR_CONFIG;
    }

    // Derive conversion factors (datasheet Section 8.1.2 Equations 3 & 5)
    dev->current_lsb = dev->max_current / 524288.0f;
    dev->power_lsb   = 3.2f * dev->current_lsb;

    // Software reset
    status = write_reg16(dev, INA228_REG_CONFIG, INA228_CONFIG_RST);
    if (status != INA228_OK) return status;

    if (dev->delay_ms != NULL) {
        dev->delay_ms(dev->ctx, 10);
    }

    // General configuration
    status = write_reg16(dev, INA228_REG_CONFIG,
                         INA228_CONFIG_ADCRANGE | INA228_CONFIG_CONVDLY_0);
    if (status != INA228_OK) return status;

    // ADC configuration
    status = write_reg16(dev, INA228_REG_ADC_CONFIG,
                         INA228_ADC_MODE_CONT_ALL |
                         INA228_ADC_VBUSCT_1052us |
                         INA228_ADC_VSHCT_1052us  |
                         INA228_ADC_VTCT_1052us   |
                         INA228_ADC_AVG_64);
    if (status != INA228_OK) return status;

    // Set shunt calibration
    return write_reg16(dev, INA228_REG_SHUNT_CAL,
                       calc_shunt_cal(dev->current_lsb, dev->shunt_resistor));
}

ina228_status_t ina228_verify_id(ina228_t *dev)
{
    ina228_status_t status = check_dev(dev);
    if (status != INA228_OK) return status;

    uint16_t id;
    status = read_reg16(dev, INA228_REG_MANUFACTURER_ID, &id);
    if (status != INA228_OK) return status;

    return (id == INA228_MANUFACTURER_ID) ? INA228_OK : INA228_ERR_BAD_ID;
}

ina228_status_t ina228_read_voltage(ina228_t *dev, float *voltage)
{
    ina228_status_t status = check_dev(dev);
    if (status != INA228_OK) return status;
    if (voltage == NULL) return INA228_ERR_NULL;

    int32_t raw;
    status = read_reg24_20bit(dev, INA228_REG_VBUS, &raw);
    if (status != INA228_OK) return status;

    *voltage = (float)raw * INA228_VBUS_LSB;
    return INA228_OK;
}

ina228_status_t ina228_read_current(ina228_t *dev, float *current)
{
    ina228_status_t status = check_dev(dev);
    if (status != INA228_OK) return status;
    if (current == NULL) return INA228_ERR_NULL;

    int32_t raw;
    status = read_reg24_20bit(dev, INA228_REG_CURRENT, &raw);
    if (status != INA228_OK) return status;

    *current = (float)raw * dev->current_lsb;
    return INA228_OK;
}

ina228_status_t ina228_read_power(ina228_t *dev, float *power)
{
    ina228_status_t status = check_dev(dev);
    if (status != INA228_OK) return status;
    if (power == NULL) return INA228_ERR_NULL;

    uint32_t raw;
    status = read_reg24_full(dev, INA228_REG_POWER, &raw);
    if (status != INA228_OK) return status;

    *power = (float)raw * dev->power_lsb;
    return INA228_OK;
}

ina228_status_t ina228_is_healthy(ina228_t *dev, uint8_t *healthy)
{
    ina228_status_t status = check_dev(dev);
    if (status != INA228_OK) return status;
    if (healthy == NULL) return INA228_ERR_NULL;

    uint16_t diag;
    status = read_reg16(dev, INA228_REG_DIAG_ALRT, &diag);
    if (status != INA228_OK) {
        *healthy = 0;
        return status;
    }

    *healthy = (diag & INA228_DIAG_MEMSTAT) ? 1 : 0;
    return INA228_OK;
}

ina228_status_t ina228_conversion_ready(ina228_t *dev, uint8_t *ready)
{
    ina228_status_t status = check_dev(dev);
    if (status != INA228_OK) return status;
    if (ready == NULL) return INA228_ERR_NULL;

    uint16_t diag;
    status = read_reg16(dev, INA228_REG_DIAG_ALRT, &diag);
    if (status != INA228_OK) return status;

    *ready = (diag & INA228_DIAG_CNVRF) ? 1 : 0;
    return INA228_OK;
}

ina228_status_t ina228_configure_alerts(ina228_t *dev,
                                        float overvoltage_limit,
                                        float undervoltage_limit,
                                        float overcurrent_limit)
{
    ina228_status_t status = check_dev(dev);
    if (status != INA228_OK) return status;

    // Overvoltage
    uint16_t bovl = (uint16_t)(overvoltage_limit / INA228_BOVL_LSB);
    status = write_reg16(dev, INA228_REG_BOVL, bovl);
    if (status != INA228_OK) return status;

    // Undervoltage
    uint16_t buvl = (uint16_t)(undervoltage_limit / INA228_BUVL_LSB);
    status = write_reg16(dev, INA228_REG_BUVL, buvl);
    if (status != INA228_OK) return status;

    // Overcurrent: SOVL is a shunt over-voltage limit, so convert the
    // current threshold to a shunt voltage via V = I * R_shunt.
    float shunt_voltage_limit = overcurrent_limit * dev->shunt_resistor;
    uint16_t sovl = (uint16_t)(shunt_voltage_limit / INA228_SOVL_LSB);
    return write_reg16(dev, INA228_REG_SOVL, sovl);
}
