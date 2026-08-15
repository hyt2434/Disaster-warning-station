/*
 * Disaster Warning Station - ESP32-S3 MAIN
 *
 * Chuc nang:
 *   F1 - DHT11: nhiet do, do am
 *   F2 - Buzzer: AUTO tu cam bien + MANUAL tu MQTT
 *   F3 - MQ-2: raw, moving average, SAFE/WARNING/DANGER
 *   F6 - JSN-SR04T: khoang cach -> muc nuoc -> 4 cap LED
 *
 * Nguyen tac quan trong:
 *   - Canh bao cuc bo KHONG phu thuoc Wi-Fi/MQTT.
 *   - Wi-Fi/MQTT reconnect theo kieu non-blocking.
 *   - Cac nguong ben duoi la gia tri khoi dau de demo, BAT BUOC hieu chinh bang du lieu that.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

// ============================================================
// 1. USER CONFIG - SUA TRUOC KHI UPLOAD
// ============================================================
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "192.168.1.100";   // IP/domain Mosquitto broker
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";                // de rong neu broker khong auth
const char* MQTT_PASSWORD = "";

const char* DEVICE_ID = "main-01";

// MQTT topics
const char* TOPIC_TELEMETRY = "disaster/main/telemetry";
const char* TOPIC_STATUS = "disaster/main/status";
const char* TOPIC_BUZZER_COMMAND = "disaster/main/command/buzzer";
const char* TOPIC_BUZZER_STATE = "disaster/main/state/buzzer";

// ============================================================
// 2. PIN MAPPING - CHOT THEO SO DO HE THONG
// ============================================================
constexpr uint8_t DHT_PIN = 4;
constexpr uint8_t MQ2_PIN = 5;
constexpr uint8_t JSN_TRIG_PIN = 6;
constexpr uint8_t JSN_ECHO_PIN = 7;   // ECHO PHAI qua chia ap ve ~3.3V
constexpr uint8_t BUZZER_PIN = 15;
constexpr uint8_t LED_GREEN_PIN = 16;
constexpr uint8_t LED_YELLOW_PIN = 17;
constexpr uint8_t LED_RED_PIN = 18;

#define DHT_TYPE DHT11

// Neu module output cua ban active LOW, doi 2 dong nay.
constexpr uint8_t OUTPUT_ON = HIGH;
constexpr uint8_t OUTPUT_OFF = LOW;

// ============================================================
// 3. TIMING - KHONG DUNG delay() DAI TRONG loop()
// ============================================================
constexpr unsigned long DHT_INTERVAL_MS = 2000;
constexpr unsigned long MQ2_INTERVAL_MS = 150;
constexpr unsigned long WATER_INTERVAL_MS = 500;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 2000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 5000;
constexpr unsigned long LED_BLINK_INTERVAL_MS = 500;

// pulseIn chi block toi da 30 ms moi lan do.
constexpr unsigned long ECHO_TIMEOUT_US = 30000;

// ============================================================
// 4. CALIBRATION / THRESHOLDS
// ============================================================
// Chieu cao tu dau cam bien JSN xuong san/moc 0 cm.
// 100 cm la gia tri VI DU trong mo ta. Hay do lai tren mo hinh that.
float INSTALLATION_HEIGHT_CM = 100.0f;

// MQ-2: dang dung ADC raw/filtered, KHONG goi la ppm khi chua calibration.
// 1000/900 lay theo vi du hysteresis da chot; warning la moc khoi dau can hieu chinh.
constexpr float GAS_WARNING_ENTER = 700.0f;
constexpr float GAS_WARNING_EXIT  = 650.0f;
constexpr float GAS_DANGER_ENTER  = 1000.0f;
constexpr float GAS_DANGER_EXIT   = 900.0f;

// Muc nuoc (cm) - cac moc warning/danger la gia tri khoi dau de demo.
// Critical 60/55 cm lay theo vi du hysteresis trong mo ta.
constexpr float WATER_WARNING_ENTER_CM = 20.0f;
constexpr float WATER_WARNING_EXIT_CM  = 15.0f;
constexpr float WATER_DANGER_ENTER_CM  = 40.0f;
constexpr float WATER_DANGER_EXIT_CM   = 35.0f;
constexpr float WATER_CRITICAL_ENTER_CM = 60.0f;
constexpr float WATER_CRITICAL_EXIT_CM  = 55.0f;

// MQ-2 moving average
constexpr uint8_t MQ2_FILTER_SIZE = 10;

// Baseline chi de telemetry/tham chieu, KHONG dung truc tiep de tao ppm.
constexpr unsigned long MQ2_BASELINE_START_AFTER_MS = 30000;
constexpr uint16_t MQ2_BASELINE_SAMPLE_COUNT = 100;

// Water EMA filter
constexpr float WATER_FILTER_ALPHA = 0.30f;

// ============================================================
// 5. TYPES / GLOBAL STATE
// ============================================================
enum GasState : uint8_t {
  GAS_SAFE,
  GAS_WARNING,
  GAS_DANGER
};

enum WaterState : uint8_t {
  WATER_SAFE,
  WATER_WARNING,
  WATER_DANGER,
  WATER_CRITICAL
};

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Timers
unsigned long bootMs = 0;
unsigned long lastDhtReadMs = 0;
unsigned long lastMq2ReadMs = 0;
unsigned long lastWaterReadMs = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastWifiRetryMs = 0;
unsigned long lastMqttRetryMs = 0;
unsigned long lastLedBlinkMs = 0;

// Network state
bool ntpConfigured = false;
bool lastWifiConnected = false;
char mqttClientId[48] = {0};

// DHT
float temperatureC = NAN;
float humidityPct = NAN;
bool dhtValid = false;

// MQ-2
int mq2Raw = 0;
float mq2Filtered = 0.0f;
int mq2Samples[MQ2_FILTER_SIZE] = {0};
uint8_t mq2SampleIndex = 0;
uint8_t mq2SampleCount = 0;
long mq2SampleSum = 0;
GasState gasState = GAS_SAFE;

bool mq2BaselineReady = false;
double mq2BaselineAccumulator = 0.0;
uint16_t mq2BaselineSamples = 0;
float mq2Baseline = NAN;

// Water
float distanceCm = NAN;
float filteredDistanceCm = NAN;
float waterLevelCm = 0.0f;
bool waterValid = false;
WaterState waterState = WATER_SAFE;

// Outputs
bool manualBuzzerOn = false;
bool buzzerOn = false;
bool ledBlinkState = false;

// Used to detect changes for MQTT state/ack
bool prevManualBuzzerOn = false;
bool prevGasDanger = false;
bool prevWaterCritical = false;
bool prevBuzzerOn = false;

// ============================================================
// 6. HELPER FUNCTIONS
// ============================================================
const char* gasStateToString(GasState state) {
  switch (state) {
    case GAS_WARNING: return "WARNING";
    case GAS_DANGER:  return "DANGER";
    default:          return "SAFE";
  }
}

const char* waterStateToString(WaterState state) {
  switch (state) {
    case WATER_WARNING:  return "WARNING";
    case WATER_DANGER:   return "DANGER";
    case WATER_CRITICAL: return "CRITICAL";
    default:             return "SAFE";
  }
}

bool gasDanger() {
  return gasState == GAS_DANGER;
}

bool waterCritical() {
  return waterState == WATER_CRITICAL;
}

bool autoDanger() {
  return gasDanger() || waterCritical();
}

void setLedPins(bool green, bool yellow, bool red) {
  digitalWrite(LED_GREEN_PIN, green ? OUTPUT_ON : OUTPUT_OFF);
  digitalWrite(LED_YELLOW_PIN, yellow ? OUTPUT_ON : OUTPUT_OFF);
  digitalWrite(LED_RED_PIN, red ? OUTPUT_ON : OUTPUT_OFF);
}

void setBuzzerHardware(bool on) {
  digitalWrite(BUZZER_PIN, on ? OUTPUT_ON : OUTPUT_OFF);
}

void buildMqttClientId() {
  uint64_t chipId = ESP.getEfuseMac();
  uint16_t shortId = static_cast<uint16_t>(chipId & 0xFFFF);
  snprintf(mqttClientId, sizeof(mqttClientId), "%s-%04X", DEVICE_ID, shortId);
}

bool getUtcIsoTime(char* out, size_t outSize) {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return false;
  }

  struct tm timeInfo;
  gmtime_r(&now, &timeInfo);
  strftime(out, outSize, "%Y-%m-%dT%H:%M:%SZ", &timeInfo);
  return true;
}

void configureNtpIfNeeded() {
  if (!ntpConfigured && WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    ntpConfigured = true;
    Serial.println("[TIME] NTP configured (UTC).");
  }
}

// ============================================================
// 7. WI-FI / MQTT - NON-BLOCKING RECONNECT
// ============================================================
void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiRetryMs = millis();
  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
}

void maintainWiFi() {
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected) {
    if (!lastWifiConnected) {
      Serial.print("[WiFi] Connected. IP: ");
      Serial.println(WiFi.localIP());
      configureNtpIfNeeded();
    }
    lastWifiConnected = true;
    return;
  }

  if (lastWifiConnected) {
    Serial.println("[WiFi] Disconnected. Local safety still active.");
  }
  lastWifiConnected = false;

  unsigned long now = millis();
  if (now - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
    lastWifiRetryMs = now;
    Serial.println("[WiFi] Retry connect...");
    WiFi.disconnect(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void publishBuzzerState();

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_BUZZER_COMMAND) != 0) {
    return;
  }

  char command[32];
  unsigned int copyLen = length;
  if (copyLen >= sizeof(command)) {
    copyLen = sizeof(command) - 1;
  }
  memcpy(command, payload, copyLen);
  command[copyLen] = '\0';

  // Trim trailing whitespace
  while (copyLen > 0 && isspace(static_cast<unsigned char>(command[copyLen - 1]))) {
    command[--copyLen] = '\0';
  }

  // Trim leading whitespace by shifting
  unsigned int start = 0;
  while (command[start] && isspace(static_cast<unsigned char>(command[start]))) {
    start++;
  }
  if (start > 0) {
    memmove(command, command + start, strlen(command + start) + 1);
  }

  for (size_t i = 0; command[i] != '\0'; ++i) {
    command[i] = toupper(static_cast<unsigned char>(command[i]));
  }

  if (strcmp(command, "ON") == 0 || strcmp(command, "1") == 0 || strcmp(command, "TRUE") == 0) {
    manualBuzzerOn = true;
    Serial.println("[MQTT] Manual buzzer command: ON");
  } else if (strcmp(command, "OFF") == 0 || strcmp(command, "0") == 0 || strcmp(command, "FALSE") == 0) {
    manualBuzzerOn = false;
    Serial.println("[MQTT] Manual buzzer command: OFF");
  } else {
    Serial.printf("[MQTT] Unknown buzzer command: %s\n", command);
    return;
  }

  // Cap nhat output ngay, roi publish state nhu ACK.
  bool shouldBeOn = autoDanger() || manualBuzzerOn;
  buzzerOn = shouldBeOn;
  setBuzzerHardware(buzzerOn);
  publishBuzzerState();
}

bool mqttConnectWithLastWill() {
  const char* willMessage = "offline";

  if (strlen(MQTT_USER) > 0) {
    return mqttClient.connect(
      mqttClientId,
      MQTT_USER,
      MQTT_PASSWORD,
      TOPIC_STATUS,
      1,
      true,
      willMessage
    );
  }

  return mqttClient.connect(
    mqttClientId,
    TOPIC_STATUS,
    1,
    true,
    willMessage
  );
}

void maintainMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastMqttRetryMs < MQTT_RETRY_INTERVAL_MS) {
    return;
  }
  lastMqttRetryMs = now;

  Serial.printf("[MQTT] Connecting to %s:%u ...\n", MQTT_HOST, MQTT_PORT);

  if (mqttConnectWithLastWill()) {
    Serial.println("[MQTT] Connected.");
    mqttClient.publish(TOPIC_STATUS, "online", true);
    mqttClient.subscribe(TOPIC_BUZZER_COMMAND, 1);
    publishBuzzerState();
  } else {
    Serial.printf("[MQTT] Connect failed. state=%d. Retry later.\n", mqttClient.state());
  }
}

// ============================================================
// 8. SENSOR READINGS
// ============================================================
void readDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    // Giu lai gia tri hop le cu, khong overwrite bang NaN.
    dhtValid = false;
    Serial.println("[DHT] Read failed. Keep last valid values.");
    return;
  }

  humidityPct = h;
  temperatureC = t;
  dhtValid = true;
}

void updateMq2MovingAverage(int newSample) {
  if (mq2SampleCount < MQ2_FILTER_SIZE) {
    mq2Samples[mq2SampleIndex] = newSample;
    mq2SampleSum += newSample;
    mq2SampleCount++;
  } else {
    mq2SampleSum -= mq2Samples[mq2SampleIndex];
    mq2Samples[mq2SampleIndex] = newSample;
    mq2SampleSum += newSample;
  }

  mq2SampleIndex = (mq2SampleIndex + 1) % MQ2_FILTER_SIZE;
  mq2Filtered = static_cast<float>(mq2SampleSum) / mq2SampleCount;
}

void updateMq2Baseline() {
  if (mq2BaselineReady) {
    return;
  }

  if (millis() - bootMs < MQ2_BASELINE_START_AFTER_MS) {
    return;
  }

  mq2BaselineAccumulator += mq2Filtered;
  mq2BaselineSamples++;

  if (mq2BaselineSamples >= MQ2_BASELINE_SAMPLE_COUNT) {
    mq2Baseline = static_cast<float>(mq2BaselineAccumulator / mq2BaselineSamples);
    mq2BaselineReady = true;
    Serial.printf("[MQ2] Baseline ready: %.1f ADC\n", mq2Baseline);
  }
}

void evaluateGasState() {
  const float v = mq2Filtered;

  switch (gasState) {
    case GAS_SAFE:
      if (v >= GAS_DANGER_ENTER) {
        gasState = GAS_DANGER;
      } else if (v >= GAS_WARNING_ENTER) {
        gasState = GAS_WARNING;
      }
      break;

    case GAS_WARNING:
      if (v >= GAS_DANGER_ENTER) {
        gasState = GAS_DANGER;
      } else if (v < GAS_WARNING_EXIT) {
        gasState = GAS_SAFE;
      }
      break;

    case GAS_DANGER:
      if (v < GAS_DANGER_EXIT) {
        gasState = (v < GAS_WARNING_EXIT) ? GAS_SAFE : GAS_WARNING;
      }
      break;
  }
}

void readMQ2() {
  mq2Raw = analogRead(MQ2_PIN);
  updateMq2MovingAverage(mq2Raw);
  updateMq2Baseline();
  evaluateGasState();
}

bool readUltrasonicDistance(float& measuredCm) {
  digitalWrite(JSN_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(JSN_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(JSN_TRIG_PIN, LOW);

  unsigned long durationUs = pulseIn(JSN_ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (durationUs == 0) {
    return false;
  }

  float cm = (durationUs * 0.0343f) / 2.0f;

  // Gioi han hop ly rong de tranh nhan pulse loi.
  if (cm <= 0.0f || cm > 600.0f) {
    return false;
  }

  measuredCm = cm;
  return true;
}

void evaluateWaterState() {
  const float v = waterLevelCm;

  switch (waterState) {
    case WATER_SAFE:
      if (v >= WATER_CRITICAL_ENTER_CM) {
        waterState = WATER_CRITICAL;
      } else if (v >= WATER_DANGER_ENTER_CM) {
        waterState = WATER_DANGER;
      } else if (v >= WATER_WARNING_ENTER_CM) {
        waterState = WATER_WARNING;
      }
      break;

    case WATER_WARNING:
      if (v >= WATER_CRITICAL_ENTER_CM) {
        waterState = WATER_CRITICAL;
      } else if (v >= WATER_DANGER_ENTER_CM) {
        waterState = WATER_DANGER;
      } else if (v < WATER_WARNING_EXIT_CM) {
        waterState = WATER_SAFE;
      }
      break;

    case WATER_DANGER:
      if (v >= WATER_CRITICAL_ENTER_CM) {
        waterState = WATER_CRITICAL;
      } else if (v < WATER_DANGER_EXIT_CM) {
        waterState = (v < WATER_WARNING_EXIT_CM) ? WATER_SAFE : WATER_WARNING;
      }
      break;

    case WATER_CRITICAL:
      if (v < WATER_CRITICAL_EXIT_CM) {
        if (v >= WATER_DANGER_ENTER_CM) {
          waterState = WATER_DANGER;
        } else if (v >= WATER_WARNING_ENTER_CM) {
          waterState = WATER_WARNING;
        } else {
          waterState = WATER_SAFE;
        }
      }
      break;
  }
}

void readWater() {
  float newDistance = NAN;
  if (!readUltrasonicDistance(newDistance)) {
    waterValid = false;
    Serial.println("[JSN] No valid echo. Keep last valid water state.");
    return;
  }

  distanceCm = newDistance;

  if (isnan(filteredDistanceCm)) {
    filteredDistanceCm = distanceCm;
  } else {
    filteredDistanceCm = WATER_FILTER_ALPHA * distanceCm
                       + (1.0f - WATER_FILTER_ALPHA) * filteredDistanceCm;
  }

  float level = INSTALLATION_HEIGHT_CM - filteredDistanceCm;
  if (level < 0.0f) level = 0.0f;
  if (level > INSTALLATION_HEIGHT_CM) level = INSTALLATION_HEIGHT_CM;

  waterLevelCm = level;
  waterValid = true;
  evaluateWaterState();
}

// ============================================================
// 9. LOCAL SAFETY OUTPUTS
// ============================================================
void updateLED() {
  switch (waterState) {
    case WATER_SAFE:
      setLedPins(true, false, false);
      break;

    case WATER_WARNING:
      setLedPins(false, true, false);
      break;

    case WATER_DANGER:
      setLedPins(false, false, true);
      break;

    case WATER_CRITICAL: {
      unsigned long now = millis();
      if (now - lastLedBlinkMs >= LED_BLINK_INTERVAL_MS) {
        lastLedBlinkMs = now;
        ledBlinkState = !ledBlinkState;
      }
      setLedPins(false, false, ledBlinkState);
      break;
    }
  }
}

const char* buzzerModeString() {
  if (autoDanger() && manualBuzzerOn) return "AUTO+MANUAL";
  if (autoDanger()) return "AUTO";
  if (manualBuzzerOn) return "MANUAL";
  return "OFF";
}

const char* buzzerReasonString() {
  if (gasDanger() && waterCritical()) return "GAS_DANGER+WATER_CRITICAL";
  if (gasDanger()) return "GAS_DANGER";
  if (waterCritical()) return "WATER_CRITICAL";
  if (manualBuzzerOn) return "WEB_COMMAND";
  return "NONE";
}

void updateBuzzer() {
  bool newState = autoDanger() || manualBuzzerOn;
  buzzerOn = newState;
  setBuzzerHardware(buzzerOn);

  bool needPublish =
    (prevBuzzerOn != buzzerOn) ||
    (prevManualBuzzerOn != manualBuzzerOn) ||
    (prevGasDanger != gasDanger()) ||
    (prevWaterCritical != waterCritical());

  if (needPublish) {
    publishBuzzerState();

    prevBuzzerOn = buzzerOn;
    prevManualBuzzerOn = manualBuzzerOn;
    prevGasDanger = gasDanger();
    prevWaterCritical = waterCritical();
  }
}

// ============================================================
// 10. MQTT PUBLISH
// ============================================================
void publishBuzzerState() {
  if (!mqttClient.connected()) {
    return;
  }

  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["state"] = buzzerOn;
  doc["mode"] = buzzerModeString();
  doc["reason"] = buzzerReasonString();
  doc["manualRequest"] = manualBuzzerOn;
  doc["autoDanger"] = autoDanger();
  doc["gasDanger"] = gasDanger();
  doc["waterCritical"] = waterCritical();
  doc["uptimeMs"] = millis();

  char isoTime[25];
  if (getUtcIsoTime(isoTime, sizeof(isoTime))) {
    doc["timestamp"] = isoTime;
  } else {
    doc["timestamp"] = nullptr;
  }

  char payload[512];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n > 0 && n < sizeof(payload)) {
    mqttClient.publish(TOPIC_BUZZER_STATE, payload, true);
  }
}

void publishTelemetry() {
  if (!mqttClient.connected()) {
    return;
  }

  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["uptimeMs"] = millis();

  char isoTime[25];
  if (getUtcIsoTime(isoTime, sizeof(isoTime))) {
    doc["timestamp"] = isoTime;
  } else {
    doc["timestamp"] = nullptr;
  }

  // DHT
  if (!isnan(temperatureC)) doc["temperature"] = temperatureC;
  else doc["temperature"] = nullptr;

  if (!isnan(humidityPct)) doc["humidity"] = humidityPct;
  else doc["humidity"] = nullptr;

  doc["dhtLastReadValid"] = dhtValid;

  // MQ-2
  doc["mq2Raw"] = mq2Raw;
  doc["mq2Filtered"] = mq2Filtered;
  doc["gasStatus"] = gasStateToString(gasState);
  doc["mq2BaselineReady"] = mq2BaselineReady;
  if (mq2BaselineReady) doc["mq2Baseline"] = mq2Baseline;
  else doc["mq2Baseline"] = nullptr;

  // Water
  if (waterValid || !isnan(filteredDistanceCm)) {
    doc["distanceCm"] = filteredDistanceCm;
    doc["waterLevelCm"] = waterLevelCm;
  } else {
    doc["distanceCm"] = nullptr;
    doc["waterLevelCm"] = nullptr;
  }
  doc["waterLastReadValid"] = waterValid;
  doc["waterStatus"] = waterStateToString(waterState);
  doc["installationHeightCm"] = INSTALLATION_HEIGHT_CM;

  // Buzzer / network
  doc["buzzer"] = buzzerOn;
  doc["buzzerMode"] = buzzerModeString();
  doc["buzzerReason"] = buzzerReasonString();
  doc["manualBuzzerOn"] = manualBuzzerOn;
  doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  if (WiFi.status() == WL_CONNECTED) doc["wifiRssi"] = WiFi.RSSI();
  else doc["wifiRssi"] = nullptr;
  doc["freeHeap"] = ESP.getFreeHeap();

  char payload[1400];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n > 0 && n < sizeof(payload)) {
    mqttClient.publish(TOPIC_TELEMETRY, payload, false);
  }
}

// ============================================================
// 11. SETUP / LOOP
// ============================================================
void setup() {
  Serial.begin(9600);
  bootMs = millis();

  pinMode(MQ2_PIN, INPUT);
  pinMode(JSN_TRIG_PIN, OUTPUT);
  pinMode(JSN_ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);

  // Safe startup: buzzer OFF, LED SAFE.
  setBuzzerHardware(false);
  buzzerOn = false;
  setLedPins(true, false, false);

  digitalWrite(JSN_TRIG_PIN, LOW);

  analogReadResolution(12); // ESP32: 0..4095
  dht.begin();

  buildMqttClientId();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1536);
  mqttClient.setKeepAlive(20);
  mqttClient.setSocketTimeout(2);

  startWiFi();

  Serial.println("[MAIN] Disaster Warning Station started.");
  Serial.println("[MAIN] Local sensor/LED/buzzer logic works even if network is offline.");
}

void loop() {
  unsigned long now = millis();

  maintainWiFi();
  maintainMQTT();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  if (now - lastDhtReadMs >= DHT_INTERVAL_MS) {
    lastDhtReadMs = now;
    readDHT();
  }

  if (now - lastMq2ReadMs >= MQ2_INTERVAL_MS) {
    lastMq2ReadMs = now;
    readMQ2();
  }

  if (now - lastWaterReadMs >= WATER_INTERVAL_MS) {
    lastWaterReadMs = now;
    readWater();
  }

  // Local safety outputs run regardless of network state.
  updateLED();
  updateBuzzer();

  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    publishTelemetry();
  }
}
