#include "mqtt_pub.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include <stdio.h>

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client = NULL;
static char s_post_id[9];    /* "AA:BB:CC\0" */
static char s_topic[48];     /* "sensors/post/AA:BB:CC/metrics\0" */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected — publishing to %s", s_topic);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from broker");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

void mqtt_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_post_id, sizeof(s_post_id), "%02X:%02X:%02X", mac[3], mac[4], mac[5]);
    snprintf(s_topic,   sizeof(s_topic),   "sensors/post/%s/metrics", s_post_id);

    char client_id[24];
    snprintf(client_id, sizeof(client_id), "esp32-post-%02X%02X%02X", mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri   = CONFIG_MQTT_BROKER_URI,
        .credentials.client_id = client_id,
    };
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
}

static const char *status_str(alert_state_t s)
{
    switch (s) {
        case ALERT_BROWNOUT:   return "BROWNOUT";
        case ALERT_POWER_LOSS: return "POWER_LOSS";
        default:               return "NORMAL";
    }
}

void mqtt_publish(float voltage_v, float current_ma, float power_mw, alert_state_t status, int lp_active)
{
    if (!s_client) return;

    char payload[160];
    snprintf(payload, sizeof(payload),
        "{\"post_id\":\"%s\","
        "\"voltage\":%.3f,"
        "\"current_ma\":%.3f,"
        "\"power_mw\":%.3f,"
        "\"status\":\"%s\","
        "\"status_code\":%d,"
        "\"lp_mode\":%d}",
        s_post_id, voltage_v, current_ma, power_mw,
        status_str(status), (int)status, lp_active);

    int msg_id = esp_mqtt_client_publish(s_client, s_topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish queued/failed (broker reconnecting?)");
    } else {
        ESP_LOGI(TAG, "published msg_id=%d: %s", msg_id, payload);
    }
}
