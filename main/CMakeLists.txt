idf_component_register(SRCS "main.c" "alert.c" "display.c" "wifi.c" "mqtt_pub.c"
                    INCLUDE_DIRS "."
                    REQUIRES st7735_driver ina219
                    PRIV_REQUIRES esp_wifi nvs_flash mqtt esp_netif esp_event)
