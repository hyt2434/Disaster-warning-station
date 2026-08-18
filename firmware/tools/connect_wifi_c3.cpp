#include <WiFi.h>
#include <PubSubClient.h>

const char* WIFI_SSID =
  "TEN_WIFI";

const char* WIFI_PASSWORD =
  "MAT_KHAU_WIFI";

// VẪN LÀ IP CỦA CÙNG LAPTOP
const char* MQTT_SERVER =
  "172.20.10.2";

const int MQTT_PORT = 1883;

const char* CLIENT_ID =
  "XIAO-F7";

const char* TEST_TOPIC =
  "disaster/f7/test";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


void connectWiFi() {

  Serial.print("Connecting WiFi");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi CONNECTED");

  Serial.print("F7 IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}


void connectMQTT() {

  while (!mqttClient.connected()) {

    Serial.print(
      "Connecting MQTT..."
    );

    if (
      mqttClient.connect(CLIENT_ID)
    ) {

      Serial.println("CONNECTED");

      mqttClient.publish(
        TEST_TOPIC,
        "Hello from F7"
      );

      Serial.println(
        "Published: Hello from F7"
      );

    } else {

      Serial.print(
        "FAILED, state = "
      );

      Serial.println(
        mqttClient.state()
      );

      delay(3000);
    }
  }
}


void setup() {

  Serial.begin(9600);

  delay(1000);

  connectWiFi();

  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  connectMQTT();
}


void loop() {

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    connectWiFi();
  }

  if (
    !mqttClient.connected()
  ) {

    connectMQTT();
  }

  mqttClient.loop();
}