/**
 * @file    test_ina228.c
 * @brief   Host-side unit tests for the platform-independent INA228 driver.
 *
 * A mock I2C bus simulates the sensor's register file in memory. Registers
 * are stored as big-endian byte arrays, exactly as the real device would
 * transmit them, so the driver's byte reconstruction and 20-bit sign
 * extension are exercised end-to-end without any hardware.
 */

#include "ina228.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// ======================================================================
// Test Framework                                                  
// ======================================================================

static int tests_run    = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond, msg)                                              \
    do {                                                                    \
        tests_run++;                                                        \
        if (!(cond)) {                                                      \
            tests_failed++;                                                 \
            printf("  FAIL: %s (line %d): %s\n", __func__, __LINE__, msg);  \
        }                                                                   \
    } while (0)

#define ASSERT_EQ_INT(exp, act, msg)                                        \
    do {                                                                    \
        tests_run++;                                                        \
        if ((int)(exp) != (int)(act)) {                                     \
            tests_failed++;                                                 \
            printf("  FAIL: %s (line %d): %s (expected %d, got %d)\n",      \
                   __func__, __LINE__, msg, (int)(exp), (int)(act));        \
        }                                                                   \
    } while (0)

#define ASSERT_NEAR(exp, act, tol, msg)                                     \
    do {                                                                    \
        tests_run++;                                                        \
        if (fabsf((exp) - (act)) > (tol)) {                                 \
            tests_failed++;                                                 \
            printf("  FAIL: %s (line %d): %s (expected %f, got %f)\n",      \
                   __func__, __LINE__, msg, (double)(exp), (double)(act));  \
        }                                                                   \
    } while (0)

// ======================================================================
// Mock I2C Bus                                                            
// ======================================================================

#define MOCK_NUM_REGS  0x40
#define MOCK_REG_WIDTH 3   // Widest INA228 register is 3 bytes 
                           // (excl. 40-bit ENERGY/CHARGE, not used here)

typedef struct {
    uint8_t  regs[MOCK_NUM_REGS][MOCK_REG_WIDTH]; // Big endian
    uint8_t  reg_len[MOCK_NUM_REGS];              // Byte width of each register 
    int      fail_reads;                          // If nonzero, next reads fail (counts down) 
    int      fail_writes;                         // If nonzero, next writes fail (counts down) 
    int      read_count;
    int      write_count;
    uint8_t  last_dev_addr;
    uint32_t delay_total_ms; 
} mock_bus_t;

// Single global mock bus; its address is passed to the driver as the ctx pointer
static mock_bus_t bus;

static void mock_reset(void)
{
    memset(&bus, 0, sizeof(bus));

    // Default 16-bit register widths
    for (int i = 0; i < MOCK_NUM_REGS; i++) bus.reg_len[i] = 2;
    bus.reg_len[INA228_REG_VSHUNT]  = 3;
    bus.reg_len[INA228_REG_VBUS]    = 3;
    bus.reg_len[INA228_REG_CURRENT] = 3;
    bus.reg_len[INA228_REG_POWER]   = 3;

    // Power-on defaults
    bus.regs[INA228_REG_MANUFACTURER_ID][0] = 0x54; // "T"
    bus.regs[INA228_REG_MANUFACTURER_ID][1] = 0x49; // "I"
    bus.regs[INA228_REG_DIAG_ALRT][1]       = 0x01; // MEMSTAT = 1 (reset value)
}

static int8_t mock_i2c_read(void *ctx, uint8_t dev_addr, uint8_t reg,
                            uint8_t *data, uint16_t len)
{
    mock_bus_t *b = (mock_bus_t *)ctx;
    b->read_count++;
    b->last_dev_addr = dev_addr;

    if (b->fail_reads > 0) { b->fail_reads--; return -1; }
    if (reg >= MOCK_NUM_REGS || len > MOCK_REG_WIDTH) return -1;

    memcpy(data, b->regs[reg], len);
    return 0;
}

static int8_t mock_i2c_write(void *ctx, uint8_t dev_addr, uint8_t reg,
                             const uint8_t *data, uint16_t len)
{
    mock_bus_t *b = (mock_bus_t *)ctx;
    b->write_count++;
    b->last_dev_addr = dev_addr;

    if (b->fail_writes > 0) { b->fail_writes--; return -1; }
    if (reg >= MOCK_NUM_REGS || len > MOCK_REG_WIDTH) return -1;

    memcpy(b->regs[reg], data, len);
    return 0;
}

static void mock_delay_ms(void *ctx, uint32_t ms)
{
    ((mock_bus_t *)ctx)->delay_total_ms += ms;
}

// Helpers to read and write to the mock register file
static uint16_t mock_get_reg16(uint8_t reg)
{
    return (uint16_t)(((uint16_t)bus.regs[reg][0] << 8) | bus.regs[reg][1]);
}

static void mock_set_reg24_20bit(uint8_t reg, int32_t value20)
{
    uint32_t packed = ((uint32_t)value20 & 0x000FFFFF) << 4;
    bus.regs[reg][0] = (uint8_t)((packed >> 16) & 0xFF);
    bus.regs[reg][1] = (uint8_t)((packed >> 8) & 0xFF);
    bus.regs[reg][2] = (uint8_t)(packed & 0xFF);
}

static void mock_set_reg24_full(uint8_t reg, uint32_t value24)
{
    bus.regs[reg][0] = (uint8_t)((value24 >> 16) & 0xFF);
    bus.regs[reg][1] = (uint8_t)((value24 >> 8) & 0xFF);
    bus.regs[reg][2] = (uint8_t)(value24 & 0xFF);
}

// Return a device descriptor wired to the mock bus
static ina228_t make_dev(void)
{
    ina228_t dev = {
        .i2c_read       = mock_i2c_read,
        .i2c_write      = mock_i2c_write,
        .delay_ms       = mock_delay_ms,
        .ctx            = &bus,
        .dev_addr       = 0x40,
        .shunt_resistor = 0.003f,
        .max_current    = 50.0f,
    };
    return dev;
}

// ======================================================================
// Tests
// ======================================================================

static void test_init_writes_correct_calibration(void)
{
    mock_reset();
    ina228_t dev = make_dev();

    ASSERT_EQ_INT(INA228_OK, ina228_init(&dev), "init should succeed");

    // CURRENT_LSB = 50 / 2^19 = 9.5367e-5 A/bit
    // SHUNT_CAL   = 13107.2e6 * 9.5367e-5 * 0.003 = 3750 
    ASSERT_EQ_INT(3750, mock_get_reg16(INA228_REG_SHUNT_CAL), "SHUNT_CAL register value");
    ASSERT_NEAR(50.0f / 524288.0f, dev.current_lsb, 1e-9f, "derived current_lsb");
    ASSERT_NEAR(3.2f * dev.current_lsb, dev.power_lsb, 1e-9f, "derived power_lsb");

    // ADC_CONFIG should be written with the expected values
    uint16_t expected_adc = INA228_ADC_MODE_CONT_ALL | INA228_ADC_VBUSCT_1052us |
                            INA228_ADC_VSHCT_1052us  | INA228_ADC_VTCT_1052us   |
                            INA228_ADC_AVG_64;
    ASSERT_EQ_INT(expected_adc, mock_get_reg16(INA228_REG_ADC_CONFIG),
                  "ADC_CONFIG register value");

    // Reset settling delay should have been requested
    ASSERT_TRUE(bus.delay_total_ms >= 10, "reset delay >= 10 ms");
}

static void test_init_rejects_bad_config(void)
{
    mock_reset();
    ina228_t dev = make_dev();
    dev.shunt_resistor = 0.0f;
    ASSERT_EQ_INT(INA228_ERR_CONFIG, ina228_init(&dev),
                  "zero shunt resistor rejected");

    dev = make_dev();
    dev.i2c_write = NULL;
    ASSERT_EQ_INT(INA228_ERR_NULL, ina228_init(&dev),
                  "missing i2c_write rejected");
}

static void test_verify_id(void)
{
    mock_reset();
    ina228_t dev = make_dev();

    ASSERT_EQ_INT(INA228_OK, ina228_verify_id(&dev), "ID 0x5449 accepted");

    bus.regs[INA228_REG_MANUFACTURER_ID][0] = 0xDE;
    bus.regs[INA228_REG_MANUFACTURER_ID][1] = 0xAD;
    ASSERT_EQ_INT(INA228_ERR_BAD_ID, ina228_verify_id(&dev),
                  "wrong ID rejected");
}

static void test_read_voltage_positive(void)
{
    mock_reset();
    ina228_t dev = make_dev();
    ina228_init(&dev);

    // 61440 * 195.3125 uV = 12.0 V 
    mock_set_reg24_20bit(INA228_REG_VBUS, 61440);

    float v = 0.0f;
    ASSERT_EQ_INT(INA228_OK, ina228_read_voltage(&dev, &v), "read ok");
    ASSERT_NEAR(12.0f, v, 1e-4f, "12.0 V decoded from raw register bytes");
}

static void test_read_current_negative_sign_extension(void)
{
    mock_reset();
    ina228_t dev = make_dev();
    ina228_init(&dev);

    // Raw 20-bit value -100000 (reverse current). On the wire this is
    // two's complement 0xE7960 shifted into bits [23:4] = bytes E7 96 00.
    // Expected: -100000 * (50 / 2^19) = -9.5367... A
    mock_set_reg24_20bit(INA228_REG_CURRENT, -100000);

    float i = 0.0f;
    ASSERT_EQ_INT(INA228_OK, ina228_read_current(&dev, &i), "read ok");
    ASSERT_NEAR(-100000.0f * (50.0f / 524288.0f), i, 1e-3f,
                "negative current sign-extended correctly");
    ASSERT_TRUE(i < 0.0f, "current is negative");
}

static void test_read_current_max_negative(void)
{
    mock_reset();
    ina228_t dev = make_dev();
    ina228_init(&dev);

    // Most negative 20-bit value: -524288 -> exactly -max_current 
    mock_set_reg24_20bit(INA228_REG_CURRENT, -524288);

    float i = 0.0f;
    ina228_read_current(&dev, &i);
    ASSERT_NEAR(-50.0f, i, 1e-3f, "full-scale negative current");
}

static void test_read_power(void)
{
    mock_reset();
    ina228_t dev = make_dev();
    ina228_init(&dev);

    // 100000 * 3.2 * (50 / 2^19) = 30.5175... W (power unsigned)
    mock_set_reg24_full(INA228_REG_POWER, 100000);

    float p = 0.0f;
    ASSERT_EQ_INT(INA228_OK, ina228_read_power(&dev, &p), "read ok");
    ASSERT_NEAR(100000.0f * 3.2f * (50.0f / 524288.0f), p, 1e-2f,
                "power decoded from full 24-bit register");
}

static void test_health_memstat(void)
{
    mock_reset();
    ina228_t dev = make_dev();

    uint8_t healthy = 0xFF;

    // MEMSTAT = 1 (mock default) -> healthy
    ASSERT_EQ_INT(INA228_OK, ina228_is_healthy(&dev, &healthy), "read ok");
    ASSERT_EQ_INT(1, healthy, "MEMSTAT=1 reports healthy");

    // MEMSTAT = 0 -> trim memory checksum error
    bus.regs[INA228_REG_DIAG_ALRT][1] = 0x00;
    ina228_is_healthy(&dev, &healthy);
    ASSERT_EQ_INT(0, healthy, "MEMSTAT=0 reports unhealthy");

    // CNVRF (bit 1) set but MEMSTAT clear -> still unhealthy 
    bus.regs[INA228_REG_DIAG_ALRT][1] = 0x02;
    ina228_is_healthy(&dev, &healthy);
    ASSERT_EQ_INT(0, healthy, "CNVRF alone does not imply healthy");

    uint8_t ready = 0;
    ina228_conversion_ready(&dev, &ready);
    ASSERT_EQ_INT(1, ready, "CNVRF=1 reports conversion ready");
}

static void test_configure_alerts(void)
{
    mock_reset();
    ina228_t dev = make_dev();
    ina228_init(&dev);

    // 48 V over, 30 V under, 50 A overcurrent on a 3 mOhm shunt
    ASSERT_EQ_INT(INA228_OK,
                  ina228_configure_alerts(&dev, 48.0f, 30.0f, 50.0f),
                  "configure ok");

    // BOVL: 48 / 3.125 mV  = 15360 
    ASSERT_EQ_INT(15360, mock_get_reg16(INA228_REG_BOVL), "BOVL value");
    // BUVL: 30 / 3.125 mV  = 9600 
    ASSERT_EQ_INT(9600, mock_get_reg16(INA228_REG_BUVL), "BUVL value");
    // SOVL: (50 A * 0.003 ohm) / 5 uV = 30000
    ASSERT_EQ_INT(30000, mock_get_reg16(INA228_REG_SOVL), "SOVL value");
}

static void test_i2c_failure_propagates(void)
{
    mock_reset();
    ina228_t dev = make_dev();
    ina228_init(&dev);

    float v = -1.0f;
    bus.fail_reads = 1;
    ASSERT_EQ_INT(INA228_ERR_I2C, ina228_read_voltage(&dev, &v),
                  "read failure surfaces as INA228_ERR_I2C");

    bus.fail_writes = 1;
    ASSERT_EQ_INT(INA228_ERR_I2C, ina228_init(&dev),
                  "write failure during init surfaces as INA228_ERR_I2C");

    uint8_t healthy = 0xFF;
    bus.fail_reads = 1;
    ina228_is_healthy(&dev, &healthy);
    ASSERT_EQ_INT(0, healthy, "health defaults to 0 when the bus fails");
}

static void test_null_arguments(void)
{
    mock_reset();
    ina228_t dev = make_dev();
    ina228_init(&dev);

    ASSERT_EQ_INT(INA228_ERR_NULL, ina228_read_voltage(&dev, NULL),
                  "NULL voltage pointer rejected");
    ASSERT_EQ_INT(INA228_ERR_NULL, ina228_read_current(NULL, NULL),
                  "NULL device rejected");
}

// ======================================================================
// Test Executer
// ======================================================================

int main(void)
{
    printf("INA228 driver unit tests (mock I2C)\n");
    printf("-----------------------------------\n");

    test_init_writes_correct_calibration();
    test_init_rejects_bad_config();
    test_verify_id();
    test_read_voltage_positive();
    test_read_current_negative_sign_extension();
    test_read_current_max_negative();
    test_read_power();
    test_health_memstat();
    test_configure_alerts();
    test_i2c_failure_propagates();
    test_null_arguments();

    printf("-----------------------------------\n");
    printf("%d assertions, %d failed\n", tests_run, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
