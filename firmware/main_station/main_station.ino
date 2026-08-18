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
 *   Normal   : C3 -> Home WiFi -> MQTT -> S3
 *   Fallback : C3 -> S3 private AP -> UDP -> S3
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
 * Buzzer state   : disaster/main/state/buzzer payload: ON / OFF Motion state   : disaster/f7/state
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
#include <WiFiUdp.h>
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
// 2. FALLBACK AP + UDP
// ============================================================

const char* DIRECT_AP_SSID = "DISASTER_MAIN_DIRECT";

const char* DIRECT_AP_PASSWORD = "12345678";

const int DIRECT_UDP_PORT = 4210;

IPAddress DIRECT_AP_IP(192, 168, 4, 1);
IPAddress DIRECT_AP_GATEWAY(192, 168, 4, 1);
IPAddress DIRECT_AP_SUBNET(255, 255, 255, 0);

// ============================================================
// 3. PINS
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
// 4. LEVELS
// ============================================================

const int SAFE = 0;
const int WARNING = 1;
const int DANGER = 2;

// ============================================================
// 5. THRESHOLDS
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
// 6. TIMING
// ============================================================

const unsigned long SENSOR_INTERVAL_MS = 2000;
const unsigned long TELEMETRY_INTERVAL_MS = 2000;
const unsigned long MOTION_TIMEOUT_MS = 4000;
const unsigned long MQTT_RETRY_MS = 5000;
const unsigned long NETWORK_LOG_INTERVAL_MS = 5000;

// ============================================================
// 7. OBJECTS
// ============================================================

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiUDP udp;

// ============================================================
// 8. SENSOR VALUES + LEVELS
// ============================================================

float temperature = 0.0;
float humidity = 0.0;

int gasRaw = 0;
int gasAverage = 0;

float distanceCm = -1.0;
float waterLevelCm = 0.0;
float waterPercent = 0.0;

bool dhtValid = false;
bool waterValid = false;

int temperatureLevel = SAFE;
int gasLevel = SAFE;
int waterLevel = SAFE;
int motionLevel = SAFE;
int systemLevel = SAFE;

// ============================================================
// 9. MOTION FROM C3
// ============================================================

float motionTilt = 0.0;
float motionVibration = 0.0;
float motionImpact = 0.0;

String motionSource = "NONE";

unsigned long lastMotionUpdate = 0;

// ============================================================
// 10. BUZZER EVENT STATE
// ============================================================

bool buzzerOn = false;

// true = user acknowledged/muted ONLY the current alarm event.
// It is automatically cleared when systemLevel returns to SAFE.
bool buzzerMuted = false;

bool previousBuzzerOn = false;

// ============================================================
// 11. NETWORK / TIMERS
// ============================================================

bool homeWiFiWasConnected = false;

unsigned long lastSensorRead = 0;
unsigned long lastTelemetry = 0;
unsigned long lastMQTTRetry = 0;
unsigned long lastNetworkStatusLog = 0;

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

  // Accept NORMAL as SAFE.
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
// 13. DHT11
// ============================================================

void readTemperatureAndHumiditySensor()
{
  float measuredTemperature = dht.readTemperature();
  float measuredHumidity = dht.readHumidity();

  if (isnan(measuredTemperature) || isnan(measuredHumidity))
  {
    dhtValid = false;
    temperatureLevel = SAFE;

    Serial.println("[DHT] Read failed -> SAFE for demo");
    return;
  }

  temperature = measuredTemperature;
  humidity = measuredHumidity;
  dhtValid = true;

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
// 14. MQ-2
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
// 15. JSN-SR04T
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

  // No echo -> invalid measurement.
  // Internally clear water danger for demo, but telemetry publishes null
  // instead of a physically impossible negative distance.
  if (measuredDistance < 0)
  {
    waterValid = false;
    distanceCm = -1.0;
    waterLevelCm = 0.0;
    waterPercent = 0.0;
    waterLevel = SAFE;

    Serial.println("[WATER] No echo -> invalid / SAFE for demo");
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
// 16. MOTION THROUGH MQTT + BUZZER COMMAND
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
    motionSource = "MQTT";
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

// ============================================================
// 17. MOTION THROUGH UDP FALLBACK
// ============================================================

void receiveMotionThroughUdp()
{
  int packetSize = udp.parsePacket();

  if (packetSize <= 0)
  {
    return;
  }

  char buffer[128];
  int bytesRead = udp.read(buffer, sizeof(buffer) - 1);

  if (bytesRead <= 0)
  {
    return;
  }

  buffer[bytesRead] = '\0';

  String packet = String(buffer);
  packet.trim();

  // Simple packet: DANGER / WARNING / NORMAL / SAFE
  int comma1 = packet.indexOf(',');

  if (comma1 < 0)
  {
    motionLevel = textToLevel(packet);
    motionSource = "UDP";
    lastMotionUpdate = millis();

    Serial.print("[UDP] Motion -> ");
    Serial.println(levelToText(motionLevel));
    return;
  }

  // Full packet: STATUS,TILT,VIBRATION,IMPACT
  int comma2 = packet.indexOf(',', comma1 + 1);
  int comma3 = packet.indexOf(',', comma2 + 1);

  if (comma2 < 0 || comma3 < 0)
  {
    Serial.print("[UDP] Invalid motion packet: ");
    Serial.println(packet);
    return;
  }

  motionLevel = textToLevel(packet.substring(0, comma1));

  motionTilt = packet.substring(comma1 + 1, comma2).toFloat();

  motionVibration = packet.substring(comma2 + 1, comma3).toFloat();

  motionImpact = packet.substring(comma3 + 1).toFloat();

  motionSource = "UDP";
  lastMotionUpdate = millis();

  Serial.print("[UDP] Motion -> ");
  Serial.print(levelToText(motionLevel));
  Serial.print(" | tilt=");
  Serial.print(motionTilt);
  Serial.print(" | vibration=");
  Serial.print(motionVibration);
  Serial.print(" | impact=");
  Serial.println(motionImpact);
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
    motionSource = "NONE";
    lastMotionUpdate = 0;

    Serial.println("[MOTION] Timeout -> SAFE");
  }
}

// ============================================================
// 18. SYSTEM + LED
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
// 19. FINAL BUZZER STATE MACHINE
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
// 20. WIFI
// ============================================================

void startWiFi()
{
  Serial.println();
  Serial.println("[WiFi] Starting AP + STA...");

  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFi.softAPConfig(DIRECT_AP_IP, DIRECT_AP_GATEWAY, DIRECT_AP_SUBNET);

  bool accessPointStarted = WiFi.softAP(DIRECT_AP_SSID, DIRECT_AP_PASSWORD);

  if (accessPointStarted)
  {
    Serial.println("[DIRECT] Fallback AP started");
    Serial.print("[DIRECT] SSID: ");
    Serial.println(DIRECT_AP_SSID);
    Serial.print("[DIRECT] IP: ");
    Serial.println(WiFi.softAPIP());
  }
  else
  {
    Serial.println("[DIRECT] Failed to start fallback AP");
  }

  bool udpListenerStarted = udp.begin(DIRECT_UDP_PORT);

  Serial.print("[DIRECT] UDP: ");
  Serial.println(udpListenerStarted ? "OK" : "FAILED");

  Serial.print("[DIRECT] UDP port: ");
  Serial.println(DIRECT_UDP_PORT);

  Serial.println();
  Serial.print("[WiFi] Connecting HOME: ");
  Serial.println(HOME_WIFI_SSID);

  // Call only once. Auto reconnect handles temporary disconnects.
  WiFi.begin(HOME_WIFI_SSID, HOME_WIFI_PASSWORD);
}

void maintainWiFi()
{
  bool connected = WiFi.status() == WL_CONNECTED;

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

    Serial.print("[MQTT] Broker: ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);
  }

  if (!connected && homeWiFiWasConnected)
  {
    homeWiFiWasConnected = false;

    Serial.println();
    Serial.println("[WiFi] HOME LOST");
    Serial.println("[DIRECT] Fallback AP remains active");
  }

  unsigned long currentTime = millis();

  if (!connected && currentTime - lastNetworkStatusLog >= NETWORK_LOG_INTERVAL_MS)
  {
    lastNetworkStatusLog = currentTime;

    Serial.print("[WiFi] HOME status: ");
    Serial.print(wifiStatusToText(WiFi.status()));
    Serial.print(" | code=");
    Serial.println((int)WiFi.status());
  }

  // Do not repeatedly call WiFi.begin()/disconnect()/reconnect() here.
  // setAutoReconnect(true) is responsible for STA reconnection.
}

// ============================================================
// 21. MQTT
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

  bool buzzerSubscribed = mqttClient.subscribe(TOPIC_BUZZER_COMMAND);

  Serial.print("[MQTT] Motion subscribe: ");
  Serial.println(motionSubscribed ? "OK" : "FAILED");

  Serial.print("[MQTT] Buzzer subscribe: ");
  Serial.println(buzzerSubscribed ? "OK" : "FAILED");

  publishBuzzerState();
}

// ============================================================
// 22. TELEMETRY
// ============================================================

void publishTelemetry()
{
  if (!mqttClient.connected())
  {
    return;
  }

  String telemetryJson = "{";

  telemetryJson += "\"temperature\":";
  telemetryJson += String(temperature, 1);
  telemetryJson += ",";

  telemetryJson += "\"humidity\":";
  telemetryJson += String(humidity, 1);
  telemetryJson += ",";

  // Preserve the current backend field name: gas
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

  // Preserve the current backend field names: motion / system
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
// 23. SERIAL MONITOR
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
    Serial.println("UNKNOWN -> SAFE for demo");
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
  Serial.print(levelToText(motionLevel));
  Serial.print(" via ");
  Serial.println(motionSource);

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
    Serial.println("NORMAL");
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

  Serial.print("HOME WiFi   : ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");

  Serial.print("MQTT        : ");
  Serial.print(mqttStateToText(mqttClient.state()));
  Serial.print(" | state=");
  Serial.println(mqttClient.state());

  Serial.print("DIRECT AP   : ");
  Serial.println(DIRECT_AP_SSID);

  Serial.print("DIRECT IP   : ");
  Serial.println(WiFi.softAPIP());

  Serial.println("================================");
}

// ============================================================
// 24. SETUP
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
// 25. LOOP
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

  // 2. Motion fallback remains available even without HOME MQTT.
  receiveMotionThroughUdp();
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
