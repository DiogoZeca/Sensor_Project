#include "alert.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include <stddef.h>

#define LED_GPIO        GPIO_NUM_4
#define BUZZER_GPIO     GPIO_NUM_5

#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT
#define LEDC_DUTY_50PCT 512

#define TICK_MS         100

typedef struct {
    uint32_t freq_hz;  /* 0 = silence */
    uint8_t  ticks;    /* duration in 100ms units */
} note_t;

/* BROWNOUT — Imperial March opening (G4 G4 G4 Eb4 Bb4 G4 Eb4 Bb4 G4) */
static const note_t s_brownout[] = {
    {392, 2}, {0, 1},
    {392, 2}, {0, 1},
    {392, 2}, {0, 1},
    {311, 2}, {466, 1},
    {392, 2}, {0, 1},
    {311, 2}, {466, 1},
    {392, 4}, {0, 4},
};
#define BROWNOUT_LEN (sizeof(s_brownout) / sizeof(s_brownout[0]))

/* POWER_LOSS — triple-beep descending emergency alarm (C6 C6 C6 / G5 G5 G5 / C5) */
static const note_t s_power_loss[] = {
    {1047, 1}, {0, 1}, {1047, 1}, {0, 1}, {1047, 1}, {0, 2},
    {784,  1}, {0, 1}, {784,  1}, {0, 1}, {784,  1}, {0, 2},
    {523,  3}, {0, 4},
};
#define POWER_LOSS_LEN (sizeof(s_power_loss) / sizeof(s_power_loss[0]))

static volatile alert_state_t s_state     = ALERT_NORMAL;
static volatile uint32_t      s_tick      = 0;
static          uint8_t       s_note_idx  = 0;
static          uint8_t       s_note_tick = 0;

static void buzzer_note(uint32_t freq_hz)
{
    if (freq_hz == 0) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    } else {
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_50PCT);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
}

static void play_melody(const note_t *melody, size_t len)
{
    if (s_note_tick == 0) {
        buzzer_note(melody[s_note_idx].freq_hz);
    }
    if (++s_note_tick >= melody[s_note_idx].ticks) {
        s_note_tick = 0;
        s_note_idx  = (s_note_idx + 1) % len;
    }
}

static void alert_timer_cb(void *arg)
{
    s_tick++;

    switch (s_state) {
        case ALERT_NORMAL:
            gpio_set_level(LED_GPIO, 1);
            buzzer_note(0);
            break;

        case ALERT_BROWNOUT:
            gpio_set_level(LED_GPIO, (s_tick / 5) % 2);
            play_melody(s_brownout, BROWNOUT_LEN);
            break;

        case ALERT_POWER_LOSS:
            gpio_set_level(LED_GPIO, s_tick % 2);
            play_melody(s_power_loss, POWER_LOSS_LEN);
            break;
    }
}

void alert_init(void)
{
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(LED_GPIO, 0);

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

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

    /* reset melody position on state change */
    s_note_idx  = 0;
    s_note_tick = 0;

    s_state = state;
}
