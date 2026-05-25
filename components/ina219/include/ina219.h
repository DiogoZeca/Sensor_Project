#pragma once

#include "esp_err.h"

/**
 * @brief Initialize the INA219 sensor.
 *
 * Configures I2C bus on GP6/GP7, writes the configuration register
 * (0x019F: 16V range, gain x1, 12-bit ADC, continuous mode) and the
 * calibration register (4096 → 1µA current LSB, 20µW power LSB).
 */
esp_err_t ina219_init(void);

/**
 * @brief Read voltage, current and power from the INA219.
 *
 * @param voltage_v   Bus voltage in Volts
 * @param current_ma  Current through shunt in milliAmps
 * @param power_mw    Power in milliWatts
 */
esp_err_t ina219_read(float *voltage_v, float *current_ma, float *power_mw);
