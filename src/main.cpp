#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "SSD1306Wire.h"
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

// Initialize display with the correct pins
SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

// KlimaLogg specific settings
#define KLIMALOGG_FREQUENCY 868.0     // KlimaLogg frequency in MHz

// Create an instance of the rtl_433 decoder
rtl_433_ESP rtl_433;

// Message buffer for rtl_433
char messageBuffer[1024];

// Packet counter
int packetCounter = 0;
unsigned long lastPacketTime = 0;

// Callback function for rtl_433 data
void rtl_433_callback(char* message) {
  packetCounter++;
  lastPacketTime = millis();
  
  // Print the raw message
  Serial.print("RTL_433 message: ");
  Serial.println(message);
  
  // Display the message on the OLED
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "KlimaLogg Pro");
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 15, "Packet #" + String(packetCounter));
  
  // Simple parsing for demonstration
  String msg = String(message);
  
  // Display raw message (truncated if needed)
  String displayMsg = msg;
  if (displayMsg.length() > 60) {
    displayMsg = displayMsg.substring(0, 57) + "...";
  }
  display.drawStringMaxWidth(0, 25, 128, displayMsg);
  
  display.display();
  
  // Flash LED to indicate packet received
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

void setup() {
  // Initialize serial
  Serial.begin(115200);
  delay(100);
  
  Serial.println("KlimaLogg Pro Receiver using rtl_433_ESP");
  
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
  
  // Initialize SPI for the radio
  SPI.begin(SCK, MISO, MOSI, SS);
  
  // Initialize rtl_433_ESP
  Serial.println("Initializing rtl_433_ESP...");
  
  // Initialize the receiver on the DIO0 pin with the KlimaLogg frequency
  rtl_433.initReceiver(DI0, KLIMALOGG_FREQUENCY);
  
  // Set the callback function with message buffer
  rtl_433.setCallback(rtl_433_callback, messageBuffer, sizeof(messageBuffer));
  
  // Enable the receiver
  rtl_433.enableReceiver();
  
  Serial.println("rtl_433_ESP initialized and ready to receive");
  
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 10, "KlimaLogg Pro");
  display.drawString(64, 30, "Ready to receive");
  display.display();
  
  Serial.println("Setup complete");
}

void loop() {
  static unsigned long lastUpdate = 0;
  static int counter = 0;
  
  // Process any incoming data
  rtl_433.loop();
  
  // Update display every second with uptime and packet count
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    counter++;
    
    // Only update if no packet was recently received
    if (millis() - lastPacketTime > 2000) {
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 0, "KlimaLogg Pro");
      display.drawString(64, 15, "Listening...");
      
      // Show packet counter
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 30, "Packets: " + String(packetCounter));
      
      // Show uptime
      display.drawString(0, 40, "Uptime: " + String(counter) + "s");
      
      display.display();
    }
    
    // Toggle LED
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    Serial.println("Running for " + String(counter) + " seconds, Packets: " + String(packetCounter));
  }
}
