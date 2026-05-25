#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ina219.h"

static const char *TAG = "STEP3";

void app_main(void)
{
    ESP_ERROR_CHECK(ina219_init());
    ESP_LOGI(TAG, "Reading INA219 every 1s — bench supply ON to see nominal values");

    while (1) {
        float v, i, p;
        if (ina219_read(&v, &i, &p) == ESP_OK) {
            ESP_LOGI(TAG, "V=%.3f V   I=%.3f mA   P=%.3f mW", v, i, p);
        } else {
            ESP_LOGW(TAG, "Read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
