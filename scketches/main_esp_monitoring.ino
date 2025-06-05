#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include "time.h"

// Wi-Fi settings
const char* ssid = "Bishko 2.4";
const char* password = "Home_007";
// NTP settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600; // Your GMT offset
const int daylightOffset_sec = 0; // No daylight saving time

// Server
const char* serverUrl = "https://bishko.pythonanywhere.com/send_data";

// Identifiers
const int workshop = 1;  // Workshop number
const int incubator = 3; // Incubator 1
const int cameraSHT1 = 1; // Camera number for SHT31 on channel 0 (incubator 1)
const int cameraSHT2 = 2; // Camera number for SHT31 on channel 1 (incubator 1)
const int cameraSHT3 = 3; // Camera number for SHT31 on channel 2 (incubator 1)

// Sensors
Adafruit_SHT31 sht31_1 = Adafruit_SHT31(); // SHT31 on channel 0
Adafruit_SHT31 sht31_2 = Adafruit_SHT31(); // SHT31 on channel 1
Adafruit_SHT31 sht31_3 = Adafruit_SHT31(); // SHT31 on channel 2

// Timers
unsigned long lastTempUpdate = 0;
unsigned long lastDbUpdate = 0;
const unsigned long tempInterval = 900000; // 15 minutes
const unsigned long dbInterval = 3600000; // 1 hour
unsigned int scheduledMinute = 32; // Minute to send data (e.g., 40 for XX:40)

// State variables
bool dataSentThisMinute = false; // Flag to track if data was sent in the current minute

// Helper function to select TCA channel
void selectTCAChannel(uint8_t channel) {
  if (channel > 7) return;
  Wire.beginTransmission(0x70); // TCA9548A address
  Wire.write(1 << channel);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  // Synchronize time with NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();

  // Initialize sensors on TCA9548 channels
  selectTCAChannel(0);
  if (!sht31_1.begin(0x44)) {
    Serial.println("SHT31 on channel 0 not detected.");
  }

  selectTCAChannel(1);
  if (!sht31_2.begin(0x44)) {
    Serial.println("SHT31 on channel 1 not detected.");
  }

  selectTCAChannel(2);
  if (!sht31_3.begin(0x44)) {
    Serial.println("SHT31 on channel 2 not detected.");
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Check Wi-Fi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nReconnected to WiFi.");
  }

  // Get current time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  // Send data at the scheduled minute each hour
  if (timeinfo.tm_min == scheduledMinute) {
    if (!dataSentThisMinute) {
      Serial.println("Sending data at scheduled minute and saving to database...");
      sendSensorData(true); // Send data and save to database
      dataSentThisMinute = true; // Mark as sent for this minute
    }
  } else {
    dataSentThisMinute = false; // Reset flag when minute changes
  }

  // Send temporary data every 15 minutes
  if (currentMillis - lastTempUpdate >= tempInterval) {
    lastTempUpdate = currentMillis;
    sendSensorData(false); // Send temporary data
  }

  // Send data to DB every 1 hour
  if (currentMillis - lastDbUpdate >= dbInterval) {
    lastDbUpdate = currentMillis;
    sendSensorData(true); // Send data to DB
  }
}

void sendSensorData(bool saveToDatabase) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Channel 0: SHT31 Sensor
    selectTCAChannel(0);
    float temp1 = sht31_1.readTemperature();
    float hum1 = sht31_1.readHumidity();

    String payload1 = "";
    if (!isnan(temp1) && !isnan(hum1)) {
      payload1 += String("[{\"sensor_type\":\"SHT31\",") +
                  "\"temperature\":" + temp1 + "," +
                  "\"humidity\":" + hum1 + "," +
                  "\"workshop\":" + workshop + "," +
                  "\"incubator\":" + incubator + "," +
                  "\"camera\":" + cameraSHT1 + "," +
                  "\"save_to_database\":" + (saveToDatabase ? "true" : "false") + "}]";

      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");
      int httpResponseCode1 = http.POST(payload1);
      if (httpResponseCode1 > 0) {
        Serial.printf("HTTP Response Code (Camera 1): %d\n", httpResponseCode1);
      } else {
        Serial.printf("Error on sending POST request (Camera 1): %s\n", http.errorToString(httpResponseCode1).c_str());
      }
      http.end();
    } else {
      Serial.println("No valid data from SHT31 (Camera 1).");
    }
    delay(10000); // Delay 10 seconds before sending the next sensor's data

    // Channel 1: SHT31 Sensor
    selectTCAChannel(1);
    float temp2 = sht31_2.readTemperature();
    float hum2 = sht31_2.readHumidity();

    String payload2 = "";
    if (!isnan(temp2) && !isnan(hum2)) {
      payload2 += String("[{\"sensor_type\":\"SHT31\",") +
                  "\"temperature\":" + temp2 + "," +
                  "\"humidity\":" + hum2 + "," +
                  "\"workshop\":" + workshop + "," +
                  "\"incubator\":" + incubator + "," +
                  "\"camera\":" + cameraSHT2 + "," +
                  "\"save_to_database\":" + (saveToDatabase ? "true" : "false") + "}]";

      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");
      int httpResponseCode2 = http.POST(payload2);
      if (httpResponseCode2 > 0) {
        Serial.printf("HTTP Response Code (Camera 2): %d\n", httpResponseCode2);
      } else {
        Serial.printf("Error on sending POST request (Camera 2): %s\n", http.errorToString(httpResponseCode2).c_str());
      }
      http.end();
    } else {
      Serial.println("No valid data from SHT31 (Camera 2).");
    }

    delay(10000); // Delay 10 seconds before sending the next sensor's data

    // Channel 2: SHT31 Sensor
    selectTCAChannel(2);
    float temp3 = sht31_3.readTemperature();
    float hum3 = sht31_3.readHumidity();

    String payload3 = "";
    if (!isnan(temp3) && !isnan(hum3)) {
      payload3 += String("[{\"sensor_type\":\"SHT31\",") +
                  "\"temperature\":" + temp3 + "," +
                  "\"humidity\":" + hum3 + "," +
                  "\"workshop\":" + workshop + "," +
                  "\"incubator\":" + incubator + "," +
                  "\"camera\":" + cameraSHT3 + "," +
                  "\"save_to_database\":" + (saveToDatabase ? "true" : "false") + "}]";

      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");
      int httpResponseCode3 = http.POST(payload3);
      if (httpResponseCode3 > 0) {
        Serial.printf("HTTP Response Code (Camera 3): %d\n", httpResponseCode3);
      } else {
        Serial.printf("Error on sending POST request (Camera 3): %s\n", http.errorToString(httpResponseCode3).c_str());
      }
      http.end();
    } else {
      Serial.println("No valid data from SHT31 (Camera 3).");
    }
  } else {
    Serial.println("WiFi not connected.");
  }
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

