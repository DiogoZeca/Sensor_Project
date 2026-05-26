# Sensor Project — Power Outage Detector for Electrical Posts

## Project Goal

Simulate a smart grid fault detection sensor to be deployed at electrical distribution posts.
Each ESP32-C6 unit represents one post. The system detects power loss and voltage anomalies,
displays real-time V/I/P readings on a TFT, reports metrics to a local Grafana dashboard via
MQTT, and triggers local alerts (LED + buzzer) based on power state.

---

## Hardware

### Power Supply

| Parameter | Value |
|---|---|
| Supply type | Single-rail bench supply |
| Output | 0–5V between + and − terminals |
| GND terminal | Earth/chassis only — NOT used as circuit reference |
| System GND | Bench supply **−** terminal → ESP32 GND |

### Load Chain (LOAD section)

| Component | Value | Role |
|---|---|---|
| R1 | 10Ω | Protective series resistor before Vin+ |
| R2 | 1KΩ | Bottom resistor of voltage divider (Vin- → GND) |
| R3 | 10KΩ potentiometer | Top of voltage divider (CH+ → Vin+) — controls bus voltage |

**Circuit path:**
```
CH+(+5V) → R1(10Ω) → Vin+(INA219) → [R100 shunt] → Vin-(INA219) → R2(1KΩ) → CH−(GND)
                                                              ↑
                                              R3 (pot) is between CH+ and Vin+
```

**Nominal operating point (pot at mid ~5KΩ):**
- Current ≈ 5V / 6,010Ω ≈ **0.83 mA**
- Bus voltage at Vin- ≈ **5 × 1KΩ / (5KΩ + 1KΩ) ≈ 0.83V** (NORMAL range)

### INA219 — GY-INA219Z (SENSING section)

| Parameter | Value |
|---|---|
| I2C Address | 0x40 (A0, A1 grounded) |
| SDA | GP6 |
| SCL | GP7 |
| I2C Pull-ups | R4 = R5 = 2.2KΩ (on module) |
| IN+ | V_IN (top of R1, bottom of bench +5V) |
| IN- | V_LOAD (bottom of R1, top of R2) |
| Powered from | +3V3 |

**INA219 Register Configuration:**

| Register | Address | Value | Notes |
|---|---|---|---|
| Configuration | 0x00 | 0x019F | See breakdown below |
| Calibration | 0x05 | 40960 | 10µA current LSB |
| Shunt Voltage | 0x01 | read-only | LSB = 10µV |
| Bus Voltage | 0x02 | read-only | LSB = 4mV |
| Current | 0x04 | read-only | LSB = 10µA |
| Power | 0x03 | read-only | LSB = 200µW |

**Configuration register 0x019F breakdown:**

| Field | Bits | Value | Meaning |
|---|---|---|---|
| RST | 15 | 0 | No reset |
| BRNG | 13 | 0 | 16V bus voltage range |
| PGA | 12-11 | 00 | Gain ×1, ±40mV shunt range |
| BADC | 10-7 | 0011 | 12-bit, single sample (532µs) |
| SADC | 6-3 | 0011 | 12-bit, single sample (532µs) |
| MODE | 2-0 | 111 | Shunt + bus, continuous |

**Calibration formula:**
```
Cal = trunc(0.04096 / (Current_LSB × R_shunt))
```

> **Hardware note:** GY-INA219Z has an onboard 0.1Ω shunt (marked "R100" on PCB).
> R1 (10Ω) stays in circuit as a series resistor before Vin+ (protection), not as the sense shunt.

```
R_shunt = 0.1Ω (module onboard)
Current_LSB = 10µA  (1µA needs Cal=409600 → overflows 16-bit register)
Cal = trunc(0.04096 / (0.000010 × 0.1)) = 40960
Power_LSB = 20 × 10µA = 200µW
```

### ESP32-C6 DevKitC-1 (ESP-32 section)

| Function | GPIO |
|---|---|
| I2C SDA (INA219) | GP6 |
| I2C SCL (INA219) | GP7 |
| SPI SCK (TFT) | GP2 |
| SPI MISO (TFT) | GP3 |
| SPI MOSI (TFT) | GP15 |
| TFT Chip Select | GP22 |
| TFT Reset | GP21 |
| TFT Data/Command | GP20 |
| TFT Backlight | GP18 |
| SD Card CS | GP19 (reserved, not used) |
| Status LED | GP4 (with R6 = 330Ω to GND) |
| Buzzer | GP5 |
| Supply | 3V3 onboard regulator |

### Adafruit ST7735R TFT (TFT section)

| Parameter | Value |
|---|---|
| Model | Adafruit 0.96" TFT |
| Controller | ST7735R |
| Resolution | 160×80 pixels (landscape) |
| Interface | SPI |
| Pull-ups on bus | R7, R8, R9 = 10KΩ |
| Backlight | Controlled via GP18 |
| SD slot | Present, CS on GP19 (not used in this project) |

### Alert Components

| Component | GPIO | Series resistor | Behavior |
|---|---|---|---|
| Status LED (D1) | GP4 | R6 = 330Ω | See alert states below |
| Buzzer (BZ) | GP5 | — | See alert states below |

---

## Alert States

| State | Condition | LED | Buzzer |
|---|---|---|---|
| NORMAL | 1.0V ≤ bus voltage ≤ 3.9V | Steady ON | Silent |
| BROWNOUT | Bus voltage > 3.9V (overload) | Slow blink (500ms period) | Intermittent tone ~1kHz |
| POWER LOSS | Bus voltage < 1.0V | Fast blink (100ms period) | Intermittent tone ~3kHz |

---

## Display Layout (160×80 landscape)

```
+--[ POST: AA:BB:CC ]--+
| V:  4.08 V           |
| I:  0.91 mA          |
| P:  3.71 mW          |
| STATUS: [ NORMAL ]   |
+----------------------+
```

- Post ID derived from last 3 bytes of WiFi MAC address
- Status row color: GREEN (NORMAL) / YELLOW (BROWNOUT) / RED (POWER LOSS)
- Refresh rate: 2Hz

---

## Software Architecture

### ESP-IDF Project Structure

```
Sensor_Project/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   └── main.c              ← app logic, task definitions, WiFi, MQTT
├── components/
│   ├── st7735_driver/      ← ported from aula10, pins reconfigured
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── st7735.h
│   │   │   ├── graphics.h
│   │   │   └── st7735_commands.h
│   │   └── src/
│   │       ├── st7735.c
│   │       └── graphics.c
│   └── ina219/             ← built from scratch
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── ina219.h
│       └── src/
│           └── ina219.c
└── RESUME.md
```

### FreeRTOS Task Architecture

| Task | Rate | Responsibility |
|---|---|---|
| `task_sensor` | 1Hz | Read INA219, evaluate thresholds, update shared struct (mutex), drive LED/buzzer |
| `task_display` | 2Hz | Read shared struct, refresh TFT |
| `task_mqtt` | Event-driven | Wake on FreeRTOS notify from task_sensor, publish JSON over MQTT |

Shared data protected by a **FreeRTOS mutex** (`SemaphoreHandle_t`).

### Shared Data Struct

```c
typedef struct {
    float    voltage_v;
    float    current_ma;
    float    power_mw;
    uint8_t  status;       // 0=NORMAL, 1=BROWNOUT, 2=POWER_LOSS
} sensor_data_t;
```

### WiFi

- Mode: **Station (STA)** — connects to local LAN
- Pattern: aligned with aula08 STA_mode example
- Credentials: configured via Kconfig (menuconfig) — `CONFIG_WIFI_SSID` / `CONFIG_WIFI_PASSWORD`
- WiFi init blocks until IP obtained before starting MQTT task

### MQTT

- Protocol: **MQTT over TCP** (no TLS — local network only)
- Pattern: aligned with aula09 MQTT_TCP example
- Broker: local Mosquitto instance (IP configured via Kconfig — `CONFIG_MQTT_BROKER_URI`)
- Client ID: derived from MAC address (e.g., `esp32-post-AABBCC`)
- Topic: `sensors/post/<post_id>/metrics`
- Publish rate: 1Hz (triggered by task_sensor notify)
- QoS: 1

### MQTT Payload (JSON)

```json
{
  "post_id": "AA:BB:CC",
  "voltage": 4.08,
  "current_ma": 0.91,
  "power_mw": 3.71,
  "status": "NORMAL",
  "status_code": 0
}
```

`status_code` (0/1/2) is included alongside the string to make Grafana threshold alerting trivial.

---

## Local Server Stack (Grafana)

Runs on developer machine via Docker Compose:

```
ESP32-C6
  └─► Mosquitto (MQTT broker, port 1883)
        └─► Telegraf (MQTT consumer input → InfluxDB line protocol)
              └─► InfluxDB
                    └─► Grafana
```

Telegraf uses the `inputs.mqtt_consumer` plugin with JSON parser pointed at topic `sensors/post/+/metrics`.
Grafana dashboards can filter by `post_id` tag to show per-post state.

---

## Lab Guide References

| Feature | Reference |
|---|---|
| FreeRTOS tasks, mutex, notifications | aula02 |
| GPIO (LED, buzzer) | aula03 |
| I2C master driver (INA219 pattern) | aula04 |
| SPI / TFT driver | aula10 |
| WiFi STA mode | aula08 |
| MQTT TCP | aula09 |

---

## INA219 Driver Design (from scratch)

### Initialization sequence
1. Write config register (0x019F)
2. Write calibration register (40960)
3. Wait one conversion cycle (~2ms)

### Read sequence (per cycle)
1. Read bus voltage register (0x02) → shift right 3 bits, multiply by 4mV LSB
2. Read shunt voltage register (0x01) → multiply by 10µV LSB (signed 16-bit)
3. Read current register (0x04) → multiply by 1µA LSB (signed 16-bit) → convert to mA
4. Read power register (0x03) → multiply by 20µW LSB → convert to mW

### Threshold evaluation
```
if   voltage > 3.9V  → BROWNOUT  (overload)
elif voltage >= 1.0V → NORMAL
else                 → POWER_LOSS
```

---

## Implementation Steps

- [x] **Step 1 — LED + Buzzer** (GPIO + LEDC PWM): verify GP4/GP5 wiring, control LED and buzzer independently
- [x] **Step 2 — TFT Display** (SPI): port st7735_driver from aula10 with correct pin mapping, get something visible on screen
- [x] **Step 3 — INA219** (I2C from scratch): validate sensor responds at 0x40, calibration correct, nominal values (~4.08V, ~0.91mA) match expectations on serial monitor
- [x] **Step 4 — Integration: INA219 + TFT + Alerts**: real V/I/P on display, alert states driving LED and buzzer — fully functional standalone device
- [ ] **Step 5 — WiFi + MQTT**: connect to local broker, publish JSON payload, verify data arrives on laptop
- [ ] **Step 6 — Full FreeRTOS integration**: restructure into 3 tasks (sensor/display/mqtt) with mutex-protected shared struct, final architecture

---

## Power Mode Strategy

### Decision: Always Active (for now)

Deep sleep is **ruled out** for this application — fault detection requires immediate response.
If the post loses power at second 1 of a 30-second sleep cycle, the outage is only detected
29 seconds later, which is unacceptable for infrastructure monitoring.

### Options analyzed

| Mode | Latency | Avg Current | Verdict |
|---|---|---|---|
| Always Active | <1s | ~80–100mA | ✅ Current choice — simple, zero latency, display always live |
| Light Sleep (RTC timer) | ~1s | ~5–20mA | ✅ Compatible — viable future optimization (aula07 has patterns) |
| GPIO wake via INA219 ALERT pin | <1ms | <1mA | ❌ Not wired in our circuit — ALERT pin not connected to any GPIO |
| Deep Sleep | Up to 30s+ | <0.1mA | ❌ Ruled out — unacceptable fault detection latency |

### Key design choice regardless of sleep mode

MQTT publish is **state-change-triggered**, not time-triggered. The ESP32 only publishes
when the status transitions between NORMAL / BROWNOUT / POWER LOSS. This avoids flooding
the broker with redundant "still NORMAL" messages every second and is a real-world pattern
used in production monitoring systems.

### Future: Light Sleep (optional Step 7)

After Step 6 is complete, light sleep can be added cleanly — wrap task delays with
`esp_light_sleep_start()` instead of `vTaskDelay()`. No restructuring needed.
WiFi stays associated via modem sleep during idle. Fault detection latency remains ~1s.

**Reference:** [ESP32-C6 Sleep Modes — ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/system/sleep_modes.html)

---

## Open Decisions / Future Updates

- [x] Buzzer: driven in **passive mode via LEDC PWM**. Chosen over active mode for
      expressiveness — different frequencies per alert state (BROWNOUT ~1kHz, POWER LOSS
      ~3kHz) make alerts distinguishable by ear. On/off beep patterns controlled fully
      in software. GP5 wiring unchanged.
- [ ] SD card offline buffering — decided OUT OF SCOPE for this version
- [ ] OTA update support — not in scope
