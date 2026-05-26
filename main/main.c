#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ina219.h"
#include "alert.h"
#include "display.h"
#include "wifi.h"
#include "mqtt_pub.h"

static const char *TAG = "STEP5";

#define THRESH_POWER_LOSS  1.0f   /* V < 1.0V  → POWER LOSS          */
#define THRESH_BROWNOUT    3.9f   /* V > 3.9V  → BROWNOUT (overload)  */

static alert_state_t evaluate_state(float voltage_v)
{
    if (voltage_v < THRESH_POWER_LOSS) return ALERT_POWER_LOSS;
    if (voltage_v > THRESH_BROWNOUT)  return ALERT_BROWNOUT;
    return ALERT_NORMAL;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    alert_init();
    display_init();
    ESP_ERROR_CHECK(ina219_init());

    wifi_init_sta();   /* blocks until IP obtained */
    mqtt_init();       /* connects to broker async */

    ESP_LOGI(TAG, "All systems ready — starting sensor loop");

    while (1) {
        float v = 0.0f, i = 0.0f, p = 0.0f;
        alert_state_t state = ALERT_POWER_LOSS;

        if (ina219_read(&v, &i, &p) == ESP_OK) {
            state = evaluate_state(v);
            ESP_LOGI(TAG, "V=%.3fV  I=%.3fmA  P=%.3fmW  state=%d", v, i, p, (int)state);
        } else {
            ESP_LOGW(TAG, "INA219 read failed — assuming POWER LOSS");
        }

        alert_set_state(state);
        display_update(v, i, p, state);
        mqtt_publish(v, i, p, state);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
