/**
 * @file    ina228.h
 * @brief   Platform-independent driver for the TI INA228 85-V, 20-Bit,
 *          Ultra-Precise Power/Energy/Charge Monitor With I2C Interface.
 */

#ifndef INA228_H
#define INA228_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ======================================================================
// Return Codes
// ======================================================================

typedef enum {
    INA228_OK          =  0,  // Success
    INA228_ERR_I2C     = -1,  // Bus transaction failed
    INA228_ERR_NULL    = -2,  // NULL pointer argument
    INA228_ERR_BAD_ID  = -3,  // Manufacturer ID mismatch
    INA228_ERR_CONFIG  = -4,  // Invalid configuration parameter
} ina228_status_t;

// ======================================================================
// Platform Interface
// ======================================================================

/**
 * @brief Read `len` bytes from register `reg` of device `dev_addr`.
 * @param ctx       Platform-specific I2C handle/context.
 * @param dev_addr  7-bit I2C device address.
 * @param reg       Register address to read from.
 * @param data      Destination buffer.
 * @param len       Number of bytes to read.
 * @return 0 on success, non-zero on failure.
 */
typedef int8_t (*ina228_i2c_read_fn)(void *ctx, uint8_t dev_addr, uint8_t reg,
                                     uint8_t *data, uint16_t len);

/**
 * @brief Write `len` bytes to register `reg` of device `dev_addr`.
 * @return 0 on success, non-zero on failure.
 */
typedef int8_t (*ina228_i2c_write_fn)(void *ctx, uint8_t dev_addr, uint8_t reg,
                                      const uint8_t *data, uint16_t len);

/**
 * @brief Blocking delay in milliseconds.
 */
typedef void (*ina228_delay_ms_fn)(void *ctx, uint32_t ms);

// ======================================================================
// Device Descriptor
// ======================================================================

typedef struct {
    // Platform interface (must be set by user)
    ina228_i2c_read_fn  i2c_read;
    ina228_i2c_write_fn i2c_write;
    ina228_delay_ms_fn  delay_ms;
    void               *ctx;

    // Device configuration (must be set by user)
    uint8_t dev_addr;               // 7-bit I2C address (0x40..0x4F)
    float   shunt_resistor;         // Shunt value (Ohms))
    float   max_current;            // Max expected current (A)

    // Conversion factors (calculated upon init)
    float   current_lsb;            // max_current / 2^19  (A/bit)
    float   power_lsb;              // current_lsb * 3.2   (W/bit)
} ina228_t;

// ======================================================================
// Register Maps (datasheet Section 7.6)
// ======================================================================

#define INA228_REG_CONFIG           0x00
#define INA228_REG_ADC_CONFIG       0x01
#define INA228_REG_SHUNT_CAL        0x02
#define INA228_REG_SHUNT_TEMPCO     0x03
#define INA228_REG_VSHUNT           0x04
#define INA228_REG_VBUS             0x05
#define INA228_REG_DIETEMP          0x06
#define INA228_REG_CURRENT          0x07
#define INA228_REG_POWER            0x08
#define INA228_REG_ENERGY           0x09
#define INA228_REG_CHARGE           0x0A
#define INA228_REG_DIAG_ALRT        0x0B
#define INA228_REG_SOVL             0x0C
#define INA228_REG_SUVL             0x0D
#define INA228_REG_BOVL             0x0E
#define INA228_REG_BUVL             0x0F
#define INA228_REG_TEMP_LIMIT       0x10
#define INA228_REG_PWR_LIMIT        0x11
#define INA228_REG_MANUFACTURER_ID  0x3E
#define INA228_REG_DEVICE_ID        0x3F

#define INA228_MANUFACTURER_ID      0x5449  // "TI" in ASCII

// CONFIG register bits
#define INA228_CONFIG_RST           (1u << 15) // Software reset
#define INA228_CONFIG_CONVDLY_0     (0u << 6)  // ADC conversion delay = 0 ms
#define INA228_CONFIG_ADCRANGE      (0u << 4)  // +/-163.84 mV shunt full scale range

// ADC_CONFIG register bits
#define INA228_ADC_MODE_CONT_ALL    (0x0Fu << 12) // Continuous mode, all measurements
#define INA228_ADC_VBUSCT_1052us    (0x05u << 9)  // Bus conversion time 1.052 ms
#define INA228_ADC_VSHCT_1052us     (0x05u << 6)  // Shunt conversion time 1.052 ms
#define INA228_ADC_VTCT_1052us      (0x05u << 3)  // Temp conversion time 1.052 ms
#define INA228_ADC_AVG_64           (0x03u << 0)  // Average 64 samples

// DIAG_ALRT register bits
#define INA228_DIAG_MEMSTAT         (1u << 0)  // 1 = trim memory checksum OK
#define INA228_DIAG_CNVRF           (1u << 1)  // 1 = conversion complete

// Measurement conversion factors
#define INA228_VBUS_LSB             0.0001953125f // 195.3125 uV/bit
#define INA228_SUVL_LSB             0.000005f     // 5 uV/bit (ADCRANGE = 0)
#define INA228_SOVL_LSB             0.000005f     // 5 uV/bit (ADCRANGE = 0)
#define INA228_BOVL_LSB             0.003125f     // 3.125 mV/bit
#define INA228_BUVL_LSB             0.003125f     // 3.125 mV/bit

// ======================================================================
// Public API
// ======================================================================

/** @brief Reset and configure the device.
 *  @return INA228_OK on success, or INA228_ERR_CONFIG for an invalid
 *          shunt/current value, or another ina228_status_t error code.
 */
ina228_status_t ina228_init(ina228_t *dev);

/** @brief Read MANUFACTURER_ID and verify it equals 0x5449 ("TI").
 *  @return INA228_OK on success, or INA228_ERR_BAD_ID if the ID does not
 *          match, or another ina228_status_t error code.
 */
ina228_status_t ina228_verify_id(ina228_t *dev);

/** @brief Read bus voltage in volts.
 *  @return INA228_OK on success, or an ina228_status_t error code.
 */
ina228_status_t ina228_read_voltage(ina228_t *dev, float *voltage);

/** @brief Read current in amps (signed; negative = reverse flow).
 *  @return INA228_OK on success, or an ina228_status_t error code.
 */
ina228_status_t ina228_read_current(ina228_t *dev, float *current);

/** @brief Read power in watts (always positive).
 *  @return INA228_OK on success, or an ina228_status_t error code.
 */
ina228_status_t ina228_read_power(ina228_t *dev, float *power);

/**
 * @brief Check device health via the MEMSTAT bit of DIAG_ALRT.
 * @param healthy Set to 1 if in normal operation.
 * @return INA228_OK on success, or an ina228_status_t error code.
 */
ina228_status_t ina228_is_healthy(ina228_t *dev, uint8_t *healthy);

/** @brief Check whether a conversion has completed via the CNVRF bit.
 *  @param ready Set to 1 when conversion is complete.
 *  @return INA228_OK on success, or an ina228_status_t error code.
 */
ina228_status_t ina228_conversion_ready(ina228_t *dev, uint8_t *ready);

/**
 * @brief Configure the over/under-voltage and shunt over-voltage
 *        (overcurrent) alert threshold registers.
 *
 * @param overvoltage_limit   Overvoltage threshold in volts (BOVL).
 * @param undervoltage_limit  Undervoltage threshold in volts (BUVL).
 * @param overcurrent_limit   Overcurrent threshold in amps.
 * @return INA228_OK on success, or an ina228_status_t error code.
 */
ina228_status_t ina228_configure_alerts(ina228_t *dev,
                                        float overvoltage_limit,
                                        float undervoltage_limit,
                                        float overcurrent_limit);

#ifdef __cplusplus
}
#endif

#endif // INA228_H
