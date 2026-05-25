#pragma once

typedef enum {
    ALERT_NORMAL     = 0,
    ALERT_BROWNOUT   = 1,
    ALERT_POWER_LOSS = 2
} alert_state_t;

void alert_init(void);
void alert_set_state(alert_state_t state);
