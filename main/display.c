#include "display.h"
#include "st7735.h"
#include "graphics.h"
#include "esp_mac.h"
#include <stdio.h>
#include <string.h>

/* ── Pin mapping (from FinalDiagram.pdf) ─────────────────────────── */
#define PIN_MOSI   15   /* GP15 */
#define PIN_SCLK    2   /* GP2  */
#define PIN_CS     22   /* GP22 TFTCS */
#define PIN_DC     20   /* GP20 DC    */
#define PIN_RST    21   /* GP21 RST   */
#define PIN_BL     18   /* GP18 LIT   */

/* ── Colours ─────────────────────────────────────────────────────── */
#define BG_COLOR    ST7735_RGB565(20, 20, 30)
#define SEP_COLOR   ST7735_RGB565(60, 60, 80)
#define TITLE_COLOR ST7735_WHITE
#define LABEL_COLOR ST7735_CYAN
#define VALUE_COLOR ST7735_YELLOW

/* ── Layout (pixels, landscape 160×80) ───────────────────────────── */
/*
 * y= 1  "POST: AA:BB:CC"   size=1
 * y= 9  separator line
 * y=13  "V:"  value         size=2  (14px tall)
 * y=31  "I:"  value         size=2
 * y=49  "P:"  value         size=2
 * y=67  separator line
 * y=70  "STATUS: NORMAL"    size=1
 */
#define X_LABEL    2
#define X_VALUE   26   /* 2 chars × 12px/char + 2px gap */
#define Y_TITLE    1
#define Y_SEP1     9
#define Y_ROW_V   13
#define Y_ROW_I   31
#define Y_ROW_P   49
#define Y_SEP2    67
#define Y_STATUS  70

static char s_post_id[12];  /* "AA:BB:CC\0" */

static uint16_t status_color(alert_state_t s)
{
    switch (s) {
        case ALERT_BROWNOUT:   return ST7735_YELLOW;
        case ALERT_POWER_LOSS: return ST7735_RED;
        default:               return ST7735_GREEN;
    }
}

static const char *status_str(alert_state_t s)
{
    switch (s) {
        case ALERT_BROWNOUT:   return "BROWNOUT";
        case ALERT_POWER_LOSS: return "POWER LOSS";
        default:               return "NORMAL";
    }
}

void display_init(void)
{
    st7735_config_t cfg = {
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK,
        .cs_io_num   = PIN_CS,
        .dc_io_num   = PIN_DC,
        .rst_io_num  = PIN_RST,
        .bl_io_num   = PIN_BL,
        .host_id     = SPI2_HOST,
    };
    ESP_ERROR_CHECK(st7735_init(&cfg));
    st7735_set_rotation(1);  /* landscape 160×80 */

    /* Derive post ID from last 3 bytes of WiFi MAC */
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(s_post_id, sizeof(s_post_id), "%02X:%02X:%02X",
                 mac[3], mac[4], mac[5]);
    } else {
        strncpy(s_post_id, "??:??:??", sizeof(s_post_id));
    }
}

void display_update(float voltage_v, float current_ma, float power_mw, alert_state_t status)
{
    char buf[24];

    st7735_fill_screen(BG_COLOR);

    /* Title */
    snprintf(buf, sizeof(buf), "POST: %s", s_post_id);
    draw_string(X_LABEL, Y_TITLE, buf, TITLE_COLOR, BG_COLOR, 1);
    draw_hline(0, Y_SEP1, 160, SEP_COLOR);

    /* Voltage */
    draw_string(X_LABEL, Y_ROW_V, "V:", LABEL_COLOR, BG_COLOR, 2);
    snprintf(buf, sizeof(buf), "%.2f V", voltage_v);
    draw_string(X_VALUE, Y_ROW_V, buf, VALUE_COLOR, BG_COLOR, 2);

    /* Current */
    draw_string(X_LABEL, Y_ROW_I, "I:", LABEL_COLOR, BG_COLOR, 2);
    snprintf(buf, sizeof(buf), "%.2f mA", current_ma);
    draw_string(X_VALUE, Y_ROW_I, buf, VALUE_COLOR, BG_COLOR, 2);

    /* Power */
    draw_string(X_LABEL, Y_ROW_P, "P:", LABEL_COLOR, BG_COLOR, 2);
    snprintf(buf, sizeof(buf), "%.2f mW", power_mw);
    draw_string(X_VALUE, Y_ROW_P, buf, VALUE_COLOR, BG_COLOR, 2);

    /* Status */
    draw_hline(0, Y_SEP2, 160, SEP_COLOR);
    draw_string(X_LABEL, Y_STATUS, "STATUS:", TITLE_COLOR, BG_COLOR, 1);
    draw_string(50, Y_STATUS, status_str(status), status_color(status), BG_COLOR, 1);
}
