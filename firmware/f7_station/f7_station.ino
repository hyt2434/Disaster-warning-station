/*
 * ============================================================
 * XIAO ESP32-C3 + MPU6050
 * SIMPLE DEMO + DIRECT FALLBACK TO ESP32-S3
 * ============================================================
 *
 * NORMAL MODE:
 *   C3 -> Home WiFi -> MQTT -> S3 / Web
 *
 * WHEN HOME WIFI IS LOST:
 *   C3 -> WiFi AP created by S3 -> UDP -> S3
 *
 * No ESP-NOW.
 * No wire between C3 and S3.
 *
 * IMPORTANT:
 *   After C3 enters DIRECT mode, it stays there until reboot.
 *   This keeps the demo logic simple and stable.
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
// 1. HOME WIFI + MQTT
// ============================================================

const char* HOME_WIFI_SSID = "YOUR_WIFI_SSID";
const char* HOME_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "192.168.1.100";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

const char* DEVICE_ID = "f7-station-01";

const char* TOPIC_MOTION_STATE = "disaster/f7/state";
const char* TOPIC_TELEMETRY = "disaster/f7/telemetry";
const char* TOPIC_STATUS = "disaster/f7/status";

// ============================================================
// 2. DIRECT FALLBACK WIFI CREATED BY S3
// ============================================================

const char* MAIN_AP_SSID = "DISASTER_MAIN_DIRECT";
const char* MAIN_AP_PASSWORD = "12345678";

// S3 AP uses this fixed IP.
IPAddress MAIN_AP_IP(192, 168, 4, 1);

const int DIRECT_UDP_PORT = 4210;

// If home WiFi is unavailable for 5 seconds,
// switch to S3's direct WiFi.
const unsigned long HOME_WIFI_FAILOVER_MS = 5000;

// If WiFi is connected but the MQTT broker is unavailable,
// use the same direct safety path after 10 seconds.
const unsigned long MQTT_FAILOVER_MS = 10000;

// ============================================================
// 3. MPU6050 PINS
// ============================================================

const int MPU_SDA_PIN = 6;
const int MPU_SCL_PIN = 7;

// ============================================================
// 4. LEVELS
// ============================================================

const int NORMAL = 0;
const int WARNING = 1;
const int DANGER = 2;

// ============================================================
// 5. DEMO THRESHOLDS
// ============================================================

const float TILT_WARNING = 10.0;
const float TILT_DANGER = 20.0;

const float VIBRATION_WARNING = 1.20;
const float VIBRATION_DANGER = 2.50;

const float IMPACT_DANGER = 10.0;

// ============================================================
// 6. OBJECTS
// ============================================================

Adafruit_MPU6050 mpu;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

WiFiUDP udp;

// ============================================================
// 7. CALIBRATION BASELINE
// ============================================================

float baselineRoll = 0;
float baselinePitch = 0;
float baselineAcceleration = 9.8;

// ============================================================
// 8. CURRENT SENSOR VALUES
// ============================================================

float roll = 0;
float pitch = 0;

float tiltAngle = 0;
float vibrationValue = 0;
float impactDelta = 0;
float accelerationMagnitude = 0;

// ============================================================
// 9. CURRENT LEVELS
// ============================================================

int tiltLevel = NORMAL;
int vibrationLevel = NORMAL;
int impactLevel = NORMAL;

int motionLevel = NORMAL;
int previousMotionLevel = NORMAL;

// ============================================================
// 10. NETWORK STATE
// ============================================================

// false = C3 is trying/using home WiFi.
// true  = C3 is using S3 direct AP.
bool directMode = false;

unsigned long homeWiFiLostSince = 0;
unsigned long lastDirectRetry = 0;
unsigned long lastMQTTRetry = 0;
unsigned long mqttUnavailableSince = 0;

bool homeWiFiWasConnected = false;
bool directWiFiWasConnected = false;

// ============================================================
// 11. SIMPLE HELPERS
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

  return "NORMAL";
}

float calculateRoll(float ax, float ay, float az)
{
  return atan2(ay, az) * 180.0 / PI;
}

float calculatePitch(float ax, float ay, float az)
{
  float bottom = sqrt(ay * ay + az * az);

  return atan2(-ax, bottom) * 180.0 / PI;
}

float calculateAccelerationMagnitude(float ax, float ay, float az)
{
  return sqrt(
    ax * ax +
    ay * ay +
    az * az
  );
}

// ============================================================
// 12. CALIBRATION
// ============================================================

void calibrateMPU()
{
  Serial.println();
  Serial.println("================================");
  Serial.println("[MPU] CALIBRATION START");
  Serial.println("[MPU] Keep the sensor still for about 2 seconds.");

  const int SAMPLE_COUNT = 100;

  float totalRoll = 0;
  float totalPitch = 0;
  float totalAcceleration = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    sensors_event_t acceleration;
    sensors_event_t gyro;
    sensors_event_t temperature;

    mpu.getEvent(
      &acceleration,
      &gyro,
      &temperature
    );

    float ax = acceleration.acceleration.x;
    float ay = acceleration.acceleration.y;
    float az = acceleration.acceleration.z;

    totalRoll += calculateRoll(ax, ay, az);
    totalPitch += calculatePitch(ax, ay, az);
    totalAcceleration +=
      calculateAccelerationMagnitude(ax, ay, az);

    delay(20);
  }

  baselineRoll =
    totalRoll / SAMPLE_COUNT;

  baselinePitch =
    totalPitch / SAMPLE_COUNT;

  baselineAcceleration =
    totalAcceleration / SAMPLE_COUNT;

  Serial.println("[MPU] CALIBRATION COMPLETE");

  Serial.print("Baseline Roll         : ");
  Serial.println(baselineRoll);

  Serial.print("Baseline Pitch        : ");
  Serial.println(baselinePitch);

  Serial.print("Baseline Acceleration : ");
  Serial.println(baselineAcceleration);

  Serial.println("================================");
}

// ============================================================
// 13. READ MOTION SENSOR
// ============================================================

void readMotionSensor()
{
  /*
   * Read 5 samples.
   *
   * VIBRATION:
   *   average change from baseline.
   *
   * IMPACT:
   *   biggest change from baseline.
   */

  const int SAMPLE_COUNT = 5;

  float totalRoll = 0;
  float totalPitch = 0;
  float totalAcceleration = 0;
  float totalVibration = 0;

  float biggestImpact = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    sensors_event_t acceleration;
    sensors_event_t gyro;
    sensors_event_t temperature;

    mpu.getEvent(
      &acceleration,
      &gyro,
      &temperature
    );

    float ax = acceleration.acceleration.x;
    float ay = acceleration.acceleration.y;
    float az = acceleration.acceleration.z;

    float currentRoll =
      calculateRoll(ax, ay, az);

    float currentPitch =
      calculatePitch(ax, ay, az);

    float currentAcceleration =
      calculateAccelerationMagnitude(
        ax,
        ay,
        az
      );

    float difference =
      fabs(
        currentAcceleration -
        baselineAcceleration
      );

    totalRoll += currentRoll;
    totalPitch += currentPitch;
    totalAcceleration += currentAcceleration;
    totalVibration += difference;

    if (difference > biggestImpact)
    {
      biggestImpact = difference;
    }

    delay(20);
  }

  roll =
    totalRoll / SAMPLE_COUNT;

  pitch =
    totalPitch / SAMPLE_COUNT;

  accelerationMagnitude =
    totalAcceleration / SAMPLE_COUNT;

  vibrationValue =
    totalVibration / SAMPLE_COUNT;

  impactDelta =
    biggestImpact;

  float rollDifference =
    fabs(roll - baselineRoll);

  float pitchDifference =
    fabs(pitch - baselinePitch);

  if (rollDifference > pitchDifference)
  {
    tiltAngle = rollDifference;
  }
  else
  {
    tiltAngle = pitchDifference;
  }
}

// ============================================================
// 14. CLASSIFY MOTION
// ============================================================

int checkTiltLevel()
{
  if (tiltAngle >= TILT_DANGER)
  {
    return DANGER;
  }

  if (tiltAngle >= TILT_WARNING)
  {
    return WARNING;
  }

  return NORMAL;
}

int checkVibrationLevel()
{
  if (vibrationValue >= VIBRATION_DANGER)
  {
    return DANGER;
  }

  if (vibrationValue >= VIBRATION_WARNING)
  {
    return WARNING;
  }

  return NORMAL;
}

int checkImpactLevel()
{
  if (impactDelta >= IMPACT_DANGER)
  {
    return DANGER;
  }

  return NORMAL;
}

void updateMotionLevel()
{
  tiltLevel =
    checkTiltLevel();

  vibrationLevel =
    checkVibrationLevel();

  impactLevel =
    checkImpactLevel();

  // Take the highest level.
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
// 15. START HOME WIFI
// ============================================================

void startHomeWiFi()
{
  directMode = false;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  WiFi.begin(
    HOME_WIFI_SSID,
    HOME_WIFI_PASSWORD
  );

  homeWiFiLostSince =
    millis();

  Serial.print("[WiFi] Connecting to HOME WiFi: ");
  Serial.println(HOME_WIFI_SSID);
}

// ============================================================
// 16. SWITCH TO S3 DIRECT WIFI
// ============================================================

void switchToDirectMode()
{
  Serial.println();
  Serial.println("[WiFi] Home WiFi unavailable.");
  Serial.println("[WiFi] SWITCH TO DIRECT MODE.");
  Serial.print("[WiFi] Connecting directly to S3 AP: ");
  Serial.println(MAIN_AP_SSID);

  directMode = true;

  // Announce the mode change before stopping MQTT.
  if (mqttClient.connected())
  {
    mqttClient.publish(
      TOPIC_STATUS,
      "direct",
      true
    );
  }

  // Stop old MQTT connection.
  mqttClient.disconnect();

  // Clear old WiFi connection.
  WiFi.disconnect();

  delay(100);

  WiFi.begin(
    MAIN_AP_SSID,
    MAIN_AP_PASSWORD
  );

  lastDirectRetry =
    millis();
}

// ============================================================
// 17. MAINTAIN WIFI
// ============================================================

void maintainWiFi()
{
  unsigned long now =
    millis();

  // --------------------------------------------------------
  // HOME MODE
  // --------------------------------------------------------

  if (!directMode)
  {
    // Home WiFi is OK.
    if (WiFi.status() == WL_CONNECTED)
    {
      if (!homeWiFiWasConnected)
      {
        homeWiFiWasConnected = true;

        Serial.print("[WiFi] HOME connected. IP: ");
        Serial.println(WiFi.localIP());

        Serial.print("[WiFi] RSSI: ");
        Serial.println(WiFi.RSSI());

        Serial.print("[MQTT] Broker: ");
        Serial.print(MQTT_HOST);
        Serial.print(":");
        Serial.println(MQTT_PORT);
      }

      homeWiFiLostSince = 0;

      return;
    }

    // Start measuring how long WiFi has been unavailable.
    if (homeWiFiLostSince == 0)
    {
      homeWiFiLostSince = now;
    }

    // Wait 5 seconds before fallback.
    if (
      now - homeWiFiLostSince >=
      HOME_WIFI_FAILOVER_MS
    )
    {
      switchToDirectMode();
    }

    return;
  }

  // --------------------------------------------------------
  // DIRECT MODE
  // --------------------------------------------------------

  // Connected to S3 AP.
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!directWiFiWasConnected)
    {
      directWiFiWasConnected = true;

      Serial.print("[DIRECT] Connected. IP: ");
      Serial.println(WiFi.localIP());
    }

    return;
  }

  directWiFiWasConnected = false;

  // Retry S3 AP every 5 seconds.
  if (
    now - lastDirectRetry <
    5000
  )
  {
    return;
  }

  lastDirectRetry = now;

  Serial.println(
    "[DIRECT] Retry S3 AP..."
  );

  WiFi.disconnect();

  delay(50);

  WiFi.begin(
    MAIN_AP_SSID,
    MAIN_AP_PASSWORD
  );
}

// ============================================================
// 18. MQTT FOR NORMAL MODE
// ============================================================

void maintainMQTT()
{
  // MQTT is used only in HOME mode.
  if (directMode)
  {
    return;
  }

  if (
    WiFi.status() !=
    WL_CONNECTED
  )
  {
    return;
  }

  if (mqttClient.connected())
  {
    mqttUnavailableSince = 0;
    return;
  }

  unsigned long now =
    millis();

  if (mqttUnavailableSince == 0)
  {
    mqttUnavailableSince = now;
  }

  if (
    now - mqttUnavailableSince >=
    MQTT_FAILOVER_MS
  )
  {
    Serial.println(
      "[MQTT] Broker unavailable. Using direct safety path."
    );

    switchToDirectMode();
    return;
  }

  if (
    now - lastMQTTRetry <
    5000
  )
  {
    return;
  }

  lastMQTTRetry = now;

  Serial.println(
    "[MQTT] Connecting..."
  );

  char clientId[48];

  snprintf(
    clientId,
    sizeof(clientId),
    "%s-%04X",
    DEVICE_ID,
    (uint16_t)(ESP.getEfuseMac() & 0xFFFF)
  );

  bool success;

  if (strlen(MQTT_USER) > 0)
  {
    success = mqttClient.connect(
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
    success = mqttClient.connect(
      clientId,
      TOPIC_STATUS,
      1,
      true,
      "offline"
    );
  }

  if (success)
  {
    mqttUnavailableSince = 0;

    Serial.println(
      "[MQTT] Connected"
    );

    mqttClient.publish(
      TOPIC_STATUS,
      "online",
      true
    );
  }
  else
  {
    Serial.println(
      "[MQTT] Connection failed"
    );
  }
}

// ============================================================
// 19. SEND DATA THROUGH MQTT
// ============================================================

void publishMQTTData()
{
  if (
    directMode ||
    !mqttClient.connected()
  )
  {
    return;
  }

  // --------------------------------------------------------
  // Simple state topic.
  // S3 subscribes to this topic.
  // --------------------------------------------------------

  mqttClient.publish(
    TOPIC_MOTION_STATE,
    levelToText(motionLevel),
    true
  );

  // --------------------------------------------------------
  // Telemetry for web/dashboard.
  // --------------------------------------------------------

  String data = "{";

  data += "\"deviceId\":\"";
  data += DEVICE_ID;
  data += "\",";

  data += "\"roll\":";
  data += String(roll, 1);
  data += ",";

  data += "\"pitch\":";
  data += String(pitch, 1);
  data += ",";

  data += "\"tilt\":";
  data += String(tiltAngle, 1);
  data += ",";

  data += "\"vibration\":";
  data += String(vibrationValue, 2);
  data += ",";

  data += "\"impact\":";
  data += String(impactDelta, 2);
  data += ",";

  data += "\"status\":\"";
  data += levelToText(motionLevel);
  data += "\"";

  data += "}";

  mqttClient.publish(
    TOPIC_TELEMETRY,
    data.c_str()
  );
}

// ============================================================
// 20. SEND DATA DIRECTLY TO S3 WITH UDP
// ============================================================

void sendDirectToS3()
{
  if (!directMode)
  {
    return;
  }

  if (
    WiFi.status() !=
    WL_CONNECTED
  )
  {
    return;
  }

  /*
   * Packet format:
   *
   * STATUS,TILT,VIBRATION,IMPACT
   *
   * Example:
   *
   * DANGER,25.4,3.10,4.50
   */

  String packet = "";

  packet +=
    levelToText(motionLevel);

  packet += ",";

  packet +=
    String(tiltAngle, 1);

  packet += ",";

  packet +=
    String(vibrationValue, 2);

  packet += ",";

  packet +=
    String(impactDelta, 2);

  udp.beginPacket(
    MAIN_AP_IP,
    DIRECT_UDP_PORT
  );

  udp.print(packet);

  udp.endPacket();

  Serial.print(
    "[DIRECT] Sent to S3: "
  );

  Serial.println(packet);
}

// ============================================================
// 21. PRINT DATA
// ============================================================

void printMotionData()
{
  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.print("Tilt       : ");
  Serial.print(tiltAngle);
  Serial.print(" deg -> ");
  Serial.println(
    levelToText(tiltLevel)
  );

  Serial.print("Vibration  : ");
  Serial.print(vibrationValue);
  Serial.print(" m/s2 -> ");
  Serial.println(
    levelToText(vibrationLevel)
  );

  Serial.print("Impact     : ");
  Serial.print(impactDelta);
  Serial.print(" m/s2 -> ");
  Serial.println(
    levelToText(impactLevel)
  );

  Serial.print("MOTION     : ");
  Serial.println(
    levelToText(motionLevel)
  );

  Serial.print("NETWORK    : ");

  if (directMode)
  {
    Serial.println(
      "DIRECT TO S3"
    );
  }
  else
  {
    Serial.println(
      "HOME WIFI + MQTT"
    );
  }

  Serial.print("WiFi       : ");

  if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
    Serial.print("CONNECTED to ");
    Serial.println(WiFi.SSID());
  }
  else
  {
    Serial.println("DISCONNECTED");
  }

  Serial.println(
    "================================"
  );
}

// ============================================================
// 22. SETUP
// ============================================================

void setup()
{
  Serial.begin(9600);

  // --------------------------------------------------------
  // MPU6050
  // --------------------------------------------------------

  Wire.begin(
    MPU_SDA_PIN,
    MPU_SCL_PIN
  );

  Serial.println(
    "[MPU] Looking for MPU6050..."
  );

  while (!mpu.begin())
  {
    Serial.println(
      "[MPU] MPU6050 NOT FOUND"
    );

    Serial.println(
      "[MPU] Check SDA / SCL / power"
    );

    delay(1000);
  }

  Serial.println(
    "[MPU] MPU6050 FOUND"
  );

  mpu.setAccelerometerRange(
    MPU6050_RANGE_8_G
  );

  mpu.setGyroRange(
    MPU6050_RANGE_500_DEG
  );

  mpu.setFilterBandwidth(
    MPU6050_BAND_21_HZ
  );

  // Keep the node still here.
  calibrateMPU();

  // --------------------------------------------------------
  // MQTT
  // --------------------------------------------------------

  mqttClient.setServer(
    MQTT_HOST,
    MQTT_PORT
  );

  mqttClient.setSocketTimeout(1);

  // --------------------------------------------------------
  // WIFI
  // --------------------------------------------------------

  startHomeWiFi();

  Serial.println();
  Serial.println(
    "[F7] Motion node started."
  );
}

// ============================================================
// 23. LOOP
// ============================================================

void loop()
{
  /*
   * 1. Maintain network.
   *
   * HOME WiFi works:
   *     use MQTT.
   *
   * HOME WiFi lost:
   *     switch to S3 direct AP.
   */

  maintainWiFi();

  maintainMQTT();

  if (
    !directMode &&
    mqttClient.connected()
  )
  {
    mqttClient.loop();
  }

  /*
   * 2. Read MPU6050.
   */

  readMotionSensor();

  /*
   * 3. NORMAL / WARNING / DANGER.
   */

  updateMotionLevel();

  /*
   * 4. Send data.
   *
   * HOME mode  -> MQTT
   * DIRECT mode -> UDP directly to S3
   */

  if (directMode)
  {
    sendDirectToS3();
  }
  else
  {
    publishMQTTData();
  }

  /*
   * 5. Print for demo.
   */

  printMotionData();

  /*
   * About 2 updates / second.
   */

  delay(500);
}
