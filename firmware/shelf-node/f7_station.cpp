/*
 * Disaster Warning Station - XIAO ESP32-C3 F7 NODE
 *
 * Chuc nang:
 *   F7 - MPU6050: gia toc, gyro, nghieng, rung, va cham
 *        -> NORMAL / WARNING / DANGER
 *        -> MQTT telemetry + alert event
 *
 * Nguyen tac:
 *   - MPU6050 duoc sample 50 Hz (20 ms/sample).
 *   - MQTT chi publish telemetry 1 Hz de giam traffic.
 *   - Alert co cooldown.
 *   - Neu mat mang, node van tiep tuc phat hien local; 1 alert gan nhat se duoc queue trong RAM.
 *   - XIAO ESP32-C3 KHONG co san battery percentage trong software neu khong them mach do pin.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include <string.h>

// ============================================================
// 1. USER CONFIG - SUA TRUOC KHI UPLOAD
// ============================================================
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "192.168.1.100";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

const char* DEVICE_ID = "f7-01";

const char* TOPIC_TELEMETRY = "disaster/f7/telemetry";
const char* TOPIC_ALERT = "disaster/f7/alert";
const char* TOPIC_STATUS = "disaster/f7/status";

// ============================================================
// 2. XIAO ESP32-C3 <-> MPU6050 PIN MAPPING
// ============================================================
constexpr uint8_t MPU_SDA_PIN = 6; // D4 / GPIO6
constexpr uint8_t MPU_SCL_PIN = 7; // D5 / GPIO7

// ============================================================
// 3. TIMING
// ============================================================
constexpr unsigned long SAMPLE_INTERVAL_MS = 20;      // 50 Hz
constexpr unsigned long TELEMETRY_INTERVAL_MS = 1000; // 1 Hz
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 5000;
constexpr unsigned long MPU_RETRY_INTERVAL_MS = 5000;
constexpr unsigned long ALERT_COOLDOWN_MS = 10000;
constexpr unsigned long IMPACT_LATCH_MS = 1500;

// 2 giay calibration neu sample 50 Hz.
constexpr uint16_t CALIBRATION_SAMPLES = 100;

// RMS cua 25 mau = cua so 0.5 s o 50 Hz.
constexpr uint8_t VIBRATION_WINDOW_SIZE = 25;

// ============================================================
// 4. THRESHOLDS - GIA TRI KHOI DAU, CAN CALIBRATION THUC NGHIEM
// ============================================================
constexpr float TILT_WARNING_ENTER_DEG = 10.0f;
constexpr float TILT_WARNING_EXIT_DEG  = 8.0f;
constexpr float TILT_DANGER_ENTER_DEG  = 20.0f;
constexpr float TILT_DANGER_EXIT_DEG   = 15.0f;

// vibrationRms la RMS cua |A - baselineA|, don vi m/s^2.
constexpr float VIB_WARNING_ENTER = 1.20f;
constexpr float VIB_WARNING_EXIT  = 0.80f;
constexpr float VIB_DANGER_ENTER  = 2.50f;
constexpr float VIB_DANGER_EXIT   = 1.80f;

// Va cham: do lech acceleration magnitude so voi baseline.
// Vi du trong mo ta co spike ~24.5 m/s^2 khi binh thuong ~9.8.
constexpr float IMPACT_DELTA_THRESHOLD = 10.0f;

// ============================================================
// 5. TYPES / GLOBALS
// ============================================================
enum Severity : uint8_t {
  LEVEL_NORMAL,
  LEVEL_WARNING,
  LEVEL_DANGER
};

enum MotionStatus : uint8_t {
  STATUS_CALIBRATING,
  STATUS_NORMAL,
  STATUS_WARNING,
  STATUS_DANGER
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Adafruit_MPU6050 mpu;

bool mpuReady = false;
bool calibrationReady = false;
bool ntpConfigured = false;
bool lastWifiConnected = false;

char mqttClientId[48] = {0};

unsigned long lastSampleMs = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastWifiRetryMs = 0;
unsigned long lastMqttRetryMs = 0;
unsigned long lastMpuRetryMs = 0;
unsigned long lastAlertMs = 0;
bool hasAlerted = false;

// Latest sensor values
float accelX = 0.0f;
float accelY = 0.0f;
float accelZ = 0.0f;
float gyroX = 0.0f;
float gyroY = 0.0f;
float gyroZ = 0.0f;
float rollDeg = 0.0f;
float pitchDeg = 0.0f;
float tiltAngleDeg = 0.0f;
float accelerationMagnitude = 0.0f;
float impactDelta = 0.0f;
float vibrationRms = 0.0f;

// Calibration sums / baseline
uint16_t calibrationCount = 0;
double sumRoll = 0.0;
double sumPitch = 0.0;
double sumAccelMagnitude = 0.0;
double sumGyroX = 0.0;
double sumGyroY = 0.0;
double sumGyroZ = 0.0;

float baselineRollDeg = 0.0f;
float baselinePitchDeg = 0.0f;
float baselineAccelMagnitude = 9.80665f;
float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;

// Vibration RMS ring buffer
float vibrationSquares[VIBRATION_WINDOW_SIZE] = {0};
uint8_t vibrationIndex = 0;
uint8_t vibrationCount = 0;
float vibrationSquareSum = 0.0f;

Severity tiltSeverity = LEVEL_NORMAL;
Severity vibrationSeverity = LEVEL_NORMAL;
MotionStatus motionStatus = STATUS_CALIBRATING;
MotionStatus previousMotionStatus = STATUS_CALIBRATING;

bool impactLatched = false;
unsigned long impactDetectedAtMs = 0;

// Offline alert queue - luu 1 event gan nhat trong RAM.
bool pendingAlert = false;
char pendingAlertType[20] = {0};
unsigned long pendingAlertUptimeMs = 0;

// ============================================================
// 6. HELPERS
// ============================================================
const char* severityToString(Severity s) {
  switch (s) {
    case LEVEL_WARNING: return "WARNING";
    case LEVEL_DANGER:  return "DANGER";
    default:            return "NORMAL";
  }
}

const char* motionStatusToString(MotionStatus s) {
  switch (s) {
    case STATUS_CALIBRATING: return "CALIBRATING";
    case STATUS_WARNING:     return "WARNING";
    case STATUS_DANGER:      return "DANGER";
    default:                 return "NORMAL";
  }
}

bool tiltDanger() {
  return tiltSeverity == LEVEL_DANGER;
}

bool vibrationDanger() {
  return vibrationSeverity == LEVEL_DANGER;
}

bool impactDanger() {
  if (!impactLatched) return false;
  if (millis() - impactDetectedAtMs >= IMPACT_LATCH_MS) {
    impactLatched = false;
    return false;
  }
  return true;
}

void buildMqttClientId() {
  uint64_t chipId = ESP.getEfuseMac();
  uint16_t shortId = static_cast<uint16_t>(chipId & 0xFFFF);
  snprintf(mqttClientId, sizeof(mqttClientId), "%s-%04X", DEVICE_ID, shortId);
}

bool getUtcIsoTime(char* out, size_t outSize) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;

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

float normalizeAngle180(float angle) {
  while (angle > 180.0f) angle -= 360.0f;
  while (angle < -180.0f) angle += 360.0f;
  return angle;
}

float angleDifference(float a, float b) {
  return normalizeAngle180(a - b);
}

void clearVibrationWindow() {
  memset(vibrationSquares, 0, sizeof(vibrationSquares));
  vibrationIndex = 0;
  vibrationCount = 0;
  vibrationSquareSum = 0.0f;
  vibrationRms = 0.0f;
}

void resetCalibration() {
  calibrationReady = false;
  calibrationCount = 0;
  sumRoll = 0.0;
  sumPitch = 0.0;
  sumAccelMagnitude = 0.0;
  sumGyroX = 0.0;
  sumGyroY = 0.0;
  sumGyroZ = 0.0;

  baselineRollDeg = 0.0f;
  baselinePitchDeg = 0.0f;
  baselineAccelMagnitude = 9.80665f;
  gyroBiasX = 0.0f;
  gyroBiasY = 0.0f;
  gyroBiasZ = 0.0f;

  tiltSeverity = LEVEL_NORMAL;
  vibrationSeverity = LEVEL_NORMAL;
  motionStatus = STATUS_CALIBRATING;
  previousMotionStatus = STATUS_CALIBRATING;
  impactLatched = false;
  clearVibrationWindow();

  Serial.println("[MPU] Calibration reset. Keep the node still for ~2 seconds.");
}

// ============================================================
// 7. WI-FI / MQTT
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
    Serial.println("[WiFi] Disconnected. Motion detection continues locally.");
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

bool mqttConnectWithLastWill() {
  if (strlen(MQTT_USER) > 0) {
    return mqttClient.connect(
      mqttClientId,
      MQTT_USER,
      MQTT_PASSWORD,
      TOPIC_STATUS,
      1,
      true,
      "offline"
    );
  }

  return mqttClient.connect(
    mqttClientId,
    TOPIC_STATUS,
    1,
    true,
    "offline"
  );
}

void flushPendingAlert();

void maintainMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttRetryMs < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttRetryMs = now;

  Serial.printf("[MQTT] Connecting to %s:%u ...\n", MQTT_HOST, MQTT_PORT);

  if (mqttConnectWithLastWill()) {
    Serial.println("[MQTT] Connected.");
    mqttClient.publish(TOPIC_STATUS, "online", true);
    flushPendingAlert();
  } else {
    Serial.printf("[MQTT] Connect failed. state=%d. Retry later.\n", mqttClient.state());
  }
}

// ============================================================
// 8. MPU6050 INITIALIZATION / RETRY
// ============================================================
void configureMpu() {
  // Theo API Adafruit MPU6050.
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  resetCalibration();
}

bool tryInitMpu() {
  if (mpu.begin()) {
    mpuReady = true;
    configureMpu();
    Serial.println("[MPU] MPU6050 found and configured.");
    return true;
  }

  mpuReady = false;
  Serial.println("[MPU] MPU6050 not found. Will retry non-blocking.");
  return false;
}

void maintainMpu() {
  if (mpuReady) return;

  unsigned long now = millis();
  if (now - lastMpuRetryMs < MPU_RETRY_INTERVAL_MS) return;
  lastMpuRetryMs = now;
  tryInitMpu();
}

// ============================================================
// 9. MOTION PROCESSING
// ============================================================
Severity updateSeverity(
  Severity current,
  float value,
  float warningEnter,
  float warningExit,
  float dangerEnter,
  float dangerExit
) {
  switch (current) {
    case LEVEL_NORMAL:
      if (value >= dangerEnter) return LEVEL_DANGER;
      if (value >= warningEnter) return LEVEL_WARNING;
      return LEVEL_NORMAL;

    case LEVEL_WARNING:
      if (value >= dangerEnter) return LEVEL_DANGER;
      if (value < warningExit) return LEVEL_NORMAL;
      return LEVEL_WARNING;

    case LEVEL_DANGER:
      if (value < dangerExit) {
        if (value < warningExit) return LEVEL_NORMAL;
        return LEVEL_WARNING;
      }
      return LEVEL_DANGER;
  }

  return LEVEL_NORMAL;
}

void updateVibration(float dynamicAcceleration) {
  float square = dynamicAcceleration * dynamicAcceleration;

  if (vibrationCount < VIBRATION_WINDOW_SIZE) {
    vibrationSquares[vibrationIndex] = square;
    vibrationSquareSum += square;
    vibrationCount++;
  } else {
    vibrationSquareSum -= vibrationSquares[vibrationIndex];
    vibrationSquares[vibrationIndex] = square;
    vibrationSquareSum += square;
  }

  vibrationIndex = (vibrationIndex + 1) % VIBRATION_WINDOW_SIZE;
  vibrationRms = sqrtf(vibrationSquareSum / vibrationCount);
}

void updateMotionStatus() {
  bool impact = impactDanger();

  if (!calibrationReady) {
    motionStatus = STATUS_CALIBRATING;
    return;
  }

  if (impact || tiltSeverity == LEVEL_DANGER || vibrationSeverity == LEVEL_DANGER) {
    motionStatus = STATUS_DANGER;
  } else if (tiltSeverity == LEVEL_WARNING || vibrationSeverity == LEVEL_WARNING) {
    motionStatus = STATUS_WARNING;
  } else {
    motionStatus = STATUS_NORMAL;
  }
}

const char* currentDangerReason() {
  bool impact = impactDanger();
  bool tilt = tiltDanger();
  bool vib = vibrationDanger();

  uint8_t count = (impact ? 1 : 0) + (tilt ? 1 : 0) + (vib ? 1 : 0);
  if (count > 1) return "MULTIPLE";
  if (impact) return "IMPACT";
  if (tilt) return "TILT";
  if (vib) return "VIBRATION";
  return "UNKNOWN";
}

void queueAlert(const char* type, unsigned long eventUptimeMs) {
  pendingAlert = true;
  strncpy(pendingAlertType, type, sizeof(pendingAlertType) - 1);
  pendingAlertType[sizeof(pendingAlertType) - 1] = '\0';
  pendingAlertUptimeMs = eventUptimeMs;
}

bool canSendAlertNow() {
  if (!hasAlerted) return true;
  return (millis() - lastAlertMs >= ALERT_COOLDOWN_MS);
}

bool publishAlertPayload(const char* type, unsigned long eventUptimeMs) {
  if (!mqttClient.connected()) return false;

  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["type"] = type;
  doc["level"] = "DANGER";
  doc["eventUptimeMs"] = eventUptimeMs;
  doc["publishedUptimeMs"] = millis();
  doc["tiltAngleDeg"] = tiltAngleDeg;
  doc["vibrationRms"] = vibrationRms;
  doc["impactDelta"] = impactDelta;

  char isoTime[25];
  if (getUtcIsoTime(isoTime, sizeof(isoTime))) doc["timestamp"] = isoTime;
  else doc["timestamp"] = nullptr;

  char payload[600];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload)) return false;

  return mqttClient.publish(TOPIC_ALERT, payload, false);
}

void triggerAlert(const char* type, unsigned long eventUptimeMs) {
  if (!canSendAlertNow()) {
    return;
  }

  hasAlerted = true;
  lastAlertMs = millis();

  if (publishAlertPayload(type, eventUptimeMs)) {
    Serial.printf("[ALERT] Published: %s\n", type);
    pendingAlert = false;
  } else {
    Serial.printf("[ALERT] Network unavailable, queue: %s\n", type);
    queueAlert(type, eventUptimeMs);
  }
}

void flushPendingAlert() {
  if (!pendingAlert || !mqttClient.connected()) return;

  if (publishAlertPayload(pendingAlertType, pendingAlertUptimeMs)) {
    Serial.printf("[ALERT] Flushed queued alert: %s\n", pendingAlertType);
    pendingAlert = false;
  }
}

void processAlertTransitions(bool newImpact, unsigned long now) {
  updateMotionStatus();

  // Impact la event ngan, uu tien gui ngay.
  if (newImpact) {
    triggerAlert("IMPACT", now);
  } else if (previousMotionStatus != STATUS_DANGER && motionStatus == STATUS_DANGER) {
    triggerAlert(currentDangerReason(), now);
  }

  previousMotionStatus = motionStatus;
}

void accumulateCalibration(
  float rawRoll,
  float rawPitch,
  float accMagnitude,
  float rawGyroX,
  float rawGyroY,
  float rawGyroZ
) {
  sumRoll += rawRoll;
  sumPitch += rawPitch;
  sumAccelMagnitude += accMagnitude;
  sumGyroX += rawGyroX;
  sumGyroY += rawGyroY;
  sumGyroZ += rawGyroZ;
  calibrationCount++;

  if (calibrationCount >= CALIBRATION_SAMPLES) {
    baselineRollDeg = static_cast<float>(sumRoll / calibrationCount);
    baselinePitchDeg = static_cast<float>(sumPitch / calibrationCount);
    baselineAccelMagnitude = static_cast<float>(sumAccelMagnitude / calibrationCount);
    gyroBiasX = static_cast<float>(sumGyroX / calibrationCount);
    gyroBiasY = static_cast<float>(sumGyroY / calibrationCount);
    gyroBiasZ = static_cast<float>(sumGyroZ / calibrationCount);

    calibrationReady = true;
    clearVibrationWindow();
    motionStatus = STATUS_NORMAL;
    previousMotionStatus = STATUS_NORMAL;

    Serial.printf(
      "[MPU] Calibration complete. baseline roll=%.2f, pitch=%.2f, |A|=%.3f\n",
      baselineRollDeg,
      baselinePitchDeg,
      baselineAccelMagnitude
    );
  }
}

void readMotionSample() {
  if (!mpuReady) return;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  accelX = a.acceleration.x;
  accelY = a.acceleration.y;
  accelZ = a.acceleration.z;

  accelerationMagnitude = sqrtf(
    accelX * accelX +
    accelY * accelY +
    accelZ * accelZ
  );

  float rawRoll = atan2f(accelY, accelZ) * 180.0f / PI;
  float rawPitch = atan2f(
    -accelX,
    sqrtf(accelY * accelY + accelZ * accelZ)
  ) * 180.0f / PI;

  float rawGyroX = g.gyro.x;
  float rawGyroY = g.gyro.y;
  float rawGyroZ = g.gyro.z;

  if (!calibrationReady) {
    rollDeg = rawRoll;
    pitchDeg = rawPitch;
    gyroX = rawGyroX;
    gyroY = rawGyroY;
    gyroZ = rawGyroZ;

    accumulateCalibration(
      rawRoll,
      rawPitch,
      accelerationMagnitude,
      rawGyroX,
      rawGyroY,
      rawGyroZ
    );
    updateMotionStatus();
    return;
  }

  rollDeg = rawRoll;
  pitchDeg = rawPitch;

  // Gyro output duoc tru bias sau calibration.
  gyroX = rawGyroX - gyroBiasX;
  gyroY = rawGyroY - gyroBiasY;
  gyroZ = rawGyroZ - gyroBiasZ;

  float deltaRoll = fabsf(angleDifference(rollDeg, baselineRollDeg));
  float deltaPitch = fabsf(angleDifference(pitchDeg, baselinePitchDeg));
  tiltAngleDeg = (deltaRoll > deltaPitch) ? deltaRoll : deltaPitch;

  float dynamicAcceleration = accelerationMagnitude - baselineAccelMagnitude;
  impactDelta = fabsf(dynamicAcceleration);

  updateVibration(dynamicAcceleration);

  tiltSeverity = updateSeverity(
    tiltSeverity,
    tiltAngleDeg,
    TILT_WARNING_ENTER_DEG,
    TILT_WARNING_EXIT_DEG,
    TILT_DANGER_ENTER_DEG,
    TILT_DANGER_EXIT_DEG
  );

  vibrationSeverity = updateSeverity(
    vibrationSeverity,
    vibrationRms,
    VIB_WARNING_ENTER,
    VIB_WARNING_EXIT,
    VIB_DANGER_ENTER,
    VIB_DANGER_EXIT
  );

  bool newImpact = false;
  unsigned long now = millis();
  if (impactDelta >= IMPACT_DELTA_THRESHOLD) {
    impactLatched = true;
    impactDetectedAtMs = now;
    newImpact = true;
  }

  processAlertTransitions(newImpact, now);
}

// ============================================================
// 10. TELEMETRY
// ============================================================
void publishTelemetry() {
  if (!mqttClient.connected()) return;

  // Refresh impact latch before serializing.
  bool impact = impactDanger();
  updateMotionStatus();

  JsonDocument doc;
  doc["deviceId"] = DEVICE_ID;
  doc["uptimeMs"] = millis();

  char isoTime[25];
  if (getUtcIsoTime(isoTime, sizeof(isoTime))) doc["timestamp"] = isoTime;
  else doc["timestamp"] = nullptr;

  doc["mpuReady"] = mpuReady;
  doc["calibrationReady"] = calibrationReady;

  doc["accelX"] = accelX;
  doc["accelY"] = accelY;
  doc["accelZ"] = accelZ;
  doc["accelMagnitude"] = accelerationMagnitude;

  doc["gyroX"] = gyroX;
  doc["gyroY"] = gyroY;
  doc["gyroZ"] = gyroZ;

  doc["rollDeg"] = rollDeg;
  doc["pitchDeg"] = pitchDeg;
  doc["tiltAngleDeg"] = tiltAngleDeg;
  doc["vibrationRms"] = vibrationRms;
  doc["impactDelta"] = impactDelta;

  doc["tiltState"] = severityToString(tiltSeverity);
  doc["vibrationState"] = severityToString(vibrationSeverity);
  doc["tiltDanger"] = tiltDanger();
  doc["vibrationDanger"] = vibrationDanger();
  doc["impactDanger"] = impact;
  doc["status"] = motionStatusToString(motionStatus);

  doc["baselineRollDeg"] = baselineRollDeg;
  doc["baselinePitchDeg"] = baselinePitchDeg;
  doc["baselineAccelMagnitude"] = baselineAccelMagnitude;

  // Khong fake battery percentage. XIAO ESP32-C3 can them mach chia ap -> ADC neu muon do pin that.
  doc["batterySupported"] = false;
  doc["batteryVoltage"] = nullptr;
  doc["batteryPercent"] = nullptr;

  doc["pendingAlert"] = pendingAlert;
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

  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
  Wire.setClock(400000);

  buildMqttClientId();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(1536);
  mqttClient.setKeepAlive(20);
  mqttClient.setSocketTimeout(2);

  // Thu init mot lan, sau do neu fail se retry non-blocking trong loop.
  lastMpuRetryMs = millis() - MPU_RETRY_INTERVAL_MS;
  tryInitMpu();

  startWiFi();

  Serial.println("[F7] XIAO ESP32-C3 motion node started.");
  Serial.println("[F7] Keep the node still during the initial ~2 s calibration.");
}

void loop() {
  unsigned long now = millis();

  maintainWiFi();
  maintainMQTT();
  maintainMpu();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    // Cong don de giam drift lich sample neu loop co tre ngan.
    lastSampleMs = now;
    readMotionSample();
  }

  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    publishTelemetry();
  }
}
