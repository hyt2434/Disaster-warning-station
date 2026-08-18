/*
 * ============================================================
 * ESP32-S3 MAIN
 * NORMAL MQTT + DIRECT UDP FALLBACK
 * ============================================================
 *
 * NORMAL MODE:
 *   C3 -> Home WiFi -> MQTT -> S3
 *
 * FALLBACK MODE:
 *   Home WiFi lost
 *   C3 -> S3 private WiFi AP -> UDP -> S3
 *
 * LOCAL SENSORS:
 *   DHT11
 *   MQ-2
 *   JSN-SR04T
 *
 * REMOTE SENSOR:
 *   C3 + MPU6050
 *
 * SYSTEM LEVEL:
 *   Highest of:
 *     temperature
 *     gas
 *     water
 *     motion
 *
 * OUTPUT:
 *   SAFE    -> GREEN LED
 *   WARNING -> YELLOW LED
 *   DANGER  -> RED LED + BUZZER
 *
 * WATER LOGIC:
 *   Sensor is mounted 100 cm above ground.
 *
 *   waterLevel = 100 - distance
 *
 *   Distance large  -> water low
 *   Distance small  -> water high
 *
 *   JSN-SR04T minimum reliable distance: 23 cm
 *   Therefore maximum directly measurable water level:
 *
 *     100 - 23 = 77 cm
 *
 *   SAFE    : distance > 40 cm  -> water < 60 cm
 *   WARNING : 30 < distance <= 40 cm -> water
 * from 60 to below 70 cm
 *   DANGER  : 23 <= distance <= 30 cm -> water from 70 to 77 cm
 *
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// 1. HOME WIFI + MQTT
// ============================================================

const char* HOME_WIFI_SSID = "YOUR_WIFI_SSID";

const char* HOME_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Laptop IPv4 running Mosquitto.
// Example: 192.168.1.12
const char* MQTT_HOST = "192.168.1.12";

const int MQTT_PORT = 1883;

const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

const char* DEVICE_ID = "main-station-01";

// Main telemetry
const char* TOPIC_TELEMETRY = "disaster/main/telemetry";

// Manual buzzer command from web
const char* TOPIC_BUZZER_COMMAND = "disaster/main/command/buzzer";

// Buzzer state
const char* TOPIC_BUZZER_STATE = "disaster/main/state/buzzer";

// C3 motion state
const char* TOPIC_MOTION_STATE = "disaster/f7/state";

// Main online/offline
const char* TOPIC_STATUS = "disaster/main/status";

// ============================================================
// 2. DIRECT WIFI FALLBACK FOR C3
// ============================================================

const char* DIRECT_AP_SSID = "DISASTER_MAIN_DIRECT";

const char* DIRECT_AP_PASSWORD = "12345678";

IPAddress DIRECT_AP_IP(192, 168, 4, 1);

IPAddress DIRECT_AP_GATEWAY(192, 168, 4, 1);

IPAddress DIRECT_AP_SUBNET(255, 255, 255, 0);

const int DIRECT_UDP_PORT = 4210;

// If no motion packet arrives for 4 s,
// reset motion state to SAFE.
const unsigned long MOTION_TIMEOUT_MS = 4000;

// ============================================================
// 3. PIN
// ============================================================

const int DHT_PIN = 4;
const int MQ2_PIN = 5;

const int TRIG_PIN = 6;
const int ECHO_PIN = 7;

const int BUZZER_PIN = 15;

const int LED_GREEN_PIN = 16;
const int LED_YELLOW_PIN = 17;
const int LED_RED_PIN = 18;

#define DHT_TYPE DHT11

// Change to false if you want silent testing.
const bool ENABLE_BUZZER = true;

// ============================================================
// 4. LEVELS
// ============================================================

const int SAFE = 0;
const int WARNING = 1;
const int DANGER = 2;

// ============================================================
// 5. THRESHOLDS
// ============================================================

// --------------------
// Temperature
// --------------------

const float TEMP_WARNING = 35.0;

const float TEMP_DANGER = 40.0;

// --------------------
// MQ-2
// --------------------

const int GAS_WARNING = 1300;

const int GAS_DANGER = 1600;

// --------------------
// Water
// --------------------

// JSN-SR04T is mounted 100 cm above ground.
const float SENSOR_HEIGHT_CM = 100.0;

// Minimum reliable distance of the JSN-SR04T in this setup.
const float MIN_VALID_DISTANCE_CM = 23.0;

// Maximum water level that can be measured reliably:
// 100 - 23 = 77 cm
const float MAX_MEASURABLE_WATER_LEVEL_CM = SENSOR_HEIGHT_CM - MIN_VALID_DISTANCE_CM;

// Alarm thresholds are compared directly with the measured distance.
// A smaller distance means a higher, more dangerous water level.
//
// SAFE:    distance > 40 cm
// WARNING: 30 < distance <= 40 cm
// DANGER:  23 <= distance <= 30 cm
const float WATER_WARNING_DISTANCE_CM = 40.0;

const float WATER_DANGER_DISTANCE_CM = 30.0;

// ============================================================
// 6. OBJECTS
// ============================================================

DHT dht(DHT_PIN, DHT_TYPE);

WiFiClient wifiClient;

PubSubClient mqttClient(wifiClient);

WiFiUDP udp;

// ============================================================
// 7. SENSOR VALUES
// ============================================================

float temperature = 0.0;
float humidity = 0.0;

int gasRaw = 0;
int gasFiltered = 0;

float distanceCm = 0.0;
float waterLevelCm = 0.0;
float waterLevelPercent = 0.0;

bool dhtValid = false;
bool waterValid = false;

// ============================================================
// 8. SENSOR LEVELS
// ============================================================

int temperatureLevel = SAFE;
int gasLevel = SAFE;
int waterLevel = SAFE;

// ============================================================
// 9. MOTION FROM C3
// ============================================================

int motionLevel = SAFE;

float motionTilt = 0.0;
float motionVibration = 0.0;
float motionImpact = 0.0;

String motionSource = "NONE";

unsigned long lastMotionUpdate = 0;

// ============================================================
// 10. SYSTEM
// ============================================================

int systemLevel = SAFE;

bool manualBuzzerOn = false;

bool buzzerOn = false;

bool previousBuzzerState = false;

// ============================================================
// 11. TIMERS
// ============================================================

unsigned long lastMQTTRetry = 0;

unsigned long lastNetworkStatusLog = 0;

unsigned long lastLocalSensorRead = 0;

unsigned long lastTelemetry = 0;

bool homeWiFiWasConnected = false;

const unsigned long MQTT_RETRY_INTERVAL_MS = 5000;

const unsigned long NETWORK_LOG_INTERVAL_MS = 5000;

// ============================================================
// 12. HELPERS
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

int textToLevel(String text)
{
  text.trim();
  text.toUpperCase();

  if (text == "DANGER")
  {
    return DANGER;
  }

  if (text == "WARNING" || text == "WARN")
  {
    return WARNING;
  }

  return SAFE;
}

const char* wifiStatusToText(wl_status_t status)
{
  switch (status)
  {
    case WL_CONNECTED:
      return "CONNECTED";

    case WL_NO_SSID_AVAIL:
      return "SSID NOT FOUND";

    case WL_CONNECT_FAILED:
      return "CONNECT FAILED";

    case WL_CONNECTION_LOST:
      return "CONNECTION LOST";

    case WL_DISCONNECTED:
      return "DISCONNECTED";

    case WL_IDLE_STATUS:
      return "CONNECTING";

    default:
      return "UNKNOWN";
  }
}

const char* mqttStateToText(int state)
{
  switch (state)
  {
    case 0:
      return "CONNECTED";

    case -4:
      return "TIMEOUT";

    case -3:
      return "CONNECTION LOST";

    case -2:
      return "BROKER UNREACHABLE";

    case -1:
      return "DISCONNECTED";

    case 1:
      return "BAD PROTOCOL";

    case 2:
      return "BAD CLIENT ID";

    case 3:
      return "BROKER UNAVAILABLE";

    case 4:
      return "BAD CREDENTIALS";

    case 5:
      return "NOT AUTHORIZED";

    default:
      return "UNKNOWN";
  }
}

// ============================================================
// 13. DHT11
// ============================================================

void readDHT11()
{
  float newTemperature = dht.readTemperature();

  float newHumidity = dht.readHumidity();

  if (isnan(newTemperature) || isnan(newHumidity))
  {
    dhtValid = false;

    Serial.println("[DHT] Read failed");

    return;
  }

  temperature = newTemperature;

  humidity = newHumidity;

  dhtValid = true;
}

// ============================================================
// 14. MQ-2
// ============================================================

int readMQ2()
{
  long total = 0;

  Serial.print("[MQ2] Samples: ");

  for (int i = 0; i < 5; i++)
  {
    int currentValue = analogRead(MQ2_PIN);

    gasRaw = currentValue;

    total += currentValue;

    Serial.print(currentValue);

    Serial.print(" ");

    delay(20);
  }

  int averageValue = total / 5;

  Serial.print("-> Average: ");

  Serial.println(averageValue);

  return averageValue;
}

// ============================================================
// 15. JSN-SR04T
// ============================================================

float readDistance()
{
  digitalWrite(TRIG_PIN, LOW);

  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);

  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0)
  {
    return -1.0;
  }

  float distance = duration * 0.0343 / 2.0;

  return distance;
}

// ============================================================
// 16. WATER LEVEL
// ============================================================

void readWaterSensor()
{
  float measuredDistance = readDistance();

  // ----------------------------------------------------------
  // CASE 1:
  // No echo received.
  //
  // Do not reset water level to SAFE.
  // Keep the previous valid water state instead.
  // ----------------------------------------------------------

  if (measuredDistance < 0)
  {
    waterValid = false;

    Serial.println("[WATER] No echo - keep previous water state");

    return;
  }

  // ----------------------------------------------------------
  // CASE 2:
  // Object/water is closer than 23 cm.
  //
  // The sensor cannot measure reliably in this region.
  // Since the sensor is 100 cm above ground,
  // distance < 23 cm means water is already above
  // approximately 77 cm.
  //
  // For safety, clamp the reading to 23 cm.
  // This gives the highest measurable water level: 77 cm,
  // which is classified as DANGER.
  // ----------------------------------------------------------

  if (measuredDistance < MIN_VALID_DISTANCE_CM)
  {
    Serial.print("[WATER] Too close for reliable measurement: ");

    Serial.print(measuredDistance);

    Serial.println(" cm -> clamp to 23 cm / DANGER zone");

    measuredDistance = MIN_VALID_DISTANCE_CM;
  }

  distanceCm = measuredDistance;

  /*
   * Physical setup:
   *
   * JSN-SR04T
   *     |
   *     | 100 cm
   *     |
   *   ground
   *
   * Formula:
   *
   * waterLevel =
   * sensorHeight - measuredDistance
   *
   * Examples:
   *
   * distance = 90 cm
   * water    = 10 cm
   *
   * distance = 40 cm
   * water    = 60 cm
   *
   * distance = 30 cm
   * water    = 70 cm
   *
   * distance = 23 cm
   * water    = 77 cm
   */

  waterLevelCm = SENSOR_HEIGHT_CM - distanceCm;

  // If measured distance is greater than the mounting height,
  // treat water level as 0 cm.
  if (waterLevelCm < 0)
  {
    waterLevelCm = 0;
  }

  // Because the sensor cannot reliably measure closer than 23 cm,
  // cap the directly measurable water level at 77 cm.
  if (waterLevelCm > MAX_MEASURABLE_WATER_LEVEL_CM)
  {
    waterLevelCm = MAX_MEASURABLE_WATER_LEVEL_CM;
  }

  // Percentage is relative to the real 100 cm installation height.
  // Therefore the maximum directly measurable percentage is 77%.
  waterLevelPercent = (waterLevelCm / SENSOR_HEIGHT_CM) * 100.0;

  waterValid = true;
}

// ============================================================
// 17. CLASSIFY TEMPERATURE
// ============================================================

int checkTemperatureLevel()
{
  if (temperature >= TEMP_DANGER)
  {
    return DANGER;
  }

  if (temperature >= TEMP_WARNING)
  {
    return WARNING;
  }

  return SAFE;
}

// ============================================================
// 18. CLASSIFY GAS
// ============================================================

int checkGasLevel()
{
  if (gasFiltered >= GAS_DANGER)
  {
    return DANGER;
  }

  if (gasFiltered >= GAS_WARNING)
  {
    return WARNING;
  }

  return SAFE;
}

// ============================================================
// 19. CLASSIFY WATER
// ============================================================

int checkWaterLevel()
{
  /*
   * Distance from 23 cm to 30 cm
   * Water level from 70 cm to 77 cm
   * -> DANGER
   */

  if (distanceCm <= WATER_DANGER_DISTANCE_CM)
  {
    return DANGER;
  }

  /*
   * Distance greater than 30 cm and up to 40 cm
   * Water level from 60 cm to below 70 cm

   * * -> WARNING
   */

  if (distanceCm <= WATER_WARNING_DISTANCE_CM)
  {
    return WARNING;
  }

  /*
   * Distance greater than 40 cm
   * Water < 60 cm
   * -> SAFE
   */

  return SAFE;
}

// ============================================================
// 20. READ + CLASSIFY LOCAL SENSORS
// ============================================================

void readAndClassifyLocalSensors()
{
  readDHT11();

  gasFiltered = readMQ2();

  readWaterSensor();

  if (dhtValid)
  {
    temperatureLevel = checkTemperatureLevel();
  }

  gasLevel = checkGasLevel();

  if (waterValid)
  {
    waterLevel = checkWaterLevel();
  }
}

// ============================================================
// 21. MOTION THROUGH MQTT
// ============================================================

void updateMotionFromMQTT(String payload)
{
  motionLevel = textToLevel(payload);

  motionSource = "MQTT";

  lastMotionUpdate = millis();

  Serial.print("[MQTT] Motion from C3: ");

  Serial.println(levelToText(motionLevel));
}

// ============================================================
// 22. MOTION THROUGH DIRECT UDP
// ============================================================

void receiveDirectMotion()
{
  int packetSize = udp.parsePacket();

  if (packetSize <= 0)
  {
    return;
  }

  char buffer[128];

  int length = udp.read(buffer, sizeof(buffer) - 1);

  if (length <= 0)
  {
    return;
  }

  buffer[length] = '\0';

  String packet = String(buffer);

  packet.trim();

  /*
   * Expected:
   *
   * STATUS,TILT,VIBRATION,IMPACT
   *
   * Example:
   *
   * DANGER,25.4,3.10,4.50
   */

  int comma1 = packet.indexOf(',');

  int comma2 = packet.indexOf(',', comma1 + 1);

  int comma3 = packet.indexOf(',', comma2 + 1);

  if (comma1 < 0 || comma2 < 0 || comma3 < 0)
  {
    Serial.print("[DIRECT] Invalid packet: ");

    Serial.println(packet);

    return;
  }

  String statusText = packet.substring(0, comma1);

  String tiltText = packet.substring(comma1 + 1, comma2);

  String vibrationText = packet.substring(comma2 + 1, comma3);

  String impactText = packet.substring(comma3 + 1);

  motionLevel = textToLevel(statusText);

  motionTilt = tiltText.toFloat();

  motionVibration = vibrationText.toFloat();

  motionImpact = impactText.toFloat();

  motionSource = "DIRECT";

  lastMotionUpdate = millis();

  Serial.println();

  Serial.print("[DIRECT] Motion: ");

  Serial.print(levelToText(motionLevel));

  Serial.print(" | tilt=");

  Serial.print(motionTilt);

  Serial.print(" | vibration=");

  Serial.print(motionVibration);

  Serial.print(" | impact=");

  Serial.println(motionImpact);
}

// ============================================================
// 23. MOTION TIMEOUT
// ============================================================

void checkMotionTimeout()
{
  if (lastMotionUpdate == 0)
  {
    return;
  }

  if (millis() - lastMotionUpdate <= MOTION_TIMEOUT_MS)
  {
    return;
  }

  motionLevel = SAFE;

  motionSource = "NONE";

  lastMotionUpdate = 0;

  Serial.println("[MOTION] C3 data timeout -> SAFE");
}

// ============================================================
// 24. OVERALL SYSTEM LEVEL
// ============================================================

void updateSystemLevel()
{
  systemLevel = temperatureLevel;

  if (gasLevel > systemLevel)
  {
    systemLevel = gasLevel;
  }

  if (waterLevel > systemLevel)
  {
    systemLevel = waterLevel;
  }

  if (motionLevel > systemLevel)
  {
    systemLevel = motionLevel;
  }
}

// ============================================================
// 25. LED
// ============================================================

void turnOffAllLEDs()
{
  digitalWrite(LED_GREEN_PIN, LOW);

  digitalWrite(LED_YELLOW_PIN, LOW);

  digitalWrite(LED_RED_PIN, LOW);
}

void updateLED()
{
  turnOffAllLEDs();

  if (systemLevel == SAFE)
  {
    digitalWrite(LED_GREEN_PIN, HIGH);

    return;
  }

  if (systemLevel == WARNING)
  {
    digitalWrite(LED_YELLOW_PIN, HIGH);

    return;
  }

  digitalWrite(LED_RED_PIN, HIGH);
}

// ============================================================
// 26. BUZZER
// ============================================================

void publishBuzzerState()
{
  if (!mqttClient.connected())
  {
    return;
  }

  mqttClient.publish(TOPIC_BUZZER_STATE, buzzerOn ? "ON" : "OFF", true);
}

void updateBuzzer()
{
  // Silent test mode.
  if (!ENABLE_BUZZER)
  {
    buzzerOn = false;

    digitalWrite(BUZZER_PIN, LOW);

    return;
  }

  bool automaticDanger = (systemLevel == DANGER);

  buzzerOn = automaticDanger || manualBuzzerOn;

  digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);

  if (buzzerOn != previousBuzzerState)
  {
    previousBuzzerState = buzzerOn;

    publishBuzzerState();
  }
}

// ============================================================
// 27. MQTT CALLBACK
// ============================================================

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  String topicText = String(topic);

  String message = "";

  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  message.trim();

  // --------------------------
  // C3 motion
  // --------------------------

  if (topicText == TOPIC_MOTION_STATE)
  {
    updateMotionFromMQTT(message);

    return;
  }

  // --------------------------
  // Manual buzzer
  // --------------------------

  if (topicText == TOPIC_BUZZER_COMMAND)
  {
    message.toUpperCase();

    if (message == "ON")
    {
      manualBuzzerOn = true;

      Serial.println("[MQTT] Manual buzzer ON");
    }

    else if (message == "OFF")
    {
      manualBuzzerOn = false;

      Serial.println("[MQTT] Manual buzzer OFF");
    }
  }
}

// ============================================================
// 28. START AP + HOME WIFI
// ============================================================

void startHomeWiFi()
{
  Serial.println();

  Serial.println("[WiFi] Starting AP + STA...");

  /*
   * AP:
   * C3 fallback network
   *
   * STA:
   * HOME WiFi for MQTT
   */

  WiFi.mode(WIFI_AP_STA);

  WiFi.persistent(false);

  WiFi.setAutoReconnect(true);

  // --------------------------
  // DIRECT AP
  // --------------------------

  WiFi.softAPConfig(DIRECT_AP_IP, DIRECT_AP_GATEWAY, DIRECT_AP_SUBNET);

  bool apStarted = WiFi.softAP(DIRECT_AP_SSID, DIRECT_AP_PASSWORD);

  if (apStarted)
  {
    Serial.println("[DIRECT] Fallback AP started.");

    Serial.print("[DIRECT] SSID: ");

    Serial.println(DIRECT_AP_SSID);

    Serial.print("[DIRECT] IP: ");

    Serial.println(WiFi.softAPIP());
  }
  else
  {
    Serial.println("[DIRECT] Failed to start AP.");
  }

  // --------------------------
  // UDP
  // --------------------------

  bool udpStarted = udp.begin(DIRECT_UDP_PORT);

  Serial.print("[DIRECT] UDP: ");

  Serial.println(udpStarted ? "OK" : "FAILED");

  Serial.print("[DIRECT] UDP port: ");

  Serial.println(DIRECT_UDP_PORT);

  // --------------------------
  // HOME WIFI
  // --------------------------

  Serial.println();

  Serial.print("[WiFi] Connecting HOME: ");

  Serial.println(HOME_WIFI_SSID);

  /*
   * IMPORTANT:
   *
   * WiFi.begin() is called
   * only ONCE.
   */

  WiFi.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
}

// ============================================================
// 29. MONITOR HOME WIFI
// ============================================================

void maintainHomeWiFi()
{
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected && !homeWiFiWasConnected)
  {
    homeWiFiWasConnected = true;

    Serial.println();

    Serial.println("[WiFi] HOME CONNECTED");

    Serial.print("[WiFi] STA IP: ");

    Serial.println(WiFi.localIP());

    Serial.print("[WiFi] RSSI: ");

    Serial.print(WiFi.RSSI());

    Serial.println(" dBm");

    Serial.print("[DIRECT] AP IP: ");

    Serial.println(WiFi.softAPIP());
  }

  if (!connected && homeWiFiWasConnected)
  {
    homeWiFiWasConnected = false;

    Serial.println();

    Serial.println("[WiFi] HOME LOST");

    Serial.println("[DIRECT] Fallback AP remains active");
  }

  unsigned long now = millis();

  if (!connected && now - lastNetworkStatusLog >= NETWORK_LOG_INTERVAL_MS)
  {
    lastNetworkStatusLog = now;

    Serial.print("[WiFi] HOME status: ");

    Serial.println(wifiStatusToText(WiFi.status()));
  }

  /*
   * DO NOT:
   *
   * WiFi.begin()
   * WiFi.reconnect()
   * WiFi.disconnect()
   *
   * here.
   */
}

// ============================================================
// 30. MQTT
// ============================================================

void maintainMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (mqttClient.connected())
  {
    return;
  }

  unsigned long now = millis();

  if (now - lastMQTTRetry < MQTT_RETRY_INTERVAL_MS)
  {
    return;
  }

  lastMQTTRetry = now;

  Serial.print("[MQTT] Connecting to ");

  Serial.print(MQTT_HOST);

  Serial.print(":");

  Serial.println(MQTT_PORT);

  char clientId[48];

  snprintf(clientId, sizeof(clientId), "%s-%04X", DEVICE_ID,
           (uint16_t)(ESP.getEfuseMac() & 0xFFFF));

  bool success = false;

  if (strlen(MQTT_USER) > 0)
  {
    success =
        mqttClient.connect(clientId, MQTT_USER, MQTT_PASSWORD, TOPIC_STATUS, 1, true, "offline");
  }
  else
  {
    success = mqttClient.connect(clientId, TOPIC_STATUS, 1, true, "offline");
  }

  if (!success)
  {
    int state = mqttClient.state();

    Serial.print("[MQTT] Failed state=");

    Serial.print(state);

    Serial.print(" (");

    Serial.print(mqttStateToText(state));

    Serial.println(")");

    return;
  }

  Serial.println("[MQTT] Connected");

  mqttClient.publish(TOPIC_STATUS, "online", true);

  bool motionSubscribed = mqttClient.subscribe(TOPIC_MOTION_STATE);

  bool commandSubscribed = mqttClient.subscribe(TOPIC_BUZZER_COMMAND);

  Serial.print("[MQTT] Motion subscribe: ");

  Serial.println(motionSubscribed ? "OK" : "FAILED");

  Serial.print("[MQTT] Command subscribe: ");

  Serial.println(commandSubscribed ? "OK" : "FAILED");

  publishBuzzerState();
}

// ============================================================
// 31. TELEMETRY
// ============================================================

void publishTelemetry()
{
  if (!mqttClient.connected())
  {
    return;
  }

  String data = "{";

  data += "\"deviceId\":\"";

  data += DEVICE_ID;

  data += "\",";

  data += "\"temperature\":";

  data += String(temperature, 1);

  data += ",";

  data += "\"humidity\":";

  data += String(humidity, 1);

  data += ",";

  data += "\"gasRaw\":";

  data += String(gasRaw);

  data += ",";

  data += "\"gasFiltered\":";

  data += String(gasFiltered);

  data += ",";

  data += "\"distanceCm\":";

  data += String(distanceCm, 1);

  data += ",";

  data += "\"waterLevelCm\":";

  data += String(waterLevelCm, 1);

  data += ",";

  data += "\"waterLevelPercent\":";

  data += String(waterLevelPercent, 1);

  data += ",";

  data += "\"motionStatus\":\"";

  data += levelToText(motionLevel);

  data += "\",";

  data += "\"motionSource\":\"";

  data += motionSource;

  data += "\",";

  data += "\"motionTilt\":";

  data += String(motionTilt, 1);

  data += ",";

  data += "\"motionVibration\":";

  data += String(motionVibration, 2);

  data += ",";

  data += "\"motionImpact\":";

  data += String(motionImpact, 2);

  data += ",";

  data += "\"systemStatus\":\"";

  data += levelToText(systemLevel);

  data += "\",";

  data += "\"buzzer\":";

  data += buzzerOn ? "true" : "false";

  data += "}";

  Serial.print("[MQTT] Publish telemetry: ");

  Serial.println(data);

  mqttClient.publish(TOPIC_TELEMETRY, data.c_str());
}

// ============================================================
// 32. SERIAL STATUS
// ============================================================

void printSystemData()
{
  Serial.println();

  Serial.println("================================");

  Serial.print("Temperature     : ");

  Serial.print(temperature);

  Serial.print(" C -> ");

  Serial.println(levelToText(temperatureLevel));

  Serial.print("Humidity        : ");

  Serial.print(humidity);

  Serial.println(" %");

  Serial.print("MQ-2 raw        : ");

  Serial.print(gasRaw);

  Serial.println(" ADC");

  Serial.print("MQ-2 avg        : ");

  Serial.print(gasFiltered);

  Serial.print(" ADC -> ");

  Serial.println(levelToText(gasLevel));

  // --------------------------
  // WATER
  // --------------------------

  Serial.print("Water distance  : ");

  Serial.print(distanceCm);

  Serial.println(" cm");

  Serial.print("Water level     : ");

  Serial.print(waterLevelCm);

  Serial.print(" cm (");

  Serial.print(waterLevelPercent);

  Serial.print("%) -> ");

  Serial.println(levelToText(waterLevel));

  Serial.print("Water sensor min : ");

  Serial.print(MIN_VALID_DISTANCE_CM);

  Serial.print(" cm | Max level: ");

  Serial.print(MAX_MEASURABLE_WATER_LEVEL_CM);

  Serial.println(" cm");

  // --------------------------
  // MOTION
  // --------------------------

  Serial.print("Motion          : ");

  Serial.print(levelToText(motionLevel));

  Serial.print(" via ");

  Serial.println(motionSource);

  // --------------------------
  // SYSTEM
  // --------------------------

  Serial.print("SYSTEM          : ");

  Serial.println(levelToText(systemLevel));

  Serial.print("BUZZER          : ");

  if (!ENABLE_BUZZER)
  {
    Serial.println("DISABLED");
  }
  else
  {
    Serial.println(buzzerOn ? "ON" : "OFF");
  }

  Serial.print("HOME WiFi       : ");

  Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");

  Serial.print("MQTT            : ");

  Serial.print(mqttStateToText(mqttClient.state()));

  Serial.print(" | state=");

  Serial.println(mqttClient.state());

  Serial.print("DIRECT AP       : ");

  Serial.println(DIRECT_AP_SSID);

  Serial.print("DIRECT IP       : ");

  Serial.println(WiFi.softAPIP());

  Serial.println("================================");
}

// ============================================================
// 33. SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(1500);

  Serial.println();

  Serial.println("==============================");

  Serial.println("ESP32-S3 STARTING...");

  Serial.println("==============================");

  // --------------------------
  // Pins
  // --------------------------

  pinMode(MQ2_PIN, INPUT);

  pinMode(TRIG_PIN, OUTPUT);

  pinMode(ECHO_PIN, INPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(LED_GREEN_PIN, OUTPUT);

  pinMode(LED_YELLOW_PIN, OUTPUT);

  pinMode(LED_RED_PIN, OUTPUT);

  // --------------------------
  // Safe startup
  // --------------------------

  digitalWrite(BUZZER_PIN, LOW);

  digitalWrite(LED_GREEN_PIN, HIGH);

  digitalWrite(LED_YELLOW_PIN, LOW);

  digitalWrite(LED_RED_PIN, LOW);

  // --------------------------
  // Sensors
  // --------------------------

  analogReadResolution(12);

  dht.begin();

  // --------------------------
  // MQTT
  // --------------------------

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  mqttClient.setCallback(mqttCallback);

  mqttClient.setBufferSize(1024);

  mqttClient.setSocketTimeout(1);

  // --------------------------
  // AP + STA
  // --------------------------

  startHomeWiFi();

  Serial.println();

  Serial.println("[MAIN] ESP32-S3 started.");

  Serial.println("[MAIN] Direct C3 fallback is available.");
}

// ============================================================
// 34. LOOP
// ============================================================

void loop()
{
  unsigned long now = millis();

  // --------------------------
  // 1. Network
  // --------------------------

  maintainHomeWiFi();

  maintainMQTT();

  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  // --------------------------
  // 2. C3 direct fallback
  // --------------------------

  receiveDirectMotion();

  checkMotionTimeout();

  // --------------------------
  // 3. Read sensors every 2 s
  // --------------------------

  if (now - lastLocalSensorRead >= 2000)
  {
    lastLocalSensorRead = now;

    // Read sensor values
    readAndClassifyLocalSensors();

    // Recalculate immediately
    updateSystemLevel();

    // Update output immediately
    updateLED();
    updateBuzzer();

    // Print newest values
    printSystemData();
  }

  // --------------------------
  // 4. Keep output updated
  // --------------------------

  updateSystemLevel();
  updateLED();
  updateBuzzer();

  // --------------------------
  // 5. Telemetry every 2 s
  // --------------------------

  if (now - lastTelemetry >= 2000)
  {
    lastTelemetry = now;

    publishTelemetry();
  }

  delay(10);
}
