#include "ina219.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "INA219";

/* ── Hardware ────────────────────────────────────────────────────── */
#define INA219_SDA_PIN       6      /* GP6  */
#define INA219_SCL_PIN       7      /* GP7  */
#define INA219_ADDR          0x40   /* A0, A1 grounded */
#define INA219_FREQ_HZ       100000
#define INA219_TIMEOUT_MS    1000

/* ── Register addresses ──────────────────────────────────────────── */
#define REG_CONFIG   0x00
#define REG_SHUNT    0x01
#define REG_BUS      0x02
#define REG_POWER    0x03
#define REG_CURRENT  0x04
#define REG_CALIB    0x05

/* ── Register values (see RESUME.md for derivation) ─────────────── */
/* 0x019F: BRNG=0 (16V), PGA=00 (x1 ±40mV), BADC=0011 (12-bit),
           SADC=0011 (12-bit), MODE=111 (continuous shunt+bus)      */
#define CONFIG_VAL   0x019F

/* GY-INA219Z module has onboard 0.1Ω shunt (marked R100 on PCB).
   Cal = trunc(0.04096 / (10e-6 × 0.1)) = 40960 → Current LSB = 10µA */
#define CALIB_VAL    40960

/* ── LSB scaling ─────────────────────────────────────────────────── */
#define BUS_VOLTAGE_LSB_V   0.004f    /* 4 mV per LSB                */
#define CURRENT_LSB_MA      0.010f    /* 10 µA = 0.010 mA per LSB    */
#define POWER_LSB_MW        0.200f    /* 200 µW = 0.200 mW per LSB   */

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

/* ── Internal helpers ────────────────────────────────────────────── */

static esp_err_t write_reg(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    return i2c_master_transmit(s_dev, buf, sizeof(buf),
                               INA219_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t read_reg(uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, buf, 2,
                                                INA219_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret == ESP_OK) {
        *out = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return ret;
}

/* ── Public API ──────────────────────────────────────────────────── */

esp_err_t ina219_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port             = I2C_NUM_0,
        .sda_io_num           = INA219_SDA_PIN,
        .scl_io_num           = INA219_SCL_PIN,
        .clk_source           = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt    = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus),
                        TAG, "I2C bus init failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = INA219_ADDR,
        .scl_speed_hz    = INA219_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev),
                        TAG, "I2C device add failed");

    /* Verify the sensor is reachable before writing registers */
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_bus, INA219_ADDR, INA219_TIMEOUT_MS),
                        TAG, "INA219 not found at 0x%02X", INA219_ADDR);

    ESP_RETURN_ON_ERROR(write_reg(REG_CONFIG, CONFIG_VAL),
                        TAG, "Config register write failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_CALIB, CALIB_VAL),
                        TAG, "Calibration register write failed");

    /* Wait for the first conversion to complete (~1.1ms for 12-bit) */
    vTaskDelay(pdMS_TO_TICKS(2));

    ESP_LOGI(TAG, "INA219 ready at 0x%02X (config=0x%04X cal=%d, shunt=0.1Ω)",
             INA219_ADDR, CONFIG_VAL, CALIB_VAL);
    return ESP_OK;
}

esp_err_t ina219_read(float *voltage_v, float *current_ma, float *power_mw)
{
    uint16_t raw;

    /* Bus voltage: bits [15:3] are the 12-bit ADC value, shift right 3 */
    ESP_RETURN_ON_ERROR(read_reg(REG_BUS, &raw),
                        TAG, "Bus voltage read failed");
    *voltage_v = (float)(raw >> 3) * BUS_VOLTAGE_LSB_V;

    /* Current: signed 16-bit, LSB = 1µA → convert to mA */
    ESP_RETURN_ON_ERROR(read_reg(REG_CURRENT, &raw),
                        TAG, "Current read failed");
    *current_ma = (float)(int16_t)raw * CURRENT_LSB_MA;

    /* Power: unsigned 16-bit, LSB = 20µW → convert to mW */
    ESP_RETURN_ON_ERROR(read_reg(REG_POWER, &raw),
                        TAG, "Power read failed");
    *power_mw = (float)raw * POWER_LSB_MW;

    return ESP_OK;
}
