#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <RadioLib.h>

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

// Initialize display with the correct pins
SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

// Initialize LoRa module
SX1278 radio = new Module(SS, DI0, RST, -1);

// KlimaLogg specific settings
#define FREQUENCY      868.250
#define BANDWIDTH      125.0
#define SPREADING_FACTOR 7
#define CODING_RATE    5
#define SYNC_WORD      0x12
#define POWER          10
#define CURRENT_LIMIT  100
#define PREAMBLE_LENGTH 8
#define GAIN           0

// LoRa interrupt handler - moved before setup() to fix the build error
volatile bool receivedFlag = false;
volatile bool enableInterrupt = true;

void handleLoRaInterrupt() {
  if(!enableInterrupt) {
    return;
  }
  receivedFlag = true;
}

void setup() {
  // Initialize serial
  Serial.begin(115200);
  delay(100);
  
  Serial.println("KlimaLogg Pro Receiver");
  
  // Set up LED pin
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize I2C
  Wire.begin(OLED_SDA, OLED_SCL);
  
  // Initialize display
  Serial.println("Initializing display");
  if (!display.init()) {
    Serial.println("Display initialization failed!");
  } else {
    Serial.println("Display initialized successfully");
    
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 10, "KlimaLogg Pro");
    display.drawString(64, 30, "Receiver");
    display.display();
  }
  
  // Initialize LoRa
  Serial.println("Initializing LoRa module...");
  int state = radio.begin(FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE, SYNC_WORD, POWER, PREAMBLE_LENGTH, GAIN);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa module initialized successfully!");
    display.clear();
    display.drawString(64, 10, "KlimaLogg Pro");
    display.drawString(64, 30, "LoRa Ready");
    display.display();
  } else {
    Serial.print("LoRa initialization failed, code: ");
    Serial.println(state);
    display.clear();
    display.drawString(64, 10, "KlimaLogg Pro");
    display.drawString(64, 30, "LoRa Failed!");
    display.drawString(64, 50, "Error: " + String(state));
    display.display();
  }
  
  // Set up LoRa for receiving
  radio.setDio0Action(handleLoRaInterrupt, RISING); // Added RISING as the second parameter
  state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa receiver started!");
  } else {
    Serial.print("Failed to start LoRa receiver, code: ");
    Serial.println(state);
  }
  
  Serial.println("Setup complete");
}

void loop() {
  static unsigned long lastUpdate = 0;
  static int counter = 0;
  static int packetCounter = 0;
  static long lastRssiCheck = 0;
  static float rssi = -120.0; // Default low RSSI value
  
  // Check RSSI every 200ms
  if (millis() - lastRssiCheck >= 200) {
    lastRssiCheck = millis();
    rssi = radio.getRSSI();
  }
  
  // Update display every second
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    counter++;
    
    // Update display with uptime and RSSI
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, "KlimaLogg Pro");
    display.drawString(64, 15, "Listening...");
    
    // Show RSSI value
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 30, "RSSI: " + String(rssi, 1) + " dBm");
    
    // Show packet counter
    display.drawString(0, 40, "Packets: " + String(packetCounter));
    
    // Show uptime
    display.drawString(0, 50, "Uptime: " + String(counter) + "s");
    display.display();
    
    // Toggle LED
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    Serial.println("Running for " + String(counter) + " seconds, RSSI: " + String(rssi) + " dBm");
  }
  
  // Check if LoRa packet received
  if(receivedFlag) {
    // Disable interrupt during processing
    enableInterrupt = false;
    receivedFlag = false;
    
    // Read received data
    String receivedData;
    int state = radio.readData(receivedData);
    
    if (state == RADIOLIB_ERR_NONE) {
      // Packet received successfully
      packetCounter++;
      
      // Convert binary data to hex for better display
      String hexData = "";
      for(size_t i = 0; i < receivedData.length(); i++) {
        char hex[4];
        sprintf(hex, "%02X ", (unsigned char)receivedData.charAt(i));
        hexData += hex;
      }
      
      Serial.println("Received packet #" + String(packetCounter) + "!");
      Serial.print("Data (HEX): ");
      Serial.println(hexData);
      Serial.print("Data Length: ");
      Serial.println(receivedData.length());
      Serial.print("RSSI: ");
      Serial.println(radio.getRSSI());
      
      // Display received data
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 0, "KlimaLogg Pro");
      display.drawString(64, 10, "Data Received! #" + String(packetCounter));
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 25, "RSSI: " + String(radio.getRSSI()) + " dBm");
      display.drawString(0, 35, "Length: " + String(receivedData.length()));
      
      // Display hex data (truncated if needed)
      String displayHex = hexData;
      if(displayHex.length() > 60) {
        displayHex = displayHex.substring(0, 57) + "...";
      }
      display.drawStringMaxWidth(0, 45, 128, displayHex);
      display.display();
      
      // Flash LED to indicate packet received
      for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
      }
    } else {
      // Error receiving packet
      Serial.print("LoRa reception failed, code: ");
      Serial.println(state);
    }
    
    // Re-enable receiver
    radio.startReceive();
    enableInterrupt = true;
  }
}