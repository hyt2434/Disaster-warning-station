/*
 * ============================================================
 * ESP32-S3 MAIN - SIMPLE VERSION
 * MQTT NORMAL + UDP FALLBACK
 * ============================================================
 *
 * BUZZER LOGIC:
 * - If ANY sensor is DANGER  -> BUZZER HIGH
 * - If ALL sensors are NOT DANGER -> BUZZER LOW
 *
 * Buzzer is always LOW at startup.
 *
 * WATER:
 * - Sensor height = 100 cm
 * - Minimum reliable distance = 23 cm
 * - Water level = 100 - distance
 * - SAFE    < 60 cm
 * - WARNING 60..69.99 cm
 * - DANGER  >= 70 cm
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// WIFI + MQTT
// ============================================================

const char* HOME_WIFI_SSID = "Thanh Xuan";
const char* HOME_WIFI_PASSWORD = "22122006";
const char* MQTT_HOST = "192.168.1.12";
const int MQTT_PORT = 1883;

const char* TOPIC_TELEMETRY = "disaster/main/telemetry";
const char* TOPIC_MOTION = "disaster/f7/state";
const char* TOPIC_STATUS = "disaster/main/status";

// ============================================================
// FALLBACK AP + UDP
// ============================================================

const char* DIRECT_AP_SSID = "DISASTER_MAIN_DIRECT";
const char* DIRECT_AP_PASSWORD = "12345678";
const int DIRECT_UDP_PORT = 4210;

IPAddress DIRECT_AP_IP(192, 168, 4, 1);
IPAddress DIRECT_AP_GATEWAY(192, 168, 4, 1);
IPAddress DIRECT_AP_SUBNET(255, 255, 255, 0);

// ============================================================
// PINS
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

// ============================================================
// LEVELS
// ============================================================

const int SAFE = 0;
const int WARNING = 1;
const int DANGER = 2;

// ============================================================
// THRESHOLDS
// ============================================================

const float TEMP_WARNING = 35.0;
const float TEMP_DANGER = 40.0;

const int GAS_WARNING = 1300;
const int GAS_DANGER = 1600;

const float SENSOR_HEIGHT_CM = 100.0;
const float MIN_VALID_DISTANCE_CM = 23.0;
const float MAX_WATER_LEVEL_CM = SENSOR_HEIGHT_CM - MIN_VALID_DISTANCE_CM;
const float WATER_WARNING_CM = 60.0;
const float WATER_DANGER_CM = 70.0;

// ============================================================
// OBJECTS
// ============================================================

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiUDP udp;

// ============================================================
// SENSOR VALUES
// ============================================================

float temperature = 0.0, humidity = 0.0;
int gasRaw = 0, gasAverage = 0;
float distanceCm = 0.0, waterLevelCm = 0.0, waterPercent = 0.0;

int temperatureLevel = SAFE, gasLevel = SAFE, waterLevel = SAFE, motionLevel = SAFE, systemLevel = SAFE;

float motionTilt = 0.0, motionVibration = 0.0, motionImpact = 0.0;
String motionSource = "NONE";

bool buzzerOn = false;
bool homeWiFiWasConnected = false;

unsigned long lastMotionUpdate = 0;
unsigned long lastSensorRead = 0;
unsigned long lastTelemetry = 0;
unsigned long lastMQTTRetry = 0;

const unsigned long MOTION_TIMEOUT_MS = 4000;
const unsigned long MQTT_RETRY_MS = 5000;

// ============================================================
// HELPERS
// ============================================================

const char* levelToText(int level)
{
  if (level == DANGER) return "DANGER";
  if (level == WARNING) return "WARNING";
  return "SAFE";
}

int textToLevel(String text)
{
  text.trim();
  text.toUpperCase();

  if (text == "DANGER") return DANGER;
  if (text == "WARNING" || text == "WARN") return WARNING;

  return SAFE;
}

// ============================================================
// DHT11
// ============================================================

void readDHT()
{
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h))
  {
    temperatureLevel = SAFE;
    Serial.println("[DHT] Read failed -> SAFE");
    return;
  }

  temperature = t;
  humidity = h;

  if (temperature >= TEMP_DANGER) temperatureLevel = DANGER;
  else if (temperature >= TEMP_WARNING) temperatureLevel = WARNING;
  else temperatureLevel = SAFE;
}

// ============================================================
// MQ-2
// ============================================================

void readMQ2()
{
  long total = 0;

  for (int i = 0; i < 5; i++)
  {
    gasRaw = analogRead(MQ2_PIN);
    total += gasRaw;
    delay(20);
  }

  gasAverage = total / 5;

  if (gasAverage >= GAS_DANGER) gasLevel = DANGER;
  else if (gasAverage >= GAS_WARNING) gasLevel = WARNING;
  else gasLevel = SAFE;
}

// ============================================================
// JSN-SR04T
// ============================================================

float readDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) return -1.0;

  return duration * 0.0343 / 2.0;
}

void readWater()
{
  float d = readDistance();

  // No echo: SAFE for demo, clear old DANGER state.
  if (d < 0)
  {
    distanceCm = -1.0;
    waterLevelCm = 0.0;
    waterPercent = 0.0;
    waterLevel = SAFE;

    Serial.println("[WATER] No echo -> SAFE");
    return;
  }

  // Blind zone: anything closer than 23 cm is already very high water.
  if (d < MIN_VALID_DISTANCE_CM) d = MIN_VALID_DISTANCE_CM;

  distanceCm = d;
  waterLevelCm = SENSOR_HEIGHT_CM - distanceCm;

  if (waterLevelCm < 0) waterLevelCm = 0;
  if (waterLevelCm > MAX_WATER_LEVEL_CM) waterLevelCm = MAX_WATER_LEVEL_CM;

  waterPercent = (waterLevelCm / SENSOR_HEIGHT_CM) * 100.0;

  if (waterLevelCm >= WATER_DANGER_CM) waterLevel = DANGER;
  else if (waterLevelCm >= WATER_WARNING_CM) waterLevel = WARNING;
  else waterLevel = SAFE;
}

// ============================================================
// MOTION FROM MQTT
// ============================================================

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  if (String(topic) != TOPIC_MOTION) return;

  String message = "";

  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  motionLevel = textToLevel(message);
  motionSource = "MQTT";
  lastMotionUpdate = millis();

  Serial.print("[MQTT] Motion -> ");
  Serial.println(levelToText(motionLevel));
}

// ============================================================
// MOTION FROM UDP FALLBACK
// ============================================================

void receiveUDP()
{
  int packetSize = udp.parsePacket();

  if (packetSize <= 0) return;

  char buffer[128];
  int len = udp.read(buffer, sizeof(buffer) - 1);

  if (len <= 0) return;

  buffer[len] = '\0';

  String packet = String(buffer);
  packet.trim();

  // Accept simple packet: DANGER / WARNING / NORMAL / SAFE
  int c1 = packet.indexOf(',');

  if (c1 < 0)
  {
    motionLevel = textToLevel(packet);
    motionSource = "UDP";
    lastMotionUpdate = millis();

    Serial.print("[UDP] Motion -> ");
    Serial.println(levelToText(motionLevel));
    return;
  }

  // Full packet: STATUS,TILT,VIBRATION,IMPACT
  int c2 = packet.indexOf(',', c1 + 1);
  int c3 = packet.indexOf(',', c2 + 1);

  if (c2 < 0 || c3 < 0) return;

  motionLevel = textToLevel(packet.substring(0, c1));
  motionTilt = packet.substring(c1 + 1, c2).toFloat();
  motionVibration = packet.substring(c2 + 1, c3).toFloat();
  motionImpact = packet.substring(c3 + 1).toFloat();
  motionSource = "UDP";
  lastMotionUpdate = millis();

  Serial.print("[UDP] Motion -> ");
  Serial.println(levelToText(motionLevel));
}

void checkMotionTimeout()
{
  if (lastMotionUpdate == 0) return;

  if (millis() - lastMotionUpdate > MOTION_TIMEOUT_MS)
  {
    motionLevel = SAFE;
    motionSource = "NONE";
    lastMotionUpdate = 0;

    Serial.println("[MOTION] Timeout -> SAFE");
  }
}

// ============================================================
// SYSTEM + OUTPUT
// ============================================================

void updateSystem()
{
  systemLevel = SAFE;

  if (temperatureLevel > systemLevel) systemLevel = temperatureLevel;
  if (gasLevel > systemLevel) systemLevel = gasLevel;
  if (waterLevel > systemLevel) systemLevel = waterLevel;
  if (motionLevel > systemLevel) systemLevel = motionLevel;
}

void updateLED()
{
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN, LOW);

  if (systemLevel == SAFE) digitalWrite(LED_GREEN_PIN, HIGH);
  else if (systemLevel == WARNING) digitalWrite(LED_YELLOW_PIN, HIGH);
  else digitalWrite(LED_RED_PIN, HIGH);
}

void updateBuzzer()
{
  // SIMPLE LOGIC:
  // Any DANGER -> ON
  // No DANGER  -> OFF
  if (temperatureLevel == DANGER || gasLevel == DANGER || waterLevel == DANGER || motionLevel == DANGER)
  {
    buzzerOn = true;
    digitalWrite(BUZZER_PIN, HIGH);
  }
  else
  {
    buzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ============================================================
// WIFI
// ============================================================

void startWiFi()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  WiFi.softAPConfig(DIRECT_AP_IP, DIRECT_AP_GATEWAY, DIRECT_AP_SUBNET);
  WiFi.softAP(DIRECT_AP_SSID, DIRECT_AP_PASSWORD);

  udp.begin(DIRECT_UDP_PORT);

  Serial.print("[DIRECT] SSID: ");
  Serial.println(DIRECT_AP_SSID);

  Serial.print("[DIRECT] IP: ");
  Serial.println(WiFi.softAPIP());

  WiFi.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);

  Serial.print("[WiFi] Connecting to: ");
  Serial.println(HOME_WIFI_SSID);
}

void maintainWiFi()
{
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected && !homeWiFiWasConnected)
  {
    homeWiFiWasConnected = true;

    Serial.print("[WiFi] Connected. IP: ");
    Serial.println(WiFi.localIP());
  }

  if (!connected && homeWiFiWasConnected)
  {
    homeWiFiWasConnected = false;

    Serial.println("[WiFi] Home WiFi lost. Direct AP still active.");
  }
}

// ============================================================
// MQTT
// ============================================================

void maintainMQTT()
{
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connected()) return;
  if (millis() - lastMQTTRetry < MQTT_RETRY_MS) return;

  lastMQTTRetry = millis();

  String clientId = "main-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);

  if (mqttClient.connect(clientId.c_str(), TOPIC_STATUS, 1, true, "offline"))
  {
    mqttClient.publish(TOPIC_STATUS, "online", true);
    mqttClient.subscribe(TOPIC_MOTION);

    Serial.println("[MQTT] Connected");
  }
  else
  {
    Serial.print("[MQTT] Failed state=");
    Serial.println(mqttClient.state());
  }
}

// ============================================================
// TELEMETRY
// ============================================================

void publishTelemetry()
{
  if (!mqttClient.connected()) return;

  String data = "{";
  data += "\"temperature\":" + String(temperature, 1) + ",";
  data += "\"humidity\":" + String(humidity, 1) + ",";
  data += "\"gas\":" + String(gasAverage) + ",";
  data += "\"distanceCm\":" + String(distanceCm, 1) + ",";
  data += "\"waterLevelCm\":" + String(waterLevelCm, 1) + ",";
  data += "\"motion\":\"" + String(levelToText(motionLevel)) + "\",";
  data += "\"system\":\"" + String(levelToText(systemLevel)) + "\",";
  data += "\"buzzer\":" + String(buzzerOn ? "true" : "false");
  data += "}";

  mqttClient.publish(TOPIC_TELEMETRY, data.c_str());
}

// ============================================================
// SERIAL
// ============================================================

void printData()
{
  Serial.println();
  Serial.println("================================");

  Serial.print("Temperature : "); Serial.print(temperature); Serial.print(" C -> "); Serial.println(levelToText(temperatureLevel));
  Serial.print("Humidity    : "); Serial.print(humidity); Serial.println(" %");
  Serial.print("MQ-2        : "); Serial.print(gasAverage); Serial.print(" ADC -> "); Serial.println(levelToText(gasLevel));

  Serial.print("Distance    : ");
  if (distanceCm < 0) Serial.println("NO ECHO");
  else { Serial.print(distanceCm); Serial.println(" cm"); }

  Serial.print("Water       : "); Serial.print(waterLevelCm); Serial.print(" cm -> "); Serial.println(levelToText(waterLevel));
  Serial.print("Motion      : "); Serial.print(levelToText(motionLevel)); Serial.print(" via "); Serial.println(motionSource);
  Serial.print("SYSTEM      : "); Serial.println(levelToText(systemLevel));
  Serial.print("BUZZER      : "); Serial.println(buzzerOn ? "ON" : "OFF");

  Serial.print("Cause       : ");
  if (!buzzerOn) Serial.println("NONE");
  else
  {
    if (temperatureLevel == DANGER) Serial.print("TEMPERATURE ");
    if (gasLevel == DANGER) Serial.print("GAS ");
    if (waterLevel == DANGER) Serial.print("WATER ");
    if (motionLevel == DANGER) Serial.print("MOTION ");
    Serial.println();
  }

  Serial.print("HOME WiFi   : "); Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
  Serial.print("DIRECT AP   : "); Serial.println(DIRECT_AP_SSID);
  Serial.print("DIRECT IP   : "); Serial.println(WiFi.softAPIP());

  Serial.println("================================");
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // IMPORTANT:
  // Always force buzzer LOW before using it.
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(MQ2_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);

  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN, LOW);

  analogReadResolution(12);
  dht.begin();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  mqttClient.setSocketTimeout(1);

  startWiFi();

  Serial.println("[MAIN] Started");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  unsigned long now = millis();

  maintainWiFi();
  maintainMQTT();

  if (mqttClient.connected()) mqttClient.loop();

  receiveUDP();
  checkMotionTimeout();

  if (now - lastSensorRead >= 2000)
  {
    lastSensorRead = now;

    readDHT();
    readMQ2();
    readWater();

    updateSystem();
    updateLED();
    updateBuzzer();

    printData();
  }

  // Motion can change between sensor readings,
  // so keep outputs updated continuously.
  updateSystem();
  updateLED();
  updateBuzzer();

  if (now - lastTelemetry >= 2000)
  {
    lastTelemetry = now;
    publishTelemetry();
  }

  delay(10);
}