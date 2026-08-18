/*
 * ============================================================
 * ESP32-S3 MAIN
 * SIMPLE DEMO + RECEIVE MOTION FROM C3
 * ============================================================
 *
 * NORMAL MODE:
 *   C3 -> Home WiFi -> MQTT -> S3
 *
 * WHEN HOME WIFI IS LOST:
 *   C3 -> S3's private WiFi AP -> UDP -> S3
 *
 * The S3 always keeps its private AP ON.
 *
 * Local sensors:
 *   DHT11      -> temperature + humidity
 *   MQ-2       -> gas
 *   JSN-SR04T  -> water level
 *
 * Remote sensor:
 *   C3 + MPU6050 -> motion
 *
 * Overall level:
 *   highest of:
 *
 *   temperature
 *   gas
 *   water
 *   motion
 *
 * SAFE    -> GREEN LED
 * WARNING -> YELLOW LED
 * DANGER  -> RED LED + BUZZER
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

const char* MQTT_HOST = "192.168.1.100";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

const char* DEVICE_ID = "main-station-01";

const char* TOPIC_TELEMETRY =
  "disaster/main/telemetry";

const char* TOPIC_BUZZER_COMMAND =
  "disaster/main/command/buzzer";

const char* TOPIC_BUZZER_STATE =
  "disaster/main/state/buzzer";

const char* TOPIC_MOTION_STATE =
  "disaster/f7/state";

const char* TOPIC_STATUS =
  "disaster/main/status";

// ============================================================
// 2. DIRECT WIFI FOR C3 FALLBACK
// ============================================================

const char* DIRECT_AP_SSID =
  "DISASTER_MAIN_DIRECT";

const char* DIRECT_AP_PASSWORD =
  "12345678";

IPAddress DIRECT_AP_IP(
  192,
  168,
  4,
  1
);

IPAddress DIRECT_AP_GATEWAY(
  192,
  168,
  4,
  1
);

IPAddress DIRECT_AP_SUBNET(
  255,
  255,
  255,
  0
);

const int DIRECT_UDP_PORT = 4210;

// If no C3 update arrives for 4 seconds,
// forget the old motion state.
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

// ============================================================
// 4. LEVELS
// ============================================================

const int SAFE = 0;
const int WARNING = 1;
const int DANGER = 2;

// ============================================================
// 5. DEMO THRESHOLDS
// ============================================================

// Temperature
const float TEMP_WARNING = 35.0;
const float TEMP_DANGER = 40.0;

// MQ-2 ADC
const int GAS_WARNING = 700;
const int GAS_DANGER = 1000;

// Water
const float SENSOR_HEIGHT_CM = 100.0;
const float WATER_WARNING = 20.0;
const float WATER_DANGER = 40.0;

// ============================================================
// 6. OBJECTS
// ============================================================

DHT dht(
  DHT_PIN,
  DHT_TYPE
);

WiFiClient wifiClient;
PubSubClient mqttClient(
  wifiClient
);

WiFiUDP udp;

// ============================================================
// 7. LOCAL SENSOR VALUES
// ============================================================

float temperature = 0;
float humidity = 0;

int gasValue = 0;

float distanceCm = 0;
float waterLevelCm = 0;

bool dhtValid = false;
bool waterValid = false;

// ============================================================
// 8. LOCAL LEVELS
// ============================================================

int temperatureLevel = SAFE;
int gasLevel = SAFE;
int waterLevel = SAFE;

// ============================================================
// 9. MOTION FROM C3
// ============================================================

int motionLevel = SAFE;

float motionTilt = 0;
float motionVibration = 0;
float motionImpact = 0;

String motionSource = "NONE";

unsigned long lastMotionUpdate = 0;

// ============================================================
// 10. OVERALL SYSTEM
// ============================================================

int systemLevel = SAFE;

bool manualBuzzerOn = false;
bool buzzerOn = false;
bool previousBuzzerState = false;

// ============================================================
// 11. TIMERS
// ============================================================

unsigned long lastHomeWiFiRetry = 0;
unsigned long lastMQTTRetry = 0;
unsigned long lastLocalSensorRead = 0;
unsigned long lastTelemetry = 0;

bool homeWiFiWasConnected = false;

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

  if (
    text == "WARNING" ||
    text == "WARN"
  )
  {
    return WARNING;
  }

  return SAFE;
}

// ============================================================
// 13. READ DHT11
// ============================================================

void readDHT11()
{
  float newTemperature =
    dht.readTemperature();

  float newHumidity =
    dht.readHumidity();

  if (
    isnan(newTemperature) ||
    isnan(newHumidity)
  )
  {
    dhtValid = false;

    Serial.println(
      "[DHT] Read failed"
    );

    return;
  }

  temperature =
    newTemperature;

  humidity =
    newHumidity;

  dhtValid = true;
}

// ============================================================
// 14. READ MQ-2
// ============================================================

int readMQ2()
{
  /*
   * Read 5 times and use simple average.
   */

  long total = 0;

  for (int i = 0; i < 5; i++)
  {
    total +=
      analogRead(MQ2_PIN);

    delay(20);
  }

  return total / 5;
}

// ============================================================
// 15. READ ULTRASONIC
// ============================================================

float readDistance()
{
  digitalWrite(
    TRIG_PIN,
    LOW
  );

  delayMicroseconds(2);

  digitalWrite(
    TRIG_PIN,
    HIGH
  );

  delayMicroseconds(10);

  digitalWrite(
    TRIG_PIN,
    LOW
  );

  unsigned long duration =
    pulseIn(
      ECHO_PIN,
      HIGH,
      30000
    );

  if (duration == 0)
  {
    return -1;
  }

  float distance =
    duration * 0.0343 / 2.0;

  return distance;
}

void readWaterSensor()
{
  float newDistance =
    readDistance();

  if (newDistance < 0)
  {
    waterValid = false;

    Serial.println(
      "[WATER] No echo"
    );

    return;
  }

  distanceCm =
    newDistance;

  waterLevelCm =
    SENSOR_HEIGHT_CM -
    distanceCm;

  if (waterLevelCm < 0)
  {
    waterLevelCm = 0;
  }

  if (
    waterLevelCm >
    SENSOR_HEIGHT_CM
  )
  {
    waterLevelCm =
      SENSOR_HEIGHT_CM;
  }

  waterValid = true;
}

// ============================================================
// 16. CLASSIFY LOCAL SENSOR LEVELS
// ============================================================

int checkTemperatureLevel()
{
  if (
    temperature >=
    TEMP_DANGER
  )
  {
    return DANGER;
  }

  if (
    temperature >=
    TEMP_WARNING
  )
  {
    return WARNING;
  }

  return SAFE;
}

int checkGasLevel()
{
  if (
    gasValue >=
    GAS_DANGER
  )
  {
    return DANGER;
  }

  if (
    gasValue >=
    GAS_WARNING
  )
  {
    return WARNING;
  }

  return SAFE;
}

int checkWaterLevel()
{
  if (
    waterLevelCm >=
    WATER_DANGER
  )
  {
    return DANGER;
  }

  if (
    waterLevelCm >=
    WATER_WARNING
  )
  {
    return WARNING;
  }

  return SAFE;
}

void readAndClassifyLocalSensors()
{
  readDHT11();

  gasValue =
    readMQ2();

  readWaterSensor();

  if (dhtValid)
  {
    temperatureLevel =
      checkTemperatureLevel();
  }

  gasLevel =
    checkGasLevel();

  if (waterValid)
  {
    waterLevel =
      checkWaterLevel();
  }
}

// ============================================================
// 17. RECEIVE MOTION THROUGH MQTT
// ============================================================

void updateMotionFromMQTT(
  String payload
)
{
  motionLevel =
    textToLevel(payload);

  motionSource =
    "MQTT";

  lastMotionUpdate =
    millis();

  Serial.print(
    "[MQTT] Motion from C3: "
  );

  Serial.println(
    levelToText(motionLevel)
  );
}

// ============================================================
// 18. RECEIVE MOTION DIRECTLY THROUGH UDP
// ============================================================

void receiveDirectMotion()
{
  int packetSize =
    udp.parsePacket();

  if (packetSize <= 0)
  {
    return;
  }

  char buffer[128];

  int length =
    udp.read(
      buffer,
      sizeof(buffer) - 1
    );

  if (length <= 0)
  {
    return;
  }

  buffer[length] =
    '\0';

  String packet =
    String(buffer);

  /*
   * Expected:
   *
   * STATUS,TILT,VIBRATION,IMPACT
   *
   * Example:
   *
   * DANGER,25.4,3.10,4.50
   */

  int comma1 =
    packet.indexOf(',');

  int comma2 =
    packet.indexOf(
      ',',
      comma1 + 1
    );

  int comma3 =
    packet.indexOf(
      ',',
      comma2 + 1
    );

  // Invalid packet
  if (
    comma1 < 0 ||
    comma2 < 0 ||
    comma3 < 0
  )
  {
    Serial.print(
      "[DIRECT] Invalid packet: "
    );

    Serial.println(packet);

    return;
  }

  String statusText =
    packet.substring(
      0,
      comma1
    );

  String tiltText =
    packet.substring(
      comma1 + 1,
      comma2
    );

  String vibrationText =
    packet.substring(
      comma2 + 1,
      comma3
    );

  String impactText =
    packet.substring(
      comma3 + 1
    );

  motionLevel =
    textToLevel(statusText);

  motionTilt =
    tiltText.toFloat();

  motionVibration =
    vibrationText.toFloat();

  motionImpact =
    impactText.toFloat();

  motionSource =
    "DIRECT";

  lastMotionUpdate =
    millis();

  Serial.println();
  Serial.print(
    "[DIRECT] Motion from C3: "
  );

  Serial.print(
    levelToText(motionLevel)
  );

  Serial.print(
    " | tilt="
  );

  Serial.print(
    motionTilt
  );

  Serial.print(
    " | vibration="
  );

  Serial.print(
    motionVibration
  );

  Serial.print(
    " | impact="
  );

  Serial.println(
    motionImpact
  );
}

// ============================================================
// 19. MOTION TIMEOUT
// ============================================================

void checkMotionTimeout()
{
  if (lastMotionUpdate == 0)
  {
    return;
  }

  if (
    millis() - lastMotionUpdate <=
    MOTION_TIMEOUT_MS
  )
  {
    return;
  }

  motionLevel = SAFE;

  motionSource = "NONE";

  lastMotionUpdate = 0;

  Serial.println(
    "[MOTION] C3 data timeout."
  );
}

// ============================================================
// 20. OVERALL SYSTEM LEVEL
// ============================================================

void updateSystemLevel()
{
  /*
   * Take the highest level from:
   *
   * temperature
   * gas
   * water
   * motion
   */

  systemLevel =
    temperatureLevel;

  if (
    gasLevel >
    systemLevel
  )
  {
    systemLevel =
      gasLevel;
  }

  if (
    waterLevel >
    systemLevel
  )
  {
    systemLevel =
      waterLevel;
  }

  if (
    motionLevel >
    systemLevel
  )
  {
    systemLevel =
      motionLevel;
  }
}

// ============================================================
// 21. LED
// ============================================================

void turnOffAllLEDs()
{
  digitalWrite(
    LED_GREEN_PIN,
    LOW
  );

  digitalWrite(
    LED_YELLOW_PIN,
    LOW
  );

  digitalWrite(
    LED_RED_PIN,
    LOW
  );
}

void updateLED()
{
  turnOffAllLEDs();

  if (systemLevel == SAFE)
  {
    digitalWrite(
      LED_GREEN_PIN,
      HIGH
    );

    return;
  }

  if (
    systemLevel ==
    WARNING
  )
  {
    digitalWrite(
      LED_YELLOW_PIN,
      HIGH
    );

    return;
  }

  digitalWrite(
    LED_RED_PIN,
    HIGH
  );
}

// ============================================================
// 22. BUZZER
// ============================================================

void publishBuzzerState()
{
  if (!mqttClient.connected())
  {
    return;
  }

  if (buzzerOn)
  {
    mqttClient.publish(
      TOPIC_BUZZER_STATE,
      "ON",
      true
    );
  }
  else
  {
    mqttClient.publish(
      TOPIC_BUZZER_STATE,
      "OFF",
      true
    );
  }
}

void updateBuzzer()
{
  bool automaticDanger =
    (systemLevel == DANGER);

  if (
    automaticDanger ||
    manualBuzzerOn
  )
  {
    buzzerOn = true;
  }
  else
  {
    buzzerOn = false;
  }

  digitalWrite(
    BUZZER_PIN,
    buzzerOn ? HIGH : LOW
  );

  if (
    buzzerOn !=
    previousBuzzerState
  )
  {
    previousBuzzerState =
      buzzerOn;

    publishBuzzerState();
  }
}

// ============================================================
// 23. MQTT CALLBACK
// ============================================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
)
{
  String topicText =
    String(topic);

  String message = "";

  for (
    unsigned int i = 0;
    i < length;
    i++
  )
  {
    message +=
      (char)payload[i];
  }

  message.trim();

  // --------------------------------------------------------
  // Motion state from C3
  // --------------------------------------------------------

  if (
    topicText ==
    TOPIC_MOTION_STATE
  )
  {
    updateMotionFromMQTT(
      message
    );

    return;
  }

  // --------------------------------------------------------
  // Manual buzzer command
  // --------------------------------------------------------

  if (
    topicText ==
    TOPIC_BUZZER_COMMAND
  )
  {
    message.toUpperCase();

    if (message == "ON")
    {
      manualBuzzerOn = true;

      Serial.println(
        "[MQTT] Manual buzzer ON"
      );
    }
    else if (
      message == "OFF"
    )
    {
      manualBuzzerOn = false;

      Serial.println(
        "[MQTT] Manual buzzer OFF"
      );
    }
  }
}

// ============================================================
// 24. HOME WIFI
// ============================================================

void startHomeWiFi()
{
  /*
   * IMPORTANT:
   *
   * AP + STA simultaneously.
   *
   * AP  = private WiFi for C3 fallback.
   * STA = home WiFi for MQTT.
   */

  WiFi.mode(
    WIFI_AP_STA
  );

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  // --------------------------------------------------------
  // Start DIRECT AP first.
  // --------------------------------------------------------

  WiFi.softAPConfig(
    DIRECT_AP_IP,
    DIRECT_AP_GATEWAY,
    DIRECT_AP_SUBNET
  );

  bool apStarted =
    WiFi.softAP(
      DIRECT_AP_SSID,
      DIRECT_AP_PASSWORD
    );

  if (apStarted)
  {
    Serial.println(
      "[DIRECT] S3 fallback AP started."
    );

    Serial.print(
      "[DIRECT] SSID: "
    );

    Serial.println(
      DIRECT_AP_SSID
    );

    Serial.print(
      "[DIRECT] AP IP: "
    );

    Serial.println(
      WiFi.softAPIP()
    );
  }
  else
  {
    Serial.println(
      "[DIRECT] Failed to start AP."
    );
  }

  // UDP server listens for C3.
  udp.begin(
    DIRECT_UDP_PORT
  );

  Serial.print(
    "[DIRECT] UDP port: "
  );

  Serial.println(
    DIRECT_UDP_PORT
  );

  // --------------------------------------------------------
  // Then connect STA to home WiFi.
  // --------------------------------------------------------

  WiFi.begin(
    HOME_WIFI_SSID,
    HOME_WIFI_PASSWORD
  );

  Serial.print(
    "[WiFi] Connecting S3 to HOME WiFi: "
  );

  Serial.println(
    HOME_WIFI_SSID
  );
}

void maintainHomeWiFi()
{
  if (
    WiFi.status() ==
    WL_CONNECTED
  )
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

    return;
  }

  if (homeWiFiWasConnected)
  {
    homeWiFiWasConnected = false;
    Serial.println("[WiFi] HOME disconnected. Direct AP remains active.");
  }

  unsigned long now =
    millis();

  if (
    now - lastHomeWiFiRetry <
    10000
  )
  {
    return;
  }

  lastHomeWiFiRetry =
    now;

  Serial.println(
    "[WiFi] Retry HOME WiFi..."
  );

  /*
   * Only STA reconnects.
   * The S3 fallback AP stays active.
   */

  WiFi.reconnect();
}

// ============================================================
// 25. MQTT
// ============================================================

void maintainMQTT()
{
  if (
    WiFi.status() !=
    WL_CONNECTED
  )
  {
    return;
  }

  if (mqttClient.connected())
  {
    return;
  }

  unsigned long now =
    millis();

  if (
    now - lastMQTTRetry <
    5000
  )
  {
    return;
  }

  lastMQTTRetry =
    now;

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

  if (!success)
  {
    Serial.println(
      "[MQTT] Connection failed"
    );

    return;
  }

  Serial.println(
    "[MQTT] Connected"
  );

  mqttClient.publish(
    TOPIC_STATUS,
    "online",
    true
  );

  mqttClient.subscribe(
    TOPIC_BUZZER_COMMAND
  );

  mqttClient.subscribe(
    TOPIC_MOTION_STATE
  );

  publishBuzzerState();
}

// ============================================================
// 26. PUBLISH MAIN TELEMETRY
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

  data += "\"gas\":";
  data += String(gasValue);
  data += ",";

  data += "\"waterLevel\":";
  data += String(waterLevelCm, 1);
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

  if (buzzerOn)
  {
    data += "true";
  }
  else
  {
    data += "false";
  }

  data += "}";

  mqttClient.publish(
    TOPIC_TELEMETRY,
    data.c_str()
  );
}

// ============================================================
// 27. SERIAL MONITOR
// ============================================================

void printSystemData()
{
  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.print("Temperature : ");
  Serial.print(temperature);
  Serial.print(" C -> ");
  Serial.println(
    levelToText(temperatureLevel)
  );

  Serial.print("Humidity    : ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("MQ-2        : ");
  Serial.print(gasValue);
  Serial.print(" ADC -> ");
  Serial.println(
    levelToText(gasLevel)
  );

  Serial.print("Water       : ");
  Serial.print(waterLevelCm);
  Serial.print(" cm -> ");
  Serial.println(
    levelToText(waterLevel)
  );

  Serial.print("Motion      : ");
  Serial.print(
    levelToText(motionLevel)
  );

  Serial.print(" via ");
  Serial.println(
    motionSource
  );

  Serial.print("SYSTEM      : ");
  Serial.println(
    levelToText(systemLevel)
  );

  Serial.print("BUZZER      : ");

  if (buzzerOn)
  {
    Serial.println("ON");
  }
  else
  {
    Serial.println("OFF");
  }

  Serial.print("HOME WiFi   : ");

  if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
    Serial.println("CONNECTED");
  }
  else
  {
    Serial.println("DISCONNECTED");
  }

  Serial.print("DIRECT AP   : ");
  Serial.println(
    DIRECT_AP_SSID
  );

  Serial.print("DIRECT IP   : ");
  Serial.println(
    WiFi.softAPIP()
  );

  Serial.println(
    "================================"
  );
}

// ============================================================
// 28. SETUP
// ============================================================

void setup()
{
  Serial.begin(9600);

  // --------------------------------------------------------
  // PINS
  // --------------------------------------------------------

  pinMode(
    MQ2_PIN,
    INPUT
  );

  pinMode(
    TRIG_PIN,
    OUTPUT
  );

  pinMode(
    ECHO_PIN,
    INPUT
  );

  pinMode(
    BUZZER_PIN,
    OUTPUT
  );

  pinMode(
    LED_GREEN_PIN,
    OUTPUT
  );

  pinMode(
    LED_YELLOW_PIN,
    OUTPUT
  );

  pinMode(
    LED_RED_PIN,
    OUTPUT
  );

  // --------------------------------------------------------
  // SAFE STARTUP
  // --------------------------------------------------------

  digitalWrite(
    BUZZER_PIN,
    LOW
  );

  digitalWrite(
    LED_GREEN_PIN,
    HIGH
  );

  digitalWrite(
    LED_YELLOW_PIN,
    LOW
  );

  digitalWrite(
    LED_RED_PIN,
    LOW
  );

  // --------------------------------------------------------
  // SENSOR
  // --------------------------------------------------------

  analogReadResolution(12);

  dht.begin();

  // --------------------------------------------------------
  // MQTT
  // --------------------------------------------------------

  mqttClient.setServer(
    MQTT_HOST,
    MQTT_PORT
  );

  mqttClient.setCallback(
    mqttCallback
  );

  mqttClient.setBufferSize(
    1024
  );

  mqttClient.setSocketTimeout(
    1
  );

  // --------------------------------------------------------
  // AP + HOME WIFI
  // --------------------------------------------------------

  startHomeWiFi();

  Serial.println();
  Serial.println(
    "[MAIN] ESP32-S3 started."
  );

  Serial.println(
    "[MAIN] Direct C3 fallback is always available."
  );
}

// ============================================================
// 29. LOOP
// ============================================================

void loop()
{
  unsigned long now =
    millis();

  /*
   * 1. Network.
   */

  maintainHomeWiFi();

  maintainMQTT();

  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  /*
   * 2. Always listen for direct C3 packets.
   *
   * This works even when HOME WiFi / MQTT is down.
   */

  receiveDirectMotion();

  checkMotionTimeout();

  /*
   * 3. Read local sensors every 2 seconds.
   */

  if (
    now - lastLocalSensorRead >=
    2000
  )
  {
    lastLocalSensorRead = now;

    readAndClassifyLocalSensors();

    printSystemData();
  }

  /*
   * 4. Recalculate overall level all the time.
   */

  updateSystemLevel();

  updateLED();

  updateBuzzer();

  /*
   * 5. Send combined telemetry every 2 seconds
   * only when MQTT exists.
   */

  if (
    now - lastTelemetry >=
    2000
  )
  {
    lastTelemetry = now;

    publishTelemetry();
  }

  /*
   * Short delay only to reduce CPU spinning.
   */

  delay(10);
}
