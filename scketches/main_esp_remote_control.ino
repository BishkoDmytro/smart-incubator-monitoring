#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <Adafruit_AHTX0.h>
#include "certificates.h"  


const char* ssid = "Redmi";
const char* password = "12345678";


const char* mqtt_server = "agiv9qkmj210r-ats.iot.us-east-1.amazonaws.com";
const char* mqtt_topic_data = "temp/data";
const char* mqtt_topic_control = "device/control";


#define FAN_PIN     26
#define HEATER_PIN  27


const char* updateStateURL = "https://bishko.pythonanywhere.com/update_state";


WiFiClientSecure mqttClient;
PubSubClient client(mqttClient);
Adafruit_AHTX0 aht;


float targetTemperature = 25.0;

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32_Client")) {
      Serial.println(" Connected!");
      client.subscribe(mqtt_topic_control);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Received message: ");
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  if (message.startsWith("{\"command\":")) {
    message.replace("{\"command\":", "");
    message.replace("}", "");
    targetTemperature = message.toFloat();
    Serial.print("New Target Temperature: ");
    Serial.println(targetTemperature);

    checkAndControlTemperature();
    sendTemperatureDataToServer();
    updateStateToServer();
  }
}

void controlTemperature(float currentTemperature) {
  Serial.print("Current Temp: ");
  Serial.println(currentTemperature);
  Serial.print("Target Temp: ");
  Serial.println(targetTemperature);

  if (currentTemperature < targetTemperature - 0.5) {
    digitalWrite(HEATER_PIN, HIGH);
    digitalWrite(FAN_PIN, LOW);
    Serial.println("Action: Heater ON, Fan OFF");
  } else if (currentTemperature > targetTemperature + 0.5) {
    digitalWrite(HEATER_PIN, LOW);
    digitalWrite(FAN_PIN, HIGH);
    Serial.println("Action: Heater OFF, Fan ON");
  } else {
    digitalWrite(HEATER_PIN, LOW);
    digitalWrite(FAN_PIN, LOW);
    Serial.println("Action: Heater OFF, Fan OFF");
  }
}

void sendTemperatureDataToServer() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  float temperature = temp.temperature;
  String heaterStatus = digitalRead(HEATER_PIN) ? "ON" : "OFF";
  String fanStatus = digitalRead(FAN_PIN) ? "ON" : "OFF";

  if (!isnan(temperature)) {
    char payload[150];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"ESP32\",\"temperature\":%.2f,\"heater\":\"%s\",\"fan\":\"%s\"}",
             temperature, heaterStatus.c_str(), fanStatus.c_str());
    client.publish(mqtt_topic_data, payload);
    Serial.println("Data sent to server (MQTT): " + String(payload));
  } else {
    Serial.println("Failed to read sensor data.");
  }
}

void updateStateToServer() {
  HTTPClient http;
  http.begin(updateStateURL);
  http.addHeader("Content-Type", "application/json");

  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  float temperature = temp.temperature;
  String heaterStatus = digitalRead(HEATER_PIN) ? "ON" : "OFF";
  String fanStatus = digitalRead(FAN_PIN) ? "ON" : "OFF";

  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"ESP32\",";
  jsonPayload += "\"temperature\":" + String(temperature, 2) + ",";
  jsonPayload += "\"heater\":\"" + heaterStatus + "\",";
  jsonPayload += "\"fan\":\"" + fanStatus + "\"";
  jsonPayload += "}";

  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode > 0) {
    Serial.print("State update HTTP response code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("State update HTTP error: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void checkAndControlTemperature() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  float temperature = temp.temperature;
  if (!isnan(temperature)) {
    controlTemperature(temperature);
  } else {
    Serial.println("Failed to read temperature for control.");
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA, SCL

  if (!aht.begin()) {
    Serial.println("Couldn't find AHT10 sensor!");
    while (1) delay(10);
  }

  pinMode(FAN_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);

  connectWiFi();

  mqttClient.setCACert(aws_root_ca);
  mqttClient.setCertificate(device_cert);
  mqttClient.setPrivateKey(private_key);

  client.setServer(mqtt_server, 8883);
  client.setCallback(callback);
  connectMQTT();

  sendTemperatureDataToServer();
  checkAndControlTemperature();
  updateStateToServer();
}

void loop() {
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  static unsigned long lastCheckTime = 0;
  static unsigned long lastSentTime = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastCheckTime >= 60000 || lastCheckTime == 0) {
    checkAndControlTemperature();
    lastCheckTime = currentMillis;
  }

  if (currentMillis - lastSentTime >= 120000 || lastSentTime == 0) {
    sendTemperatureDataToServer();
    updateStateToServer();
    lastSentTime = currentMillis;
  }

  delay(100);
}
