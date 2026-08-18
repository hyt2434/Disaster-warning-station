# Hợp đồng MQTT topics và payload

> **Project:** Disaster Warning System  
> **Firmware:** ESP32-S3 Main + XIAO ESP32-C3 F7  
> **Contract version:** v1.0  
> **Root topic:** `disaster/`

This document defines the MQTT communication contract between:

- **ESP32-S3 Main**
- **XIAO ESP32-C3 F7**
- **Mosquitto Broker**
- **Backend**
- **Website / Dashboard**
- **Notification Service**

The purpose of this contract is to ensure that the firmware, backend, and frontend use the **same topics, field names, payload formats, and status values**, instead of each component defining its own conventions.

---

# 1. Overall MQTT Architecture

```text
ESP32-S3 Main ───────┐
                     │ publish
XIAO ESP32-C3 F7 ────┤
                     ▼
                 Mosquitto
                     │
                     ▼
                  Backend
                ┌────┼─────┐
                │    │     │
                ▼    ▼     ▼
              Web  Storage Notification
```

Control direction:

```text
Website
   │
   ▼
Backend
   │ publish command
   ▼
Mosquitto
   │
   ▼
ESP32-S3 Main
```

---

# 2. Standard MQTT Topic Tree

```text
disaster/
│
├── main/
│   ├── telemetry
│   ├── status
│   ├── alert
│   ├── state/
│   │   └── buzzer
│   └── command/
│       └── buzzer
│
└── f7/
    ├── telemetry
    ├── status
    └── alert
```

Complete topic list:

```text
disaster/main/telemetry
disaster/main/status
disaster/main/alert
disaster/main/state/buzzer
disaster/main/command/buzzer

disaster/f7/telemetry
disaster/f7/status
disaster/f7/alert
```

> **Implementation note:** `disaster/main/alert` is part of the standard MQTT design, but the current Main firmware does not publish this topic yet. The schema below is the proposed contract for the next firmware update. The other topics already match the current firmware.

---

# 3. Topic Summary

| Topic | Publisher | Subscriber | Payload | Retain | Status |
|---|---|---|---|---:|---|
| `disaster/main/telemetry` | Main | Backend | JSON | No | Implemented |
| `disaster/main/status` | Main / Broker LWT | Backend | Text | Yes | Implemented |
| `disaster/main/alert` | Main | Backend | JSON | No | **TODO firmware** |
| `disaster/main/state/buzzer` | Main | Backend | JSON | Yes | Implemented |
| `disaster/main/command/buzzer` | Backend | Main | Text | No | Implemented |
| `disaster/f7/telemetry` | F7 | Backend | JSON | No | Implemented |
| `disaster/f7/status` | F7 / Broker LWT | Backend | Text | Yes | Implemented |
| `disaster/f7/alert` | F7 | Backend | JSON | No | Implemented |

---

# 4. General JSON Conventions

## 4.1 `deviceId`

ESP32-S3 Main:

```json
"deviceId": "main-01"
```

XIAO ESP32-C3:

```json
"deviceId": "f7-01"
```

The backend should not identify a device only from the MQTT topic. The `deviceId` should also be stored with the message.

---

## 4.2 `timestamp`

Use UTC ISO-8601 format:

```json
"timestamp": "2026-08-15T00:30:15Z"
```

If the ESP has not synchronized with NTP yet:

```json
"timestamp": null
```

The backend should add its own server-side timestamp:

```text
receivedAt
```

when the MQTT message is received.

---

## 4.3 `uptimeMs`

```json
"uptimeMs": 153420
```

Meaning:

```text
number of milliseconds since the ESP booted
```

This field is useful when:

- NTP is not available yet;
- debugging unexpected resets;
- detecting that a board has rebooted;
- determining event ordering.

---

## 4.4 `null` Values

If a sensor does not currently have a valid reading:

```json
"temperature": null
```

Do not replace missing values with fake values such as:

```json
"temperature": 0
```

because `0` may be a valid physical measurement.

---

# 5. ESP32-S3 Main

---

# 5.1 Topic: `disaster/main/telemetry`

## Purpose

Publishes aggregated data from:

- DHT11;
- MQ-2;
- JSN-SR04T;
- buzzer;
- Wi-Fi status;
- system diagnostics.

## Flow

```text
ESP32-S3 Main
      │
      │ publish approximately every 2 seconds
      ▼
disaster/main/telemetry
      │
      ▼
Backend
```

## Full JSON Payload

```json
{
  "deviceId": "main-01",
  "uptimeMs": 153420,
  "timestamp": "2026-08-15T00:30:15Z",

  "temperature": 30.2,
  "humidity": 68.0,
  "dhtLastReadValid": true,

  "mq2Raw": 634,
  "mq2Filtered": 631.5,
  "gasStatus": "SAFE",
  "mq2BaselineReady": true,
  "mq2Baseline": 415.8,

  "distanceCm": 65.4,
  "waterLevelCm": 34.6,
  "waterLastReadValid": true,
  "waterStatus": "WARNING",
  "installationHeightCm": 100.0,

  "buzzer": false,
  "buzzerMode": "OFF",
  "buzzerReason": "NONE",
  "manualBuzzerOn": false,

  "wifiConnected": true,
  "wifiRssi": -56,
  "freeHeap": 221456
}
```

---

## Field Definitions

### DHT11

| Field | Type | Example | Meaning |
|---|---|---:|---|
| `temperature` | number / null | `30.2` | Temperature in °C |
| `humidity` | number / null | `68.0` | Relative humidity in % |
| `dhtLastReadValid` | boolean | `true` | Whether the most recent DHT read was valid |

If the latest read fails but a previous valid value exists:

```json
{
  "temperature": 30.2,
  "humidity": 68.0,
  "dhtLastReadValid": false
}
```

This means the displayed values are the **last known good values**.

---

### MQ-2

| Field | Type | Example | Meaning |
|---|---|---:|---|
| `mq2Raw` | integer | `634` | Current raw ADC reading |
| `mq2Filtered` | number | `631.5` | Moving-average filtered value |
| `gasStatus` | string | `"SAFE"` | Gas hazard state |
| `mq2BaselineReady` | boolean | `true` | Whether baseline calibration is complete |
| `mq2Baseline` | number / null | `415.8` | MQ-2 baseline value |

Valid `gasStatus` values:

```text
SAFE
WARNING
DANGER
```

> `mq2Raw` and `mq2Filtered` are currently ADC values, **not ppm values**.

---

### Water / JSN-SR04T

| Field | Type | Example | Meaning |
|---|---|---:|---|
| `distanceCm` | number / null | `65.4` | Distance from sensor to water surface |
| `waterLevelCm` | number / null | `34.6` | Calculated water level from the bottom |
| `waterLastReadValid` | boolean | `true` | Whether the latest ultrasonic reading was valid |
| `waterStatus` | string | `"WARNING"` | Water-level state |
| `installationHeightCm` | number | `100.0` | Sensor installation height |

Relationship:

```text
waterLevelCm
=
installationHeightCm
-
distanceCm
```

Valid `waterStatus` values:

```text
SAFE
WARNING
DANGER
CRITICAL
```

---

### Buzzer

| Field | Type | Example | Meaning |
|---|---|---|---|
| `buzzer` | boolean | `false` | Actual buzzer output state |
| `buzzerMode` | string | `"AUTO"` | Source responsible for the active buzzer |
| `buzzerReason` | string | `"GAS_DANGER"` | Reason for the buzzer state |
| `manualBuzzerOn` | boolean | `false` | Whether the website is requesting buzzer ON |

Valid `buzzerMode` values:

```text
OFF
AUTO
MANUAL
AUTO+MANUAL
```

Valid `buzzerReason` values:

```text
NONE
WEB_COMMAND
GAS_DANGER
WATER_CRITICAL
GAS_DANGER+WATER_CRITICAL
```

---

### Network / Diagnostics

| Field | Type | Example | Meaning |
|---|---|---:|---|
| `wifiConnected` | boolean | `true` | Whether Wi-Fi is connected |
| `wifiRssi` | integer / null | `-56` | Wi-Fi RSSI in dBm |
| `freeHeap` | integer | `221456` | Remaining free heap memory |

Example RSSI interpretation:

```text
-40 dBm → excellent
-60 dBm → good
-75 dBm → weak
```

---

## Retain

```text
false
```

Old telemetry does not need to remain stored on the broker.

---

## Frequency

Current firmware:

```text
2000 ms
≈ 0.5 Hz
```

---

# 5.2 Topic: `disaster/main/status`

## Purpose

Indicates whether the ESP32-S3 Main is:

```text
online
```

or:

```text
offline
```

## Payload

### On Successful Connection

```text
online
```

### On Unexpected Disconnection

```text
offline
```

This topic does **not need JSON**. A short text payload is better suited for MQTT Last Will.

---

## Last Will

When Main connects to MQTT:

```text
Will Topic:
disaster/main/status

Will Payload:
offline

Will Retain:
true
```

After a successful connection, Main publishes:

```text
online
```

with retain enabled.

---

## Retain

```text
true
```

This allows a restarted backend to immediately retrieve the latest known device status.

---

# 5.3 Topic: `disaster/main/alert`

> **Status:** The contract is defined, but the current Main firmware still needs this publisher to be added.

## Purpose

Telemetry answers:

> What are the sensors currently measuring?

Alert answers:

> What dangerous event has just occurred?

The backend should not need to infer every state transition from periodic telemetry alone.

---

## GAS DANGER

```json
{
  "deviceId": "main-01",
  "type": "GAS",
  "level": "DANGER",
  "value": 1082.4,
  "unit": "adc",
  "previousState": "WARNING",
  "currentState": "DANGER",
  "eventUptimeMs": 153420,
  "timestamp": "2026-08-15T00:30:15Z"
}
```

---

## WATER DANGER

```json
{
  "deviceId": "main-01",
  "type": "WATER",
  "level": "DANGER",
  "value": 44.3,
  "unit": "cm",
  "previousState": "WARNING",
  "currentState": "DANGER",
  "eventUptimeMs": 164200,
  "timestamp": "2026-08-15T00:30:26Z"
}
```

---

## WATER CRITICAL

```json
{
  "deviceId": "main-01",
  "type": "WATER",
  "level": "CRITICAL",
  "value": 62.5,
  "unit": "cm",
  "previousState": "DANGER",
  "currentState": "CRITICAL",
  "eventUptimeMs": 170600,
  "timestamp": "2026-08-15T00:30:32Z"
}
```

---

## Field Contract

| Field | Type | Required | Meaning |
|---|---|---:|---|
| `deviceId` | string | Yes | `"main-01"` |
| `type` | string | Yes | `GAS` / `WATER` |
| `level` | string | Yes | `WARNING` / `DANGER` / `CRITICAL` |
| `value` | number | Yes | Sensor value at the time of the event |
| `unit` | string | Yes | `adc` / `cm` |
| `previousState` | string | Yes | Previous state |
| `currentState` | string | Yes | New state |
| `eventUptimeMs` | integer | Yes | Device uptime when the event occurred |
| `timestamp` | string / null | Yes | UTC time when NTP is available |

---

## When Should It Be Published?

Do not publish continuously.

Example:

```text
SAFE
 ↓
WARNING
 ↓
DANGER   ← publish alert
 ↓
DANGER
 ↓
DANGER   ← do not publish repeatedly
```

A recovery event could also be added later:

```text
DANGER
 ↓
WARNING
```

For example:

```json
{
  "type": "GAS",
  "level": "RECOVERED"
}
```

However, `RECOVERED` is **not part of contract v1.0**.

---

## Retain

```text
false
```

An alert is an event, not a current state.

---

# 5.4 Topic: `disaster/main/command/buzzer`

## Publisher

```text
Backend
```

## Subscriber

```text
ESP32-S3 Main
```

## Purpose

Allows the website to manually control the buzzer through the backend.

---

## Standard Payload

Turn ON:

```text
ON
```

Turn OFF:

```text
OFF
```

The current firmware also accepts:

```text
1
TRUE
```

as equivalent to `ON`, and:

```text
0
FALSE
```

as equivalent to `OFF`.

However, the backend should **only send**:

```text
ON
OFF
```

to keep the contract simple and consistent.

---

## No JSON for Command v1

There is no need to send:

```json
{
  "command": "ON"
}
```

because the command currently has only two states.

If future requirements include:

- duration;
- user;
- commandId;
- schedule;

then this command can be upgraded to a JSON payload.

---

## Retain

```text
false
```

This is important.

If an `ON` command were retained, the ESP could receive the old command after rebooting and turn the buzzer on unintentionally.

---

# 5.5 Topic: `disaster/main/state/buzzer`

## Purpose

The ESP publishes the **actual current buzzer state** after combining automatic and manual logic.

Flow:

```text
Web
 ↓
Backend
 ↓
command/buzzer = ON
 ↓
ESP Main
 ↓
process AUTO + MANUAL
 ↓
state/buzzer
 ↓
Backend / Web
```

---

## JSON Payload

### Buzzer OFF

```json
{
  "deviceId": "main-01",
  "state": false,
  "mode": "OFF",
  "reason": "NONE",
  "manualRequest": false,
  "autoDanger": false,
  "gasDanger": false,
  "waterCritical": false,
  "uptimeMs": 153420,
  "timestamp": "2026-08-15T00:30:15Z"
}
```

---

### Buzzer MANUAL

```json
{
  "deviceId": "main-01",
  "state": true,
  "mode": "MANUAL",
  "reason": "WEB_COMMAND",
  "manualRequest": true,
  "autoDanger": false,
  "gasDanger": false,
  "waterCritical": false,
  "uptimeMs": 160200,
  "timestamp": "2026-08-15T00:30:22Z"
}
```

---

### Buzzer AUTO Because of Gas

```json
{
  "deviceId": "main-01",
  "state": true,
  "mode": "AUTO",
  "reason": "GAS_DANGER",
  "manualRequest": false,
  "autoDanger": true,
  "gasDanger": true,
  "waterCritical": false,
  "uptimeMs": 175400,
  "timestamp": "2026-08-15T00:30:37Z"
}
```

---

### AUTO + MANUAL

```json
{
  "deviceId": "main-01",
  "state": true,
  "mode": "AUTO+MANUAL",
  "reason": "GAS_DANGER",
  "manualRequest": true,
  "autoDanger": true,
  "gasDanger": true,
  "waterCritical": false,
  "uptimeMs": 180000,
  "timestamp": "2026-08-15T00:30:42Z"
}
```

---

## Field Contract

| Field | Type | Meaning |
|---|---|---|
| `state` | boolean | Actual buzzer ON/OFF state |
| `mode` | string | `OFF`, `AUTO`, `MANUAL`, `AUTO+MANUAL` |
| `reason` | string | Reason for the current state |
| `manualRequest` | boolean | Whether the web requested ON |
| `autoDanger` | boolean | Whether an automatic danger condition exists |
| `gasDanger` | boolean | Whether gas is in `DANGER` |
| `waterCritical` | boolean | Whether water is in `CRITICAL` |

---

## Important Logic

```text
buzzer =
autoDanger
OR
manualRequest
```

Therefore:

```text
Gas DANGER
+
Web sends OFF
=
Buzzer remains ON
```

The website must not be able to disable an active automatic safety alarm.

---

## Retain

```text
true
```

This is a **current state**, so retaining it is appropriate.

---

# 6. XIAO ESP32-C3 F7

---

# 6.1 Topic: `disaster/f7/telemetry`

## Purpose

Publishes MPU6050 readings and processed motion information:

- acceleration;
- gyroscope;
- roll;
- pitch;
- tilt;
- vibration;
- impact;
- state;
- calibration;
- diagnostics.

---

## Frequency

The MPU6050 is sampled at:

```text
50 Hz
1 sample / 20 ms
```

but telemetry is published at only:

```text
1 Hz
1 message / second
```

This reduces MQTT traffic while still providing useful dashboard updates.

---

## Full JSON Payload

```json
{
  "deviceId": "f7-01",
  "uptimeMs": 153420,
  "timestamp": "2026-08-15T00:30:15Z",

  "mpuReady": true,
  "calibrationReady": true,

  "accelX": 0.12,
  "accelY": -0.08,
  "accelZ": 9.75,
  "accelMagnitude": 9.751,

  "gyroX": 0.01,
  "gyroY": 0.02,
  "gyroZ": 0.01,

  "rollDeg": 2.4,
  "pitchDeg": 3.1,
  "tiltAngleDeg": 3.9,
  "vibrationRms": 0.18,
  "impactDelta": 0.42,

  "tiltState": "NORMAL",
  "vibrationState": "NORMAL",
  "tiltDanger": false,
  "vibrationDanger": false,
  "impactDanger": false,
  "status": "NORMAL",

  "baselineRollDeg": 1.2,
  "baselinePitchDeg": 0.8,
  "baselineAccelMagnitude": 9.79,

  "batterySupported": false,
  "batteryVoltage": null,
  "batteryPercent": null,

  "pendingAlert": false,
  "wifiConnected": true,
  "wifiRssi": -58,
  "freeHeap": 183120
}
```

---

## MPU State

### `mpuReady`

```json
"mpuReady": true
```

means the MPU6050 has been detected and can be read successfully.

---

### `calibrationReady`

```json
"calibrationReady": true
```

The firmware uses approximately:

```text
100 samples
50 Hz
≈ 2 seconds
```

for initial calibration.

---

## Acceleration

| Field | Unit |
|---|---|
| `accelX` | m/s² |
| `accelY` | m/s² |
| `accelZ` | m/s² |
| `accelMagnitude` | m/s² |

Formula:

```text
A = sqrt(ax² + ay² + az²)
```

---

## Gyroscope

| Field | Unit |
|---|---|
| `gyroX` | rad/s |
| `gyroY` | rad/s |
| `gyroZ` | rad/s |

---

## Tilt

| Field | Unit |
|---|---|
| `rollDeg` | degree |
| `pitchDeg` | degree |
| `tiltAngleDeg` | degree |

`tiltAngleDeg` is the relative tilt compared with the baseline orientation captured during calibration.

---

## Vibration

```json
"vibrationRms": 0.18
```

Unit:

```text
m/s²
```

The firmware calculates RMS over approximately:

```text
25 samples
≈ 0.5 second at 50 Hz
```

---

## Impact

```json
"impactDelta": 12.8
```

This is calculated as:

```text
|accelerationMagnitude - baselineAcceleration|
```

If the value exceeds the impact threshold:

```json
"impactDanger": true
```

---

## State

### `tiltState`

```text
NORMAL
WARNING
DANGER
```

### `vibrationState`

```text
NORMAL
WARNING
DANGER
```

### `status`

```text
CALIBRATING
NORMAL
WARNING
DANGER
```

---

## Battery

The current firmware intentionally returns:

```json
{
  "batterySupported": false,
  "batteryVoltage": null,
  "batteryPercent": null
}
```

Do not generate a fake battery percentage when the board does not yet have an ADC-based battery measurement circuit.

---

## Retain

```text
false
```

---

# 6.2 Topic: `disaster/f7/status`

This topic works the same way as Main status.

## Online

```text
online
```

## Offline

```text
offline
```

The broker uses MQTT Last Will to publish `offline` if F7 unexpectedly loses power or connection.

## Retain

```text
true
```

---

# 6.3 Topic: `disaster/f7/alert`

## Purpose

Publishes a dangerous event immediately instead of waiting for the next telemetry interval.

There are three main alert types:

```text
TILT
VIBRATION
IMPACT
```

If multiple danger conditions occur at the same time:

```text
MULTIPLE
```

---

## TILT Alert

```json
{
  "deviceId": "f7-01",
  "type": "TILT",
  "level": "DANGER",
  "eventUptimeMs": 153420,
  "publishedUptimeMs": 153430,
  "tiltAngleDeg": 26.8,
  "vibrationRms": 0.42,
  "impactDelta": 0.65,
  "timestamp": "2026-08-15T00:30:15Z"
}
```

---

## VIBRATION Alert

```json
{
  "deviceId": "f7-01",
  "type": "VIBRATION",
  "level": "DANGER",
  "eventUptimeMs": 165200,
  "publishedUptimeMs": 165210,
  "tiltAngleDeg": 4.1,
  "vibrationRms": 3.18,
  "impactDelta": 1.35,
  "timestamp": "2026-08-15T00:30:27Z"
}
```

---

## IMPACT Alert

```json
{
  "deviceId": "f7-01",
  "type": "IMPACT",
  "level": "DANGER",
  "eventUptimeMs": 176400,
  "publishedUptimeMs": 176410,
  "tiltAngleDeg": 5.0,
  "vibrationRms": 1.75,
  "impactDelta": 14.9,
  "timestamp": "2026-08-15T00:30:38Z"
}
```

---

## MULTIPLE Alert

```json
{
  "deviceId": "f7-01",
  "type": "MULTIPLE",
  "level": "DANGER",
  "eventUptimeMs": 188000,
  "publishedUptimeMs": 188010,
  "tiltAngleDeg": 29.1,
  "vibrationRms": 3.42,
  "impactDelta": 13.8,
  "timestamp": "2026-08-15T00:30:50Z"
}
```

---

## Field Contract

| Field | Type | Meaning |
|---|---|---|
| `deviceId` | string | `"f7-01"` |
| `type` | string | `TILT`, `VIBRATION`, `IMPACT`, `MULTIPLE` |
| `level` | string | Currently always `"DANGER"` |
| `eventUptimeMs` | integer | Device uptime when the event occurred |
| `publishedUptimeMs` | integer | Device uptime when MQTT actually published it |
| `tiltAngleDeg` | number | Tilt angle at publish time |
| `vibrationRms` | number | Current vibration RMS |
| `impactDelta` | number | Current impact delta |
| `timestamp` | string / null | UTC timestamp |

---

## Why Are There Two Uptime Fields?

Example: F7 loses Wi-Fi:

```text
event occurs
eventUptimeMs = 100000
        │
        │ Wi-Fi unavailable
        ▼
alert stored as pending
        │
        │ reconnect 8 seconds later
        ▼
publishedUptimeMs = 108000
```

The backend can compare these two values to determine whether the alert was delayed by a network outage.

---

## Alert Cooldown

Current firmware:

```text
10 seconds
```

This prevents repeated danger samples from generating excessive notifications.

---

## Retain

```text
false
```

Old alerts must not be retained.

---

# 7. MQTT Subscriptions by Component

## ESP32-S3 Main

Subscribe:

```text
disaster/main/command/buzzer
```

Publish:

```text
disaster/main/telemetry
disaster/main/status
disaster/main/state/buzzer
```

After Main alert support is added:

```text
disaster/main/alert
```

---

## XIAO ESP32-C3 F7

Publish:

```text
disaster/f7/telemetry
disaster/f7/status
disaster/f7/alert
```

Currently:

```text
no command topic is subscribed
```

---

## Backend

Recommended subscriptions:

```text
disaster/+/telemetry
disaster/+/status
disaster/+/alert
disaster/main/state/buzzer
```

Backend publishes:

```text
disaster/main/command/buzzer
```

---

# 8. MQTT Wildcards

## `+`

Matches exactly **one topic level**.

```text
disaster/+/telemetry
```

matches:

```text
disaster/main/telemetry
disaster/f7/telemetry
```

but not:

```text
disaster/main/status
```

---

## `#`

Matches all remaining levels.

```text
disaster/#
```

matches every topic in the system.

This is useful during debugging with MQTT Explorer or `mosquitto_sub`.

---

# 9. Standard Retain Policy

| Message Type | Retain |
|---|---:|
| Telemetry | No |
| Alert | No |
| Command | No |
| Status | Yes |
| Current State | Yes |

General principle:

```text
EVENT → do not retain
STATE → may be retained
```

Example:

```text
IMPACT
```

is an event, so it should not be retained.

```text
buzzer = ON
```

is a current state, so retaining it is useful.

---

# 10. QoS Policy

The current firmware uses `PubSubClient`.

## Current Publishing Behavior

Telemetry, alert, online status, and state messages currently use:

```text
QoS 0
```

The Last Will is configured with:

```text
QoS 1
```

Main subscribes to the buzzer command with:

```text
QoS 1
```

If supported by the backend MQTT library, the backend should publish buzzer commands using QoS 1.

---

# 11. Suggested Backend Data Model

The backend can normalize an MQTT message into:

```json
{
  "topic": "disaster/main/telemetry",
  "deviceId": "main-01",
  "type": "TELEMETRY",
  "receivedAt": "2026-08-15T00:30:15.550Z",
  "payload": {}
}
```

Or:

```json
{
  "topic": "disaster/f7/alert",
  "deviceId": "f7-01",
  "type": "ALERT",
  "receivedAt": "2026-08-15T00:30:38.120Z",
  "payload": {}
}
```

This makes the database easier to extend later.

---

# 12. Website Mapping

## Main Dashboard

From:

```text
disaster/main/telemetry
```

map:

```text
Temperature       ← temperature
Humidity          ← humidity

Gas raw           ← mq2Raw
Gas filtered      ← mq2Filtered
Gas state         ← gasStatus

Distance          ← distanceCm
Water level       ← waterLevelCm
Water state       ← waterStatus

Buzzer            ← buzzer
Buzzer mode       ← buzzerMode
```

---

## F7 Dashboard

From:

```text
disaster/f7/telemetry
```

map:

```text
Tilt              ← tiltAngleDeg
Vibration         ← vibrationRms
Impact            ← impactDelta

Tilt state        ← tiltState
Vibration state   ← vibrationState
Overall status    ← status
```

---

# 13. Complete Runtime Examples

## Normal Operation

Main:

```text
disaster/main/telemetry
```

```json
{
  "deviceId": "main-01",
  "temperature": 29.5,
  "humidity": 70.0,
  "mq2Filtered": 420.5,
  "gasStatus": "SAFE",
  "waterLevelCm": 10.2,
  "waterStatus": "SAFE",
  "buzzer": false
}
```

F7:

```text
disaster/f7/telemetry
```

```json
{
  "deviceId": "f7-01",
  "tiltAngleDeg": 3.0,
  "vibrationRms": 0.2,
  "impactDelta": 0.3,
  "status": "NORMAL"
}
```

---

## Gas Danger

```text
MQ2
 ↓
gasStatus = DANGER
 ↓
Main local safety logic
 ↓
buzzer ON
```

Main telemetry:

```json
{
  "gasStatus": "DANGER",
  "buzzer": true,
  "buzzerMode": "AUTO",
  "buzzerReason": "GAS_DANGER"
}
```

Main alert after the feature is added:

```text
disaster/main/alert
```

```json
{
  "deviceId": "main-01",
  "type": "GAS",
  "level": "DANGER",
  "value": 1105.5,
  "unit": "adc"
}
```

Backend flow:

```text
Main Alert
 ↓
PushSafer
 ↓
Phone
```

---

## Impact Event

F7 detects:

```text
impactDelta >= threshold
```

and publishes:

```text
disaster/f7/alert
```

```json
{
  "deviceId": "f7-01",
  "type": "IMPACT",
  "level": "DANGER",
  "impactDelta": 14.9
}
```

The backend then sends a notification.

---

## Website Turns the Buzzer ON

Backend publishes:

```text
Topic:
disaster/main/command/buzzer

Payload:
ON
```

ESP receives:

```text
manualBuzzerOn = true
```

Then publishes:

```text
disaster/main/state/buzzer
```

```json
{
  "state": true,
  "mode": "MANUAL",
  "reason": "WEB_COMMAND"
}
```

---

# 14. Final Design Principles

```text
TELEMETRY
=
periodic sensor/system data


ALERT
=
an event that has just occurred


STATE
=
the device's actual current state


COMMAND
=
a requested action from the backend


STATUS
=
online / offline
```

The most important safety principle is:

```text
MQTT / Internet
=
communication layer
```

not:

```text
safety decision layer
```

The ESP32-S3 Main must continue to:

- read sensors;
- evaluate danger states;
- control LEDs;
- activate the buzzer;

even when:

```text
Wi-Fi = OFF
MQTT = OFF
Backend = OFF
Website = OFF
```

---

# 15. Contract v1.0 Fields That Must Stay Consistent

Backend, firmware, and frontend should agree on these names and avoid changing them independently:

```text
deviceId
timestamp
gasStatus
waterStatus
buzzerMode
buzzerReason
tiltState
vibrationState
status
```

Standard enums:

```text
Gas:
SAFE | WARNING | DANGER

Water:
SAFE | WARNING | DANGER | CRITICAL

F7 severity:
NORMAL | WARNING | DANGER

F7 overall:
CALIBRATING | NORMAL | WARNING | DANGER

Buzzer mode:
OFF | AUTO | MANUAL | AUTO+MANUAL
```

If any field name or enum value must change later, the contract version should be incremented instead of silently changing only one component.
