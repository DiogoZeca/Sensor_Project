#pragma once
#include "alert.h"

/* Initialise the MQTT client and connect to the broker. */
void mqtt_init(void);

/* Publish a JSON metrics payload to sensors/post/<post_id>/metrics, QoS 1. */
/* lp_active: 1 = heartbeat (idle/sleeping), 0 = state change (active/reacting) */
void mqtt_publish(float voltage_v, float current_ma, float power_mw, alert_state_t status, int lp_active);
