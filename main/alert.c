#include "alert.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

#define LED_GPIO         GPIO_NUM_4
#define BUZZER_GPIO      GPIO_NUM_5

#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define LEDC_DUTY_50PCT  512   /* 50% of 2^10 = 1023 */

#define TICK_MS          100   /* esp_timer fires every 100ms */

static volatile alert_state_t s_state = ALERT_NORMAL;
static volatile uint32_t      s_tick  = 0;

static void buzzer_set(bool on)
{
    uint32_t duty = on ? LEDC_DUTY_50PCT : 0;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void alert_timer_cb(void *arg)
{
    s_tick++;

    switch (s_state) {
        case ALERT_NORMAL:
            gpio_set_level(LED_GPIO, 1);
            buzzer_set(false);
            break;

        case ALERT_BROWNOUT:
            /* LED: toggle every 5 ticks = 500ms period */
            gpio_set_level(LED_GPIO, (s_tick / 5) % 2);
            /* Buzzer: 200ms ON / 800ms OFF (cycle = 10 ticks) at 1kHz */
            buzzer_set((s_tick % 10) < 2);
            break;

        case ALERT_POWER_LOSS:
            /* LED: toggle every tick = 100ms period */
            gpio_set_level(LED_GPIO, s_tick % 2);
            /* Buzzer: 100ms ON / 100ms OFF at 3kHz */
            buzzer_set(s_tick % 2 == 0);
            break;
    }
}

void alert_init(void)
{
    /* LED GPIO */
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(LED_GPIO, 0);

    /* LEDC timer for buzzer */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* LEDC channel for buzzer, starts silent */
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    /* Periodic timer: fires every TICK_MS milliseconds */
    esp_timer_handle_t timer;
    const esp_timer_create_args_t timer_args = {
        .callback = alert_timer_cb,
        .name     = "alert_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, TICK_MS * 1000));
}

void alert_set_state(alert_state_t state)
{
    if (state == s_state) return;

    /* Update buzzer frequency before the state changes so the
       first ON tick already uses the correct frequency */
    if (state == ALERT_BROWNOUT) {
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, 1000);
    } else if (state == ALERT_POWER_LOSS) {
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, 3000);
    }

    s_state = state;
}
