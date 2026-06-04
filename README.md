# Smart Power Fault Detection System

ESP32-C6 sensor node for real-time detection of power outages and voltage anomalies at electrical distribution posts. Detects three scenarios — **NORMAL**, **BROWNOUT**, and **POWER LOSS** — with local alerts and remote dashboard reporting.

## Features

- Sub-second fault detection via continuous INA219 voltage/current/power monitoring
- Local alerts: LED (blink pattern) + passive buzzer (melody per scenario)
- Local display: real-time V/I/P readings + scenario status on ST7735R TFT
- Remote monitoring: MQTT → Telegraf → InfluxDB → Grafana dashboard
- Light sleep: 50%+ CPU sleep time with zero impact on detection latency
- Multi-post ready: each node identified by MAC address, Grafana filters by post

## Hardware

| Component | Role |
|---|---|
| ESP32-C6 DevKitC-1 | Main MCU — processing, WiFi, alerts |
| GY-INA219Z | Voltage + current + power sensor (I²C, onboard 0.1Ω shunt) |
| Adafruit ST7735R 0.96" TFT | Local display (SPI, 160×80) |
| Status LED (GP4) + R6 330Ω | Visual alert |
| Passive buzzer (GP5) | Audio alert via LEDC PWM |
| R1 10Ω, R2 1KΩ, R3 10KΩ pot | Load simulation / voltage divider |

See [`Docs/FinalDiagram.pdf`](Docs/FinalDiagram.pdf) for the full circuit diagram.

## Alert Scenarios

| Scenario | Condition | LED | Buzzer |
|---|---|---|---|
| NORMAL | 1.0V ≤ V ≤ 3.9V | Steady ON | Silent |
| BROWNOUT | V > 3.9V | Slow blink 500ms | Imperial March |
| POWER LOSS | V < 1.0V | Fast blink 100ms | Emergency alarm |

## Project Structure

```
Sensor_Project/
├── main/
│   ├── main.c          — FreeRTOS tasks, PM config, app entry
│   ├── alert.c/h       — LED + buzzer state machine, melodies
│   ├── display.c/h     — TFT rendering
│   ├── mqtt_pub.c/h    — MQTT client + JSON payload
│   ├── wifi.c/h        — WiFi STA init
│   └── Kconfig.projbuild — WiFi/MQTT credentials via menuconfig
├── components/
│   ├── ina219/         — INA219 I²C driver (built from scratch)
│   └── st7735_driver/  — ST7735R SPI driver
├── server/
│   ├── docker-compose.yml  — Mosquitto + Telegraf + InfluxDB + Grafana
│   ├── telegraf.conf        — MQTT consumer → InfluxDB pipeline
│   └── mosquitto.conf       — Broker config
├── Docs/
│   └── FinalDiagram.pdf    — Full circuit schematic
├── partitions.csv           — Custom partition table
└── sdkconfig.defaults       — Hardware + PM overrides (no credentials)
```

## Setup

### 1. Configure credentials

```bash
idf.py menuconfig
```

Navigate to **Sensor Project Configuration** and set:
- WiFi SSID
- WiFi Password
- MQTT Broker URI (e.g. `mqtt://192.168.1.100:1884`)

### 2. Start the server stack

```bash
cd server
docker compose up -d
```

Wait ~10s for InfluxDB to initialise. Verify the broker is receiving:

```bash
mosquitto_sub -h localhost -p 1884 -t "sensors/post/+/metrics" -v
```

### 3. Flash the ESP32

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Expected output:
```
I (wifi): IP: 192.168.x.x
I (mqtt): connected — publishing to sensors/post/XX:XX:XX/metrics
I (STEP6): V=X.XXX  I=X.XXX  P=X.XXX  state=X
I (mqtt): published msg_id=...: {...}
```

### 4. Open Grafana

Navigate to `http://localhost:3000` (admin / admin).

Configure the InfluxDB data source:
- Query language: **Flux**
- URL: `http://influxdb:8086`
- Org: `ase` | Bucket: `sensors` | Token: `my-super-secret-token`

## Architecture

```
[Power Supply] → [INA219] → [ESP32-C6] → WiFi → [Mosquitto]
                                    ↓                     ↓
                              [TFT + LED              [Telegraf]
                               + Buzzer]                  ↓
                                                     [InfluxDB]
                                                          ↓
                                                      [Grafana]
```

The edge node (ESP32) operates fully standalone — local alerts fire independently of network availability. The server stack is local-only (Docker Compose), with no cloud dependency.

### FreeRTOS Tasks

| Task | Rate | Responsibility |
|---|---|---|
| `task_sensor` | 1 Hz | Read INA219, evaluate state, drive alerts, notify MQTT task |
| `task_display` | 2 Hz | Read shared struct, refresh TFT |
| `task_mqtt` | Event-driven | Wake on state change or 10s heartbeat, publish JSON |

### Power Management

Light sleep via FreeRTOS tickless idle — the CPU sleeps automatically whenever all tasks are blocked. Measured result: **~50% sleep time**, zero missed fault events.

Deep sleep is explicitly ruled out: a fault at second 1 of a 30s sleep cycle would go undetected for 29 seconds — unacceptable for infrastructure monitoring.

## Technical Reference

See [`RESUME.md`](RESUME.md) for detailed hardware specs, INA219 calibration, register map, GPIO pinout, and Grafana query reference.
