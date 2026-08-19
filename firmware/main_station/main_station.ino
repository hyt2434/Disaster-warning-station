/*
 * ============================================================
 * DISASTER WARNING STATION - ESP32-S3 MAIN (FINAL)
 * ============================================================
 *
 * LOCAL SENSORS:
 *   DHT11      -> temperature + humidity
 *   MQ-2       -> gas
 *   JSN-SR04T  -> water level
 *
 * REMOTE SENSOR:
 *   XIAO ESP32-C3 + MPU6050 -> motion
 *
 * NETWORK:
 *   F7 -> Home WiFi -> MQTT -> Main
 *
 * SYSTEM LEVEL:
 *   Highest of temperature / gas / water / motion
 *
 * OUTPUT:
 *   SAFE    -> GREEN LED
 *   WARNING -> YELLOW LED
 *   DANGER  -> RED LED + BUZZER (unless current alarm is muted)
 *
 * IMPORTANT BUZZER LOGIC:
 *   - DANGER starts -> buzzer automatically ON.
 *   - User sends OFF/MUTE during DANGER -> buzzer OFF, but system stays DANGER.
 *   - While the same alarm event is still active, buzzer stays OFF.
 *   - When the WHOLE system returns to SAFE -> buzzerMuted resets automatically.
 *   - The next DANGER event -> buzzer automatically ON again.
 *   - ON/UNMUTE cancels the mute; it does NOT force the buzzer ON while SAFE.
 *
 * MQTT:
 *   Telemetry      : disaster/main/telemetry
 *   Buzzer command : disaster/main/command/buzzer
 *                     payload: ON / OFF
 *
 *   Buzzer state : disaster/main/state/buzzer payload: ON / OFF
 *   Motion state : disaster/f7/state
 *   Main status    : disaster/main/status
 *
 * WATER:
 *   Sensor height = 100 cm
 *   Minimum reliable distance = 23 cm
 *   Water level = 100 - distance
 *   SAFE    < 60 cm
 *   WARNING 60..69.99 cm
 *   DANGER  >= 70 cm
 *
 * NOTE:
 *   Fill HOME_WIFI_SSID, HOME_WIFI_PASSWORD and MQTT_HOST
 *   before uploading.
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// 1. HOME WIFI + MQTT
// ============================================================

const char* HOME_WIFI_SSID = "YOUR_WIFI_SSID";
const char* HOME_WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// IPv4/domain of the machine running Mosquitto.
const char* MQTT_HOST = "192.168.1.12";
const int MQTT_PORT = 1883;

const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

const char* DEVICE_ID = "main-station-01";

const char* TOPIC_TELEMETRY = "disaster/main/telemetry";

const char* TOPIC_BUZZER_COMMAND = "disaster/main/command/buzzer";

const char* TOPIC_BUZZER_STATE = "disaster/main/state/buzzer";

const char* TOPIC_MOTION = "disaster/f7/state";

const char* TOPIC_STATUS = "disaster/main/status";

// ============================================================
// 2. PINS
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

// Set false for silent testing.
const bool ENABLE_BUZZER = true;

// If Serial says OFF but your physical buzzer still sounds,
// change this to false because your buzzer module is active-low.
const bool BUZZER_ACTIVE_HIGH = true;

// ============================================================
// 3. LEVELS
// ============================================================

const int SAFE = 0;
const int WARNING = 1;
const int DANGER = 2;

// ============================================================
// 4. THRESHOLDS
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
// 5. TIMING
// ============================================================

// Read local sensors and publish their latest values every 2 seconds.
const unsigned long SENSOR_INTERVAL_MS = 2000;
const unsigned long TELEMETRY_INTERVAL_MS = 2000;
const unsigned long MOTION_TIMEOUT_MS = 6000;
const unsigned long MQTT_RETRY_MS = 5000;
const unsigned long NETWORK_LOG_INTERVAL_MS = 5000;

// ============================================================
// 6. OBJECTS
// ============================================================

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ============================================================
// 7. SENSOR VALUES + LEVELS
// ============================================================

float temperature = 0.0;
float humidity = 0.0;

int gasRaw = 0;
int gasAverage = 0;

float distanceCm = -1.0;
float waterLevelCm = 0.0;
float waterPercent = 0.0;

bool waterValid = false;

int temperatureLevel = SAFE;
int gasLevel = SAFE;
int waterLevel = SAFE;
int motionLevel = SAFE;
int systemLevel = SAFE;

// ============================================================
// 8. MOTION FROM C3
// ============================================================

unsigned long lastMotionUpdate = 0;

// ============================================================
// 9. BUZZER EVENT STATE
// ============================================================

bool buzzerOn = false;

// true = user acknowledged/muted ONLY the current alarm event.
// It is automatically cleared when systemLevel returns to SAFE.
bool buzzerMuted = false;

bool previousBuzzerOn = false;

// ============================================================
// 10. NETWORK / TIMERS
// ============================================================

bool homeWiFiWasConnected = false;

unsigned long lastSensorRead = 0;
unsigned long lastTelemetry = 0;
unsigned long lastMQTTRetry = 0;
unsigned long lastNetworkStatusLog = 0;

// ============================================================
// 11. HELPERS
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

  if (text == "WARNING")
  {
    return WARNING;
  }

  // The only remaining contract value is SAFE.
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

bool isDangerAlarmActive()
{
  return systemLevel == DANGER;
}

void writeBuzzerHardware(bool shouldTurnOn)
{
  if (!ENABLE_BUZZER)
  {
    shouldTurnOn = false;
  }

  digitalWrite(BUZZER_PIN,
               BUZZER_ACTIVE_HIGH ? (shouldTurnOn ? HIGH : LOW) : (shouldTurnOn ? LOW : HIGH));
}

// ============================================================
// 12. DHT11
// ============================================================

void readTemperatureAndHumiditySensor()
{
  float measuredTemperature = dht.readTemperature();
  float measuredHumidity = dht.readHumidity();

  if (isnan(measuredTemperature) || isnan(measuredHumidity))
  {
    Serial.println("[DHT] Read failed -> keep last valid value");
    return;
  }

  temperature = measuredTemperature;
  humidity = measuredHumidity;

  if (temperature >= TEMP_DANGER)
  {
    temperatureLevel = DANGER;
  }
  else if (temperature >= TEMP_WARNING)
  {
    temperatureLevel = WARNING;
  }
  else
  {
    temperatureLevel = SAFE;
  }
}

// ============================================================
// 13. MQ-2
// ============================================================

void readGasSensor()
{
  long totalGasReading = 0;

  for (int i = 0; i < 5; i++)
  {
    gasRaw = analogRead(MQ2_PIN);
    totalGasReading += gasRaw;
    delay(20);
  }

  gasAverage = totalGasReading / 5;

  if (gasAverage >= GAS_DANGER)
  {
    gasLevel = DANGER;
  }
  else if (gasAverage >= GAS_WARNING)
  {
    gasLevel = WARNING;
  }
  else
  {
    gasLevel = SAFE;
  }
}

// ============================================================
// 14. JSN-SR04T
// ============================================================

float readDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long echoDurationMicroseconds = pulseIn(ECHO_PIN, HIGH, 30000);

  if (echoDurationMicroseconds == 0)
  {
    return -1.0;
  }

  return echoDurationMicroseconds * 0.0343 / 2.0;
}

void readWaterLevelSensor()
{
  float measuredDistance = readDistance();

  // No echo means unknown, not SAFE. Keep the previous local safety level,
  // while telemetry publishes null for the failed measurement.
  if (measuredDistance < 0)
  {
    waterValid = false;

    Serial.println("[WATER] No echo -> keep last valid safety level");
    return;
  }

  // Blind zone: closer than 23 cm cannot be measured reliably.
  // With a 100 cm installation height this already corresponds to
  // water >= 77 cm, which belongs to DANGER.
  if (measuredDistance < MIN_VALID_DISTANCE_CM)
  {
    Serial.print("[WATER] Too close: ");
    Serial.print(measuredDistance);
    Serial.println(" cm -> clamp to 23 cm");

    measuredDistance = MIN_VALID_DISTANCE_CM;
  }

  distanceCm = measuredDistance;
  waterLevelCm = SENSOR_HEIGHT_CM - distanceCm;

  if (waterLevelCm < 0.0)
  {
    waterLevelCm = 0.0;
  }

  if (waterLevelCm > MAX_WATER_LEVEL_CM)
  {
    waterLevelCm = MAX_WATER_LEVEL_CM;
  }

  waterPercent = (waterLevelCm / SENSOR_HEIGHT_CM) * 100.0;

  waterValid = true;

  if (waterLevelCm >= WATER_DANGER_CM)
  {
    waterLevel = DANGER;
  }
  else if (waterLevelCm >= WATER_WARNING_CM)
  {
    waterLevel = WARNING;
  }
  else
  {
    waterLevel = SAFE;
  }
}

// ============================================================
// 15. MOTION THROUGH MQTT + BUZZER COMMAND
// ============================================================

void publishBuzzerState()
{
  if (!mqttClient.connected())
  {
    return;
  }

  mqttClient.publish(TOPIC_BUZZER_STATE, buzzerOn ? "ON" : "OFF", true);
}

void updateBuzzer();

void handleBuzzerCommand(String message)
{
  message.trim();
  message.toUpperCase();

  // OFF means: mute only the CURRENT DANGER event.
  // This command never changes systemLevel or the warning LEDs.
  if (message == "OFF")
  {
    if (isDangerAlarmActive())
    {
      buzzerMuted = true;

      Serial.println("[BUZZER] Current DANGER alarm muted by user");
    }
    else
    {
      // Do NOT create a persistent manual-off while SAFE/WARNING.
      buzzerMuted = false;

      Serial.println("[BUZZER] OFF received with no active DANGER -> no persistent mute");
    }

    updateBuzzer();
    return;
  }

  // ON means: cancel the mute / acknowledge override.
  // It does NOT force the buzzer ON while the system is SAFE.
  // This command also never changes systemLevel or the warning LEDs.
  if (message == "ON")
  {
    buzzerMuted = false;

    Serial.println("[BUZZER] Current alarm mute cancelled");

    updateBuzzer();
    return;
  }

  Serial.print("[BUZZER] Unknown command: ");
  Serial.println(message);
}

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  String topicText = String(topic);
  String message = "";

  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  message.trim();

  // C3 motion state.
  if (topicText == TOPIC_MOTION)
  {
    motionLevel = textToLevel(message);
    lastMotionUpdate = millis();

    Serial.print("[MQTT] Motion -> ");
    Serial.println(levelToText(motionLevel));
    return;
  }

  // Web/backend buzzer command.
  if (topicText == TOPIC_BUZZER_COMMAND)
  {
    Serial.print("[MQTT] Buzzer command -> ");
    Serial.println(message);

    handleBuzzerCommand(message);
    return;
  }
}

void checkMotionTimeout()
{
  if (lastMotionUpdate == 0)
  {
    return;
  }

  if (millis() - lastMotionUpdate > MOTION_TIMEOUT_MS)
  {
    motionLevel = SAFE;
    lastMotionUpdate = 0;

    Serial.println("[MOTION] No recent F7 data -> SAFE");
  }
}

// ============================================================
// 16. SYSTEM + LED
// ============================================================

void updateSystemLevel()
{
  systemLevel = SAFE;

  if (temperatureLevel > systemLevel)
  {
    systemLevel = temperatureLevel;
  }

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

void updateStatusLights()
{
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN, LOW);

  if (systemLevel == SAFE)
  {
    digitalWrite(LED_GREEN_PIN, HIGH);
  }
  else if (systemLevel == WARNING)
  {
    digitalWrite(LED_YELLOW_PIN, HIGH);
  }
  else
  {
    digitalWrite(LED_RED_PIN, HIGH);
  }
}

// ============================================================
// 17. FINAL BUZZER STATE MACHINE
// ============================================================

void updateBuzzer()
{
  /*
   * Alarm-event state machine:
   *
   * SAFE
   *   -> clear old mute
   *   -> buzzer OFF
   *
   * WARNING
   *   -> buzzer OFF
   *   -> keep existing mute until SAFE, because the same event
   *      may return to DANGER without becoming normal first.
   *
   * DANGER + !buzzerMuted
   *   -> buzzer ON
   *
   * DANGER + buzzerMuted
   *   -> buzzer OFF
   */

  if (systemLevel == SAFE && buzzerMuted)
  {
    buzzerMuted = false;

    Serial.println("[BUZZER] System returned SAFE -> old alarm mute cleared");
  }

  bool shouldSound = ENABLE_BUZZER && isDangerAlarmActive() && !buzzerMuted;

  buzzerOn = shouldSound;

  writeBuzzerHardware(buzzerOn);

  if (buzzerOn != previousBuzzerOn)
  {
    previousBuzzerOn = buzzerOn;

    Serial.print("[BUZZER] Actual state -> ");
    Serial.println(buzzerOn ? "ON" : "OFF");

    publishBuzzerState();
  }
}

// ============================================================
// 18. WIFI
// ============================================================

void startWiFi()
{
  Serial.println();
  Serial.print("[WiFi] Connecting to HOME: ");
  Serial.println(HOME_WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFi.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
}

void maintainWiFi()
{
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected)
  {
    if (!homeWiFiWasConnected)
    {
      homeWiFiWasConnected = true;

      Serial.println();
      Serial.println("[WiFi] HOME CONNECTED");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
    }

    return;
  }

  if (homeWiFiWasConnected)
  {
    homeWiFiWasConnected = false;
    mqttClient.disconnect();

    Serial.println("[WiFi] HOME LOST");
  }

  if (millis() - lastNetworkStatusLog >= NETWORK_LOG_INTERVAL_MS)
  {
    lastNetworkStatusLog = millis();
    Serial.println("[WiFi] Waiting for Home WiFi...");
  }
}

// ============================================================
// 19. MQTT
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

  if (millis() - lastMQTTRetry < MQTT_RETRY_MS)
  {
    return;
  }

  lastMQTTRetry = millis();

  char clientId[48];

  snprintf(clientId, sizeof(clientId), "%s-%04X", DEVICE_ID,
           (uint16_t)(ESP.getEfuseMac() & 0xFFFF));

  Serial.print("[MQTT] Connecting to ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);

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

  bool motionSubscribed = mqttClient.subscribe(TOPIC_MOTION);

  bool buzzerSubscribed = mqttClient.subscribe(TOPIC_BUZZER_COMMAND, 1);

  Serial.print("[MQTT] Motion subscribe: ");
  Serial.println(motionSubscribed ? "OK" : "FAILED");

  Serial.print("[MQTT] Buzzer subscribe: ");
  Serial.println(buzzerSubscribed ? "OK" : "FAILED");

  publishBuzzerState();
}

// ============================================================
// 20. TELEMETRY
// ============================================================

void publishTelemetry()
{
  if (!mqttClient.connected())
  {
    return;
  }

  String telemetryJson = "{";

  telemetryJson += "\"deviceId\":\"";
  telemetryJson += DEVICE_ID;
  telemetryJson += "\",";

  telemetryJson += "\"temperature\":";
  telemetryJson += String(temperature, 1);
  telemetryJson += ",";

  telemetryJson += "\"humidity\":";
  telemetryJson += String(humidity, 1);
  telemetryJson += ",";

  telemetryJson += "\"gas\":";
  telemetryJson += String(gasAverage);
  telemetryJson += ",";

  // IMPORTANT:
  // If JSN-SR04T is invalid, publish JSON null instead of -1 / fake 0.
  // Backend schemas should use Optional/nullable fields.
  telemetryJson += "\"distanceCm\":";

  if (waterValid)
  {
    telemetryJson += String(distanceCm, 1);
  }
  else
  {
    telemetryJson += "null";
  }

  telemetryJson += ",";

  telemetryJson += "\"waterLevelCm\":";

  if (waterValid)
  {
    telemetryJson += String(waterLevelCm, 1);
  }
  else
  {
    telemetryJson += "null";
  }

  telemetryJson += ",";

  telemetryJson += "\"motion\":\"";
  telemetryJson += levelToText(motionLevel);
  telemetryJson += "\",";

  telemetryJson += "\"system\":\"";
  telemetryJson += levelToText(systemLevel);
  telemetryJson += "\",";

  // Actual hardware state.
  telemetryJson += "\"buzzer\":";
  telemetryJson += buzzerOn ? "true" : "false";
  telemetryJson += ",";

  // Human acknowledgment state for current alarm event.
  telemetryJson += "\"buzzerMuted\":";
  telemetryJson += buzzerMuted ? "true" : "false";

  telemetryJson += "}";

  Serial.print("[MQTT] Telemetry -> ");
  Serial.println(telemetryJson);

  mqttClient.publish(TOPIC_TELEMETRY, telemetryJson.c_str());
}

// ============================================================
// 21. SERIAL MONITOR
// ============================================================

void printSystemStatus()
{
  Serial.println();
  Serial.println("================================");

  Serial.print("Temperature : ");
  Serial.print(temperature);
  Serial.print(" C -> ");
  Serial.println(levelToText(temperatureLevel));

  Serial.print("Humidity    : ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("MQ-2 raw    : ");
  Serial.print(gasRaw);
  Serial.println(" ADC");

  Serial.print("MQ-2 avg    : ");
  Serial.print(gasAverage);
  Serial.print(" ADC -> ");
  Serial.println(levelToText(gasLevel));

  Serial.print("Distance    : ");

  if (!waterValid)
  {
    Serial.println("INVALID / NO ECHO");
  }
  else
  {
    Serial.print(distanceCm);
    Serial.println(" cm");
  }

  Serial.print("Water       : ");

  if (!waterValid)
  {
    Serial.print("UNKNOWN -> keep ");
    Serial.println(levelToText(waterLevel));
  }
  else
  {
    Serial.print(waterLevelCm);
    Serial.print(" cm (");
    Serial.print(waterPercent);
    Serial.print("%) -> ");
    Serial.println(levelToText(waterLevel));
  }

  Serial.print("Motion      : ");
  Serial.println(levelToText(motionLevel));

  Serial.print("SYSTEM      : ");
  Serial.println(levelToText(systemLevel));

  Serial.print("BUZZER      : ");
  Serial.println(buzzerOn ? "ON" : "OFF");

  Serial.print("MUTED       : ");
  Serial.println(buzzerMuted ? "YES" : "NO");

  Serial.print("Alarm state : ");

  if (systemLevel == DANGER && buzzerMuted)
  {
    Serial.println("DANGER - ACKNOWLEDGED/MUTED");
  }
  else if (systemLevel == DANGER)
  {
    Serial.println("DANGER - SOUNDING");
  }
  else if (systemLevel == WARNING)
  {
    Serial.println("WARNING");
  }
  else
  {
    Serial.println("SAFE");
  }

  Serial.print("Cause       : ");

  if (systemLevel != DANGER)
  {
    Serial.println("NONE");
  }
  else
  {
    if (temperatureLevel == DANGER)
    {
      Serial.print("TEMPERATURE ");
    }

    if (gasLevel == DANGER)
    {
      Serial.print("GAS ");
    }

    if (waterLevel == DANGER)
    {
      Serial.print("WATER ");
    }

    if (motionLevel == DANGER)
    {
      Serial.print("MOTION ");
    }

    Serial.println();
  }

  Serial.println("NETWORK     : HOME WIFI + MQTT");

  Serial.print("WiFi        : ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");

  Serial.print("MQTT        : ");
  Serial.print(mqttStateToText(mqttClient.state()));
  Serial.print(" | state=");
  Serial.println(mqttClient.state());

  Serial.println("================================");
}

// ============================================================
// 22. SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);
  delay(1000);

  pinMode(MQ2_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);

  // Safe startup.
  buzzerOn = false;
  buzzerMuted = false;
  previousBuzzerOn = false;

  writeBuzzerHardware(false);

  digitalWrite(LED_GREEN_PIN, HIGH);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN, LOW);

  analogReadResolution(12);
  dht.begin();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  mqttClient.setCallback(mqttCallback);

  mqttClient.setBufferSize(768);
  mqttClient.setSocketTimeout(1);

  startWiFi();

  Serial.println();
  Serial.println("[MAIN] ESP32-S3 started");
  Serial.println("[MAIN] Alarm-event mute logic enabled");
}

// ============================================================
// 23. LOOP
// ============================================================

void loop()
{
  unsigned long currentTime = millis();

  // 1. Network.
  maintainWiFi();
  maintainMQTT();

  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  // 2. Reset stale F7 motion when MQTT updates stop.
  checkMotionTimeout();

  // 3. Local sensors every 2 seconds.
  if (currentTime - lastSensorRead >= SENSOR_INTERVAL_MS)
  {
    lastSensorRead = currentTime;

    readTemperatureAndHumiditySensor();
    readGasSensor();
    readWaterLevelSensor();

    updateSystemLevel();
    updateStatusLights();
    updateBuzzer();

    printSystemStatus();
  }

  // Motion can change between local sensor reads.
  updateSystemLevel();
  updateStatusLights();
  updateBuzzer();

  // 4. Telemetry every 2 seconds.
  if (currentTime - lastTelemetry >= TELEMETRY_INTERVAL_MS)
  {
    lastTelemetry = currentTime;
    publishTelemetry();
  }

  delay(10);
}
