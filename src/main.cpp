#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <rtl_433_ESP.h>

// Built-in LED pin for TTGO LoRa32
#define LED_PIN 25

// Undefine the board's default pin definitions to avoid warnings
#undef OLED_SDA
#undef OLED_SCL
#undef OLED_RST

// Define our custom pin configuration for v2.0/v2.1
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST -1  // No reset pin needed

// LoRa pins for TTGO LoRa32
#define SCK     5    // GPIO5  -- SX1278's SCK
#define MISO    19   // GPIO19 -- SX1278's MISO
#define MOSI    27   // GPIO27 -- SX1278's MOSI
#define SS      18   // GPIO18 -- SX1278's CS
#define RST     23   // GPIO23 -- SX1278's RESET
#define DI0     26   // GPIO26 -- SX1278's IRQ(Interrupt Request)

// Define RF module parameters if not defined in build flags
#ifndef RF_MODULE_RECEIVER_GPIO
#define RF_MODULE_RECEIVER_GPIO DI0
#endif

#ifndef RF_MODULE_FREQUENCY
#define RF_MODULE_FREQUENCY 868.33
#endif

// Define DIO1 pin if not defined in build flags
#ifndef RF_MODULE_DIO1
#define RF_MODULE_DIO1 -1  // Set to -1 if not used or connect to appropriate pin
#endif

// Initialize display with the correct pins
SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

#define JSON_MSG_BUFFER 512
char messageBuffer[JSON_MSG_BUFFER];

rtl_433_ESP rf;
int count = 0;

// Callback function to process decoded messages
void rtl_433_Callback(char* message) {
  DynamicJsonDocument jsonDocument(1024); // Use DynamicJsonDocument with a size
  DeserializationError error = deserializeJson(jsonDocument, message);
  
  if (error) {
    Log.error(F("deserializeJson() failed: %s" CR), error.c_str());
    return;
  }
  
  // Check if the message is from a KlimaLogg
  const char* protocol = jsonDocument["protocol"];
  if (protocol && strstr(protocol, "KlimaLogg") != NULL) {
    // Process KlimaLogg data
    Log.notice(F("KlimaLogg data received!"));
    
    // Update display with KlimaLogg data
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, "KlimaLogg Pro");
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 15, "Packet #" + String(count));
    
    // Display temperature if available
    if (jsonDocument.containsKey("temperature_C")) {
      float temp = jsonDocument["temperature_C"];
      display.drawString(0, 25, "Temp: " + String(temp) + "°C");
    }
    
    // Display humidity if available
    if (jsonDocument.containsKey("humidity")) {
      int humidity = jsonDocument["humidity"];
      display.drawString(0, 35, "Humidity: " + String(humidity) + "%");
    }
    
    display.display();
    
    // Flash LED to indicate packet received
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }
  
  // Log all JSON data
  String jsonString;
  serializeJson(jsonDocument, jsonString);
  Log.notice(F("Received message: %s" CR), jsonString.c_str());
  count++;
}

void setup() {
  // Initialize serial
  Serial.begin(115200);
  delay(1000);
  
  Log.begin(LOG_LEVEL_TRACE, &Serial);
  Log.notice(F("KlimaLogg Receiver starting" CR));
  
  // Set up LED pin
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize I2C
  Wire.begin(OLED_SDA, OLED_SCL);
  
  // Initialize display
  Log.notice(F("Initializing display" CR));
  if (!display.init()) {
    Log.error(F("Display initialization failed!" CR));
  } else {
    Log.notice(F("Display initialized successfully" CR));
    
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 10, "KlimaLogg Pro");
    display.drawString(64, 30, "Receiver");
    display.display();
  }
  
  // Initialize SPI for the radio
  SPI.begin(SCK, MISO, MOSI, SS);
  
  // Configure FSK reception for KlimaLogg
  Log.notice(F("Initializing rtl_433_ESP..." CR));
  rf.initReceiver(RF_MODULE_RECEIVER_GPIO, RF_MODULE_FREQUENCY);
  rf.setCallback(rtl_433_Callback, messageBuffer, JSON_MSG_BUFFER);
  rf.enableReceiver();
  Log.notice(F("Receiver initialized, waiting for KlimaLogg signals" CR));
  rf.getModuleStatus();
  
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 10, "KlimaLogg Pro");
  display.drawString(64, 30, "Ready to receive");
  display.display();
  
  Log.notice(F("Setup complete" CR));
}

void loop() {
  static unsigned long lastUpdate = 0;
  static int counter = 0;
  
  // Process any incoming data
  rf.loop();
  
  // Update display every second with uptime and packet count
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    counter++;
    
    // Only update if no packet was recently received
    if (millis() - lastUpdate > 2000) {
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 0, "KlimaLogg Pro");
      display.drawString(64, 15, "Listening...");
      
      // Show packet counter
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 30, "Packets: " + String(count));
      
      // Show uptime
      display.drawString(0, 40, "Uptime: " + String(counter) + "s");
      
      display.display();
    }
    
    // Toggle LED
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    Log.verbose(F("Running for %d seconds, Packets: %d" CR), counter, count);
  }
}
