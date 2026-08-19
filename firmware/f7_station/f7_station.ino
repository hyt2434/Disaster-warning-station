/*
 * ============================================================
 * XIAO ESP32-C3 F7 + MPU6050
 * ============================================================
 *
 * NORMAL PATH:
 *   F7 -> Home WiFi -> MQTT -> Main / Backend / Web
 *
 * LOCAL FALLBACK PATH:
 *   F7 creates WiFi AP -> Main connects to F7 AP -> UDP -> Main
 *
 * The F7 access point is always available. This makes the fallback
 * easy to test and does not interrupt the normal MQTT connection.
 *
 * TEST WITHOUT TURNING OFF THE ROUTER:
 *   1. Set LOCAL_TEST_MODE = true in this file and Main firmware.
 *   2. Upload this F7 firmware first.
 *   3. Upload Main firmware.
 *   4. Open both Serial Monitors at 115200 baud.
 *
 * MPU6050 DATA:
 *   - Tilt: change of roll or pitch compared with startup position.
 *   - Vibration: average acceleration change in one reading group.
 *   - Impact: second-largest acceleration change in one reading group.
 *
 * SEND INTERVAL: 2 seconds.
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>

// ============================================================
// 1. NORMAL HOME WIFI + MQTT
// ============================================================

const char* HOME_WIFI_SSID = "YOUR_WIFI_SSID";
const char* HOME_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// IP address of the computer running Mosquitto.
const char* MQTT_HOST = "192.168.1.12";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

const char* DEVICE_ID = "f7-station-01";

const char* TOPIC_MOTION_STATE = "disaster/f7/state";
const char* TOPIC_TELEMETRY = "disaster/f7/telemetry";
const char* TOPIC_STATUS = "disaster/f7/status";

// ============================================================
// 2. LOCAL FALLBACK WIFI CREATED BY F7
// ============================================================

const char* F7_AP_SSID = "DISASTER_F7_DIRECT";
const char* F7_AP_PASSWORD = "12345678";

IPAddress F7_AP_IP(192, 168, 7, 1);
IPAddress F7_AP_GATEWAY(192, 168, 7, 1);
IPAddress F7_AP_SUBNET(255, 255, 255, 0);
IPAddress F7_AP_BROADCAST_IP(192, 168, 7, 255);

const int DIRECT_UDP_PORT = 4210;

// false: normal operation using Home WiFi and MQTT.
// true : skip Home WiFi and only test F7 AP -> UDP -> Main.
const bool LOCAL_TEST_MODE = false;

// ============================================================
// 3. MPU6050 PINS AND THRESHOLDS
// ============================================================

const int MPU_SDA_PIN = 6;
const int MPU_SCL_PIN = 7;

const int SAFE = 0;
const int WARNING = 1;
const int DANGER = 2;

// A small tilt can appear because of sensor noise or a slightly uneven table.
// These demo thresholds avoid warning for small changes around the start position.
const float TILT_WARNING_DEGREES = 15.0;
const float TILT_DANGER_DEGREES = 30.0;

const float VIBRATION_WARNING = 0.80;
const float VIBRATION_DANGER = 2.00;

const float IMPACT_DANGER = 8.0;

// Read several samples so that one noisy value cannot decide the whole status.
const int MOTION_SAMPLE_COUNT = 20;
const int MINIMUM_VALID_SAMPLES = 10;

// Values outside this range are treated as a disconnected sensor or I2C noise.
const float MINIMUM_VALID_ACCELERATION = 2.0;
const float MAXIMUM_VALID_ACCELERATION = 40.0;

// ============================================================
// 4. TIMING
// ============================================================

const unsigned long PUBLISH_INTERVAL_MS = 2000;
const unsigned long MQTT_RETRY_INTERVAL_MS = 5000;
const unsigned long NETWORK_LOG_INTERVAL_MS = 5000;

// ============================================================
// 5. OBJECTS AND CURRENT VALUES
// ============================================================

Adafruit_MPU6050 mpu;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiUDP udp;

float baselineRoll = 0.0;
float baselinePitch = 0.0;
float roll = 0.0;
float pitch = 0.0;
float tiltAngle = 0.0;
float vibrationValue = 0.0;
float impactDelta = 0.0;

int tiltLevel = SAFE;
int vibrationLevel = SAFE;
int impactLevel = SAFE;
int motionLevel = SAFE;

bool mpuReady = false;
bool motionSensorValid = false;
bool calibrationSuccessful = false;

unsigned long lastPublishTime = 0;
unsigned long lastMQTTRetry = 0;
unsigned long lastNetworkLog = 0;

bool homeWiFiWasConnected = false;
bool mainWasConnectedToF7 = false;

// ============================================================
// 6. SIMPLE HELPERS
// ============================================================

const char* levelToText(int level)
{
  if (level == DANGER)
  {
    return "DANGER";
  }

  if (level == WARNING)
  {
    return "WARNING";
  }

  return "SAFE";
}

float calculateRoll(float accelerationX, float accelerationY, float accelerationZ)
{
  return atan2(accelerationY, accelerationZ) * 180.0 / PI;
}

float calculatePitch(float accelerationX, float accelerationY, float accelerationZ)
{
  float bottom = sqrt(
    accelerationY * accelerationY +
    accelerationZ * accelerationZ
  );

  return atan2(-accelerationX, bottom) * 180.0 / PI;
}

float calculateAccelerationMagnitude(
  float accelerationX,
  float accelerationY,
  float accelerationZ
)
{
  return sqrt(
    accelerationX * accelerationX +
    accelerationY * accelerationY +
    accelerationZ * accelerationZ
  );
}

bool isValidAcceleration(float accelerationMagnitude)
{
  return isfinite(accelerationMagnitude) &&
         accelerationMagnitude >= MINIMUM_VALID_ACCELERATION &&
         accelerationMagnitude <= MAXIMUM_VALID_ACCELERATION;
}

float calculateAngleDifference(float currentAngle, float baselineAngle)
{
  float difference = fabs(currentAngle - baselineAngle);

  // Roll can jump from +179 degrees to -179 degrees although the real
  // physical change is only 2 degrees.
  if (difference > 180.0)
  {
    difference = 360.0 - difference;
  }

  return difference;
}

// ============================================================
// 7. CALIBRATE MPU6050
// ============================================================

void calibrateMPU()
{
  const int SAMPLE_COUNT = 100;
  const int MAXIMUM_ATTEMPTS = 200;

  float totalRoll = 0.0;
  float totalPitch = 0.0;
  int validSampleCount = 0;
  int attemptCount = 0;

  Serial.println();
  Serial.println("[MPU] Calibration starts. Keep F7 still for 2 seconds.");

  while (validSampleCount < SAMPLE_COUNT && attemptCount < MAXIMUM_ATTEMPTS)
  {
    attemptCount++;

    sensors_event_t acceleration;
    sensors_event_t gyro;
    sensors_event_t sensorTemperature;

    mpu.getEvent(&acceleration, &gyro, &sensorTemperature);

    float accelerationX = acceleration.acceleration.x;
    float accelerationY = acceleration.acceleration.y;
    float accelerationZ = acceleration.acceleration.z;

    float accelerationMagnitude = calculateAccelerationMagnitude(
      accelerationX,
      accelerationY,
      accelerationZ
    );

    if (isValidAcceleration(accelerationMagnitude))
    {
      totalRoll += calculateRoll(accelerationX, accelerationY, accelerationZ);
      totalPitch += calculatePitch(accelerationX, accelerationY, accelerationZ);
      validSampleCount++;
    }

    delay(20);
  }

  if (validSampleCount < MINIMUM_VALID_SAMPLES)
  {
    Serial.println("[MPU] Calibration failed: too many invalid samples");
    Serial.println("[MPU] Check 3.3V, GND, SDA and SCL wiring");
    return;
  }

  baselineRoll = totalRoll / validSampleCount;
  baselinePitch = totalPitch / validSampleCount;
  calibrationSuccessful = true;

  Serial.println("[MPU] Calibration completed");
  Serial.print("[MPU] Baseline roll: ");
  Serial.println(baselineRoll);
  Serial.print("[MPU] Baseline pitch: ");
  Serial.println(baselinePitch);
}

// ============================================================
// 8. READ AND CLASSIFY MOTION
// ============================================================

void readMotionSensor()
{
  float totalRoll = 0.0;
  float totalPitch = 0.0;
  float totalAcceleration = 0.0;
  float accelerationSamples[MOTION_SAMPLE_COUNT];

  int validSampleCount = 0;
  int attemptCount = 0;
  const int MAXIMUM_ATTEMPTS = MOTION_SAMPLE_COUNT * 2;

  while (
    validSampleCount < MOTION_SAMPLE_COUNT &&
    attemptCount < MAXIMUM_ATTEMPTS
  )
  {
    attemptCount++;

    sensors_event_t acceleration;
    sensors_event_t gyro;
    sensors_event_t sensorTemperature;

    mpu.getEvent(&acceleration, &gyro, &sensorTemperature);

    float accelerationX = acceleration.acceleration.x;
    float accelerationY = acceleration.acceleration.y;
    float accelerationZ = acceleration.acceleration.z;

    float currentAcceleration = calculateAccelerationMagnitude(
      accelerationX,
      accelerationY,
      accelerationZ
    );

    if (isValidAcceleration(currentAcceleration))
    {
      totalRoll += calculateRoll(accelerationX, accelerationY, accelerationZ);
      totalPitch += calculatePitch(accelerationX, accelerationY, accelerationZ);
      totalAcceleration += currentAcceleration;
      accelerationSamples[validSampleCount] = currentAcceleration;
      validSampleCount++;
    }

    delay(10);
  }

  if (validSampleCount < MINIMUM_VALID_SAMPLES)
  {
    motionSensorValid = false;
    vibrationValue = 0.0;
    impactDelta = 0.0;
    Serial.println("[MPU] Reading ignored: too many invalid acceleration samples");
    return;
  }

  motionSensorValid = calibrationSuccessful;
  roll = totalRoll / validSampleCount;
  pitch = totalPitch / validSampleCount;

  float averageAcceleration = totalAcceleration / validSampleCount;
  float totalAccelerationChange = 0.0;
  float largestChange = 0.0;
  float secondLargestChange = 0.0;

  for (int sample = 0; sample < validSampleCount; sample++)
  {
    float accelerationChange = fabs(
      accelerationSamples[sample] - averageAcceleration
    );

    totalAccelerationChange += accelerationChange;

    if (accelerationChange > largestChange)
    {
      secondLargestChange = largestChange;
      largestChange = accelerationChange;
    }
    else if (accelerationChange > secondLargestChange)
    {
      secondLargestChange = accelerationChange;
    }
  }

  vibrationValue = totalAccelerationChange / validSampleCount;

  // Use the second-largest change. One isolated noisy sample is ignored,
  // while a real impact normally affects at least two consecutive samples.
  impactDelta = secondLargestChange;

  float rollDifference = calculateAngleDifference(roll, baselineRoll);
  float pitchDifference = calculateAngleDifference(pitch, baselinePitch);

  tiltAngle = max(rollDifference, pitchDifference);
}

int getTiltLevel()
{
  if (tiltAngle >= TILT_DANGER_DEGREES)
  {
    return DANGER;
  }

  if (tiltAngle >= TILT_WARNING_DEGREES)
  {
    return WARNING;
  }

  return SAFE;
}

int getVibrationLevel()
{
  if (vibrationValue >= VIBRATION_DANGER)
  {
    return DANGER;
  }

  if (vibrationValue >= VIBRATION_WARNING)
  {
    return WARNING;
  }

  return SAFE;
}

int getImpactLevel()
{
  if (impactDelta >= IMPACT_DANGER)
  {
    return DANGER;
  }

  return SAFE;
}

void updateMotionLevel()
{
  if (!motionSensorValid)
  {
    tiltLevel = SAFE;
    vibrationLevel = SAFE;
    impactLevel = SAFE;
    motionLevel = SAFE;
    return;
  }

  tiltLevel = getTiltLevel();
  vibrationLevel = getVibrationLevel();
  impactLevel = getImpactLevel();

  motionLevel = tiltLevel;

  if (vibrationLevel > motionLevel)
  {
    motionLevel = vibrationLevel;
  }

  if (impactLevel > motionLevel)
  {
    motionLevel = impactLevel;
  }
}

// ============================================================
// 9. START WIFI
// ============================================================

void startWiFi()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFi.softAPConfig(F7_AP_IP, F7_AP_GATEWAY, F7_AP_SUBNET);

  bool accessPointStarted = WiFi.softAP(F7_AP_SSID, F7_AP_PASSWORD);

  if (accessPointStarted)
  {
    Serial.println("[DIRECT] F7 fallback WiFi started");
    Serial.print("[DIRECT] SSID: ");
    Serial.println(F7_AP_SSID);
    Serial.print("[DIRECT] IP: ");
    Serial.println(WiFi.softAPIP());
  }
  else
  {
    Serial.println("[DIRECT] Failed to start F7 fallback WiFi");
  }

  if (LOCAL_TEST_MODE)
  {
    Serial.println("[TEST] LOCAL_TEST_MODE is ON. Home WiFi and MQTT are skipped.");
    return;
  }

  Serial.print("[WiFi] Connecting to HOME: ");
  Serial.println(HOME_WIFI_SSID);
  WiFi.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
}

void maintainWiFi()
{
  bool homeConnected = WiFi.status() == WL_CONNECTED;
  bool mainConnected = WiFi.softAPgetStationNum() > 0;

  if (homeConnected && !homeWiFiWasConnected)
  {
    homeWiFiWasConnected = true;
    Serial.print("[WiFi] HOME connected. IP: ");
    Serial.println(WiFi.localIP());
  }

  if (!homeConnected && homeWiFiWasConnected)
  {
    homeWiFiWasConnected = false;
    mqttClient.disconnect();
    Serial.println("[WiFi] HOME lost. F7 fallback WiFi is still available.");
  }

  if (mainConnected != mainWasConnectedToF7)
  {
    mainWasConnectedToF7 = mainConnected;
    Serial.print("[DIRECT] Main connected to F7 WiFi: ");
    Serial.println(mainConnected ? "YES" : "NO");
  }

  if (!homeConnected && millis() - lastNetworkLog >= NETWORK_LOG_INTERVAL_MS)
  {
    lastNetworkLog = millis();
    Serial.println("[WiFi] HOME unavailable. Waiting for Main on F7 fallback WiFi.");
  }
}

// ============================================================
// 10. MQTT NORMAL PATH
// ============================================================

void maintainMQTT()
{
  if (LOCAL_TEST_MODE || WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (mqttClient.connected())
  {
    return;
  }

  if (millis() - lastMQTTRetry < MQTT_RETRY_INTERVAL_MS)
  {
    return;
  }

  lastMQTTRetry = millis();

  char clientId[48];
  snprintf(
    clientId,
    sizeof(clientId),
    "%s-%04X",
    DEVICE_ID,
    (uint16_t)(ESP.getEfuseMac() & 0xFFFF)
  );

  bool connected;

  if (strlen(MQTT_USER) > 0)
  {
    connected = mqttClient.connect(
      clientId,
      MQTT_USER,
      MQTT_PASSWORD,
      TOPIC_STATUS,
      1,
      true,
      "offline"
    );
  }
  else
  {
    connected = mqttClient.connect(
      clientId,
      TOPIC_STATUS,
      1,
      true,
      "offline"
    );
  }

  if (connected)
  {
    mqttClient.publish(TOPIC_STATUS, "online", true);
    Serial.println("[MQTT] Connected");
  }
  else
  {
    Serial.print("[MQTT] Connection failed. State: ");
    Serial.println(mqttClient.state());
  }
}

void publishMQTTData()
{
  if (!mqttClient.connected())
  {
    return;
  }

  mqttClient.publish(
    TOPIC_MOTION_STATE,
    levelToText(motionLevel),
    true
  );

  String telemetryJson = "{";

  telemetryJson += "\"deviceId\":\"";
  telemetryJson += DEVICE_ID;
  telemetryJson += "\",";

  telemetryJson += "\"roll\":";
  telemetryJson += String(roll, 1);
  telemetryJson += ",";

  telemetryJson += "\"pitch\":";
  telemetryJson += String(pitch, 1);
  telemetryJson += ",";

  telemetryJson += "\"tilt\":";
  telemetryJson += String(tiltAngle, 1);
  telemetryJson += ",";

  telemetryJson += "\"vibration\":";
  telemetryJson += String(vibrationValue, 2);
  telemetryJson += ",";

  telemetryJson += "\"impact\":";
  telemetryJson += String(impactDelta, 2);
  telemetryJson += ",";

  telemetryJson += "\"status\":\"";
  telemetryJson += levelToText(motionLevel);
  telemetryJson += "\"";

  telemetryJson += "}";

  mqttClient.publish(TOPIC_TELEMETRY, telemetryJson.c_str());

  Serial.print("[MQTT] Published: ");
  Serial.println(telemetryJson);
}

// ============================================================
// 11. UDP LOCAL FALLBACK PATH
// ============================================================

void sendDirectDataToMain()
{
  if (WiFi.softAPgetStationNum() == 0)
  {
    return;
  }

  /*
   * Packet format:
   * STATUS,ROLL,PITCH,TILT,VIBRATION,IMPACT
   *
   * Example:
   * WARNING,1.2,2.5,12.4,1.35,2.10
   */
  String packet = "";

  packet += levelToText(motionLevel);
  packet += ",";
  packet += String(roll, 1);
  packet += ",";
  packet += String(pitch, 1);
  packet += ",";
  packet += String(tiltAngle, 1);
  packet += ",";
  packet += String(vibrationValue, 2);
  packet += ",";
  packet += String(impactDelta, 2);

  udp.beginPacket(F7_AP_BROADCAST_IP, DIRECT_UDP_PORT);
  udp.print(packet);
  udp.endPacket();

  Serial.print("[DIRECT] Sent to Main: ");
  Serial.println(packet);
}

// ============================================================
// 12. SERIAL MONITOR
// ============================================================

void printMotionData()
{
  Serial.println();
  Serial.println("================================");

  Serial.print("Roll        : ");
  Serial.print(roll);
  Serial.println(" deg");

  Serial.print("Pitch       : ");
  Serial.print(pitch);
  Serial.println(" deg");

  Serial.print("Tilt change : ");
  Serial.print(tiltAngle);
  Serial.print(" deg -> ");
  Serial.println(levelToText(tiltLevel));

  Serial.print("Vibration   : ");
  Serial.print(vibrationValue);
  Serial.print(" m/s2 -> ");
  Serial.println(levelToText(vibrationLevel));

  Serial.print("Impact      : ");
  Serial.print(impactDelta);
  Serial.print(" m/s2 -> ");
  Serial.println(levelToText(impactLevel));

  Serial.print("MOTION      : ");
  Serial.println(levelToText(motionLevel));

  Serial.print("MPU DATA    : ");
  Serial.println(motionSensorValid ? "VALID" : "INVALID");

  Serial.print("HOME MQTT   : ");
  Serial.println(mqttClient.connected() ? "CONNECTED" : "DISCONNECTED");

  Serial.print("MAIN DIRECT : ");
  Serial.println(WiFi.softAPgetStationNum() > 0 ? "CONNECTED" : "NOT CONNECTED");

  Serial.println("================================");
}

// ============================================================
// 13. SETUP AND LOOP
// ============================================================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);

  Serial.println("[MPU] Looking for MPU6050...");

  mpuReady = mpu.begin();

  if (mpuReady)
  {
    Serial.println("[MPU] Found");

    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    calibrateMPU();
  }
  else
  {
    Serial.println("[MPU] Not found. Network remains available; check SDA, SCL and power.");
  }

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setSocketTimeout(1);

  startWiFi();

  Serial.println();
  Serial.println("[F7] Motion station started");
  Serial.println("[F7] Publish interval: 2 seconds");
}

void loop()
{
  maintainWiFi();
  maintainMQTT();

  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  unsigned long currentTime = millis();

  if (currentTime - lastPublishTime >= PUBLISH_INTERVAL_MS)
  {
    lastPublishTime = currentTime;

    if (mpuReady)
    {
      readMotionSensor();
      updateMotionLevel();
    }

    if (motionSensorValid)
    {
      // Normal path for Main, backend and web.
      publishMQTTData();

      // Local safety path when Main is connected to the F7 access point.
      sendDirectDataToMain();
    }

    printMotionData();
  }

  delay(10);
}
