#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_pm.h"
#include "ina219.h"
#include "alert.h"
#include "display.h"
#include "wifi.h"
#include "mqtt_pub.h"

static const char *TAG = "STEP6";

/* ── Thresholds ───────────────────────────────────────────────────── */
#define THRESH_POWER_LOSS  1.0f
#define THRESH_BROWNOUT    3.9f

/* ── Shared data ──────────────────────────────────────────────────── */
typedef struct {
    float         voltage_v;
    float         current_ma;
    float         power_mw;
    alert_state_t status;
} sensor_data_t;

static sensor_data_t    g_data = {0};
static SemaphoreHandle_t g_mutex;
static TaskHandle_t      g_mqtt_task_handle;

/* ── Helpers ──────────────────────────────────────────────────────── */
static alert_state_t evaluate_state(float v)
{
    if (v < THRESH_POWER_LOSS) return ALERT_POWER_LOSS;
    if (v > THRESH_BROWNOUT)   return ALERT_BROWNOUT;
    return ALERT_NORMAL;
}

/* ── task_sensor — 1 Hz ───────────────────────────────────────────── */
static void task_sensor(void *arg)
{
    alert_state_t prev_state = ALERT_NORMAL;

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

        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_data.voltage_v  = v;
            g_data.current_ma = i;
            g_data.power_mw   = p;
            g_data.status     = state;
            xSemaphoreGive(g_mutex);
        }

        /* notify MQTT task only when state changes */
        if (state != prev_state) {
            prev_state = state;
            xTaskNotifyGive(g_mqtt_task_handle);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ── task_display — 2 Hz ─────────────────────────────────────────── */
static void task_display(void *arg)
{
    float v = 0.0f, i = 0.0f, p = 0.0f;
    alert_state_t state = ALERT_POWER_LOSS;

    while (1) {
        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            v     = g_data.voltage_v;
            i     = g_data.current_ma;
            p     = g_data.power_mw;
            state = g_data.status;
            xSemaphoreGive(g_mutex);
        }

        display_update(v, i, p, state);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ── task_mqtt — event-driven (state change only) ─────────────────── */
static void task_mqtt(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        float v = 0.0f, i = 0.0f, p = 0.0f;
        alert_state_t state = ALERT_POWER_LOSS;

        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            v     = g_data.voltage_v;
            i     = g_data.current_ma;
            p     = g_data.power_mw;
            state = g_data.status;
            xSemaphoreGive(g_mutex);
        }

        mqtt_publish(v, i, p, state);
    }
}

/* ── app_main ─────────────────────────────────────────────────────── */
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

    wifi_init_sta();
    mqtt_init();

    /* WiFi modem sleep — CPU sleeps between beacon intervals */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    /* Automatic light sleep via tickless idle — CPU sleeps whenever
       all tasks are blocked in vTaskDelay(), no task code changes needed */
    esp_pm_config_t pm_config = {
        .max_freq_mhz       = 160,
        .min_freq_mhz       = 40,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    g_mutex = xSemaphoreCreateMutex();
    configASSERT(g_mutex);

    /* task_mqtt created first so its handle is valid before task_sensor starts */
    xTaskCreate(task_mqtt,   "task_mqtt",   4096, NULL, 5, &g_mqtt_task_handle);
    xTaskCreate(task_display,"task_display",4096, NULL, 4, NULL);
    xTaskCreate(task_sensor, "task_sensor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "All tasks started");
}
