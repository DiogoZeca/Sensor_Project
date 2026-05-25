#pragma once

#include "alert.h"

void display_init(void);
void display_update(float voltage_v, float current_ma, float power_mw, alert_state_t status);
