#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include "SSD1306.h"

// Pin definitions for TTGO LoRa32 V1.3
#define SCK     5    // GPIO5  -- SX1278's SCK
#define MISO    19   // GPIO19 -- SX1278's MISO
#define MOSI    27   // GPIO27 -- SX1278's MOSI
#define SS      18   // GPIO18 -- SX1278's CS
#define RST     23   // GPIO23 -- SX1278's RESET
#define DI0     26   // GPIO26 -- SX1278's IRQ(Interrupt Request)
#define I2C_SDA 4    // OLED display
#define I2C_SCL 15   // OLED display

// KlimaLogg radio configuration
#define FREQUENCY      868.3 // EU frequency (use 915.0 for US)
#define BANDWIDTH      100.0 // Approximate based on KlimaLogg specs
#define SPREADING_FACTOR 7   // FSK equivalent for LoRa
#define CODING_RATE    5     // Medium redundancy
#define SYNC_WORD      0xAA  // From KlimaLogg protocol (preamble pattern)

// Module for RadioLib
SX1278 radio = new Module(SS, DI0, RST, 99); // pin 99 is unused

// Display
SSD1306 display(0x3c, I2C_SDA, I2C_SCL);

// KlimaLogg Protocol Constants
#define ACTION_GET_HISTORY 0x00
#define ACTION_REQ_SET_TIME 0x01
#define ACTION_REQ_SET_CONFIG 0x02
#define ACTION_GET_CONFIG 0x03
#define ACTION_GET_CURRENT 0x04
#define ACTION_SEND_CONFIG 0x20
#define ACTION_SEND_TIME 0x60

#define RESPONSE_DATA_WRITTEN 0x10
#define RESPONSE_GET_CONFIG 0x20
#define RESPONSE_GET_CURRENT 0x30
#define RESPONSE_GET_HISTORY 0x40
#define RESPONSE_REQUEST 0x50
#define RESPONSE_REQ_READ_HISTORY 0x50
#define RESPONSE_REQ_FIRST_CONFIG 0x51
#define RESPONSE_REQ_SET_CONFIG 0x52
#define RESPONSE_REQ_SET_TIME 0x53

// AX5051 register emulation
struct AX5051Registers {
  uint8_t FREQ3;
  uint8_t FREQ2;
  uint8_t FREQ1;
  uint8_t FREQ0;
  uint8_t MODULATION;
  uint8_t ENCODING;
  uint8_t FRAMING;
  // Add more registers as needed
};

// Current sensor data structure
struct SensorData {
  float temperature;
  float humidity;
  uint32_t timestamp;
  bool valid;
};

// Global variables
AX5051Registers registers;
SensorData sensors[9]; // 0 = base station, 1-8 = remote sensors
uint16_t deviceID = 0;
uint8_t loggerID = 0;
String receivedPacket = "";
int signalQuality = 0;

// Function declarations
void setupRadio();
void receiveKlimaLoggData();
void parseCurrentDataFrame(uint8_t* buffer, int length);
void parseHistoryDataFrame(uint8_t* buffer, int length);
void parseConfigFrame(uint8_t* buffer, int length);
void displayData();
float decodeTemperature(uint8_t* buffer, int start, bool startOnHiNibble);
uint8_t decodeHumidity(uint8_t* buffer, int start, bool startOnHiNibble);
uint8_t calculateChecksum(uint8_t* buffer, int start, int end);

void setup() {
  // Initialize serial
  Serial.begin(115200);
  while (!Serial);
  Serial.println("KlimaLogg Pro Receiver");
  
  // Initialize OLED display
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW);    // Reset OLED
  delay(50); 
  digitalWrite(16, HIGH);   // Turn on OLED
  
  display.init();
  display.flipScreenVertically();  
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "KlimaLogg Receiver");
  display.drawString(0, 20, "Initializing...");
  display.display();
  
  // Initialize RadioLib with SX1278 module
  setupRadio();
  
  // Initialize sensor data
  for (int i = 0; i < 9; i++) {
    sensors[i].temperature = -999.9;
    sensors[i].humidity = 0;
    sensors[i].timestamp = 0;
    sensors[i].valid = false;
  }
  
  display.clear();
  display.drawString(0, 0, "KlimaLogg Receiver");
  display.drawString(0, 20, "Waiting for data...");
  display.display();
}

void loop() {
  // Try to receive KlimaLogg data
  receiveKlimaLoggData();
  
  // Display the data every second
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 1000) {
    lastDisplayUpdate = millis();
    displayData();
  }
}

void setupRadio() {
  // Initialize SPI for radio communication
  SPI.begin(SCK, MISO, MOSI, SS);
  
  Serial.println("Initializing radio...");
  
  // Initialize radio with FSK modulation (KlimaLogg uses FSK)
  int state = radio.beginFSK(FREQUENCY, 9.6, 50.0, BANDWIDTH, 10, 16);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Radio initialized successfully!");
  } else {
    Serial.print("Failed to initialize radio, code: ");
    Serial.println(state);
    while (true); // Hang if radio init fails
  }
  
  // Configure for KlimaLogg communication
  radio.setFrequency(FREQUENCY);
  radio.setOOK(false); // Use FSK not OOK
  radio.setDataShaping(0); // No data shaping
  radio.setEncoding(0); // NRZ encoding
  
  // Set sync word (preamble pattern in KlimaLogg)
  uint8_t syncWord[] = {0xAA, 0xAA};
  radio.setSyncWord(syncWord, 2);
  
  // Enable listening
  radio.startReceive();
  
  Serial.println("Radio configured for KlimaLogg reception");
}

void receiveKlimaLoggData() {
  // Check if new data received
  if (radio.available()) {
    // Buffer for packet data (max KlimaLogg packet size is 273 bytes)
    uint8_t data[280] = {0};
    size_t dataLen = 0;
    
    // Read received data
    int state = radio.readData(data, dataLen);
    
    if (state == RADIOLIB_ERR_NONE) {
      // Reception successful
      signalQuality = radio.getRSSI() + 120; // Scale RSSI to 0-100 range approximately
      if (signalQuality > 100) signalQuality = 100;
      if (signalQuality < 0) signalQuality = 0;
      
      // Process the received data based on message type
      if (dataLen >= 4) { // Ensure packet has at least a header
        uint8_t responseType = data[3] & 0xF0;
        
        switch (responseType) {
          case RESPONSE_GET_CURRENT:
            Serial.println("Received current weather data");
            parseCurrentDataFrame(data, dataLen);
            break;
            
          case RESPONSE_GET_HISTORY:
            Serial.println("Received history data");
            parseHistoryDataFrame(data, dataLen);
            break;
            
          case RESPONSE_GET_CONFIG:
            Serial.println("Received config data");
            parseConfigFrame(data, dataLen);
            break;
            
          default:
            Serial.print("Unknown response type: 0x");
            Serial.println(responseType, HEX);
            break;
        }
      }
    } else {
      Serial.print("Reception failed, code: ");
      Serial.println(state);
    }
    
    // Restart reception
    radio.startReceive();
  }
}

void parseCurrentDataFrame(uint8_t* buffer, int length) {
  if (length < 230) { // Minimum length for current weather frame
    Serial.println("Current weather frame too short");
    return;
  }
  
  // Extract device ID and logger ID
  deviceID = (buffer[0] << 8) | buffer[1];
  loggerID = buffer[2];
  
  // Signal quality
  signalQuality = buffer[4] & 0x7F;
  
  // Example parsing for base station (sensor 0) temperature and humidity
  // Based on KlimaLogg protocol map for CurrentData
  sensors[0].temperature = decodeTemperature(buffer, 29, false);
  sensors[0].humidity = decodeHumidity(buffer, 17, true);
  sensors[0].timestamp = millis() / 1000; // Current time in seconds
  sensors[0].valid = true;
  
  // Parse additional sensors (1-8) as needed
  // For example:
  sensors[1].temperature = decodeTemperature(buffer, 53, false);
  sensors[1].humidity = decodeHumidity(buffer, 41, true);
  sensors[1].timestamp = millis() / 1000;
  sensors[1].valid = true;
  
  Serial.print("Base sensor - Temperature: ");
  Serial.print(sensors[0].temperature);
  Serial.print("°C, Humidity: ");
  Serial.print(sensors[0].humidity);
  Serial.println("%");
}

void parseHistoryDataFrame(uint8_t* buffer, int length) {
  // History data parsing would go here
  // This is more complex and would need more implementation
  Serial.println("History data parsing not implemented yet");
}

void parseConfigFrame(uint8_t* buffer, int length) {
  if (length < 125) { // Min length for config frame
    Serial.println("Config frame too short");
    return;
  }
  
  // Extract configuration data
  uint8_t settings = buffer[5];
  uint8_t timeZone = buffer[6];
  uint8_t historyInterval = buffer[7];
  
  Serial.print("Settings: 0x");
  Serial.println(settings, HEX);
  
  // Extract more config data as needed
}

void displayData() {
  display.clear();
  display.drawString(0, 0, "KlimaLogg Receiver");
  
  // Display signal quality
  display.drawString(0, 10, "Signal: " + String(signalQuality) + "%");
  
  // Display sensor data for sensors that have valid readings
  int yPos = 25;
  for (int i = 0; i < 9; i++) {
    if (sensors[i].valid) {
      String sensorName = (i == 0) ? "Base" : "Sensor " + String(i);
      String tempStr = String(sensors[i].temperature, 1) + "°C";
      String humStr = String(sensors[i].humidity) + "%";
      
      display.drawString(0, yPos, sensorName + ": " + tempStr + " " + humStr);
      yPos += 10;
      
      // Make sure we don't overflow the display
      if (yPos > 55) break;
    }
  }
  
  display.display();
}

float decodeTemperature(uint8_t* buffer, int start, bool startOnHiNibble) {
  // Implementation based on KlimaLogg's decoding
  float result = -999.9;
  
  // Check for error or overflow condition
  // This is simplified and would need more robust implementation
  
  if (startOnHiNibble) {
    uint16_t rawtemp = (buffer[start] >> 4) * 10
                     + (buffer[start + 0] & 0xF) * 1
                     + (buffer[start + 1] >> 4) * 0.1;
    result = rawtemp - 40.0; // Offset as per KlimaLogg
  } else {
    uint16_t rawtemp = (buffer[start] & 0xF) * 10
                     + (buffer[start + 1] >> 4) * 1
                     + (buffer[start + 1] & 0xF) * 0.1;
    result = rawtemp - 40.0; // Offset as per KlimaLogg
  }
  
  return result;
}

uint8_t decodeHumidity(uint8_t* buffer, int start, bool startOnHiNibble) {
  // Implementation based on KlimaLogg's decoding
  uint8_t result = 0;
  
  // Check for error or overflow condition
  // This is simplified and would need more robust implementation
  
  if (startOnHiNibble) {
    result = (buffer[start] >> 4) * 10 + (buffer[start] & 0xF);
  } else {
    result = (buffer[start] & 0xF) * 10 + (buffer[start + 1] >> 4);
  }
  
  return result;
}

uint8_t calculateChecksum(uint8_t* buffer, int start, int end) {
  uint16_t cs = 0;
  for (int i = start; i < end; i++) {
    cs += buffer[i];
  }
  return cs;
}