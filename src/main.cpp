#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>  
#include "SSD1306.h" 

// Debug flag to help with troubleshooting
#define DEBUG_STARTUP 1

#define SCK     5    // GPIO5  -- SX1278's SCK
#define MISO    19   // GPIO19 -- SX1278's MISO
#define MOSI    27   // GPIO27 -- SX1278's MOSI
#define SS      18   // GPIO18 -- SX1278's CS
#define RST     23   // GPIO23 -- SX1278's RESET
#define DI0     26   // GPIO26 -- SX1278's IRQ(Interrupt Request)

// Use the board's predefined OLED pins instead of redefining them
// OLED_RST is already defined as 16 in pins_arduino.h
// Let's also define the I2C pins based on the board definition
#define OLED_SDA 4
#define OLED_SCL 15

// Initialize display with the correct pins
SSD1306 display(0x3c, OLED_SDA, OLED_SCL);

void setup() {
  Serial.begin(115200);
  delay(50);  // Give serial a moment to initialize
  
  if (DEBUG_STARTUP) Serial.println("Starting KlimaLogg Pro Receiver");
  
  // Reset OLED with proper delay sequence
  if (DEBUG_STARTUP) Serial.println("Configuring OLED reset pin");
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);    // set reset pin low to reset OLED
  delay(100);                      // increased delay for more reliable reset
  digitalWrite(OLED_RST, HIGH);   // while OLED is running, must set reset pin high
  delay(100);                      // added delay after reset
  
  // Initialize display with more error handling
  if (DEBUG_STARTUP) Serial.println("Initializing display");
  Wire.begin(OLED_SDA, OLED_SCL);  // Explicitly initialize I2C with SDA=4, SCL=15
  
  bool displayInitialized = false;
  try {
    displayInitialized = display.init();
    if (DEBUG_STARTUP) Serial.println("Display init call completed");
  } catch (...) {
    Serial.println("Exception during display init");
  }
  
  if(!displayInitialized) {
    Serial.println("Display initialization failed!");
    // Try with a different address - some displays use 0x3d instead of 0x3c
    display = SSD1306(0x3d, OLED_SDA, OLED_SCL);
    try {
      displayInitialized = display.init();
      if(displayInitialized) {
        Serial.println("Display initialized with alternate address 0x3d");
      } else {
        Serial.println("Display initialization failed with alternate address too");
      }
    } catch (...) {
      Serial.println("Exception during alternate display init");
    }
  } else {
    Serial.println("Display initialized successfully");
  }
  
  if(displayInitialized) {
    display.flipScreenVertically();  
    display.setFont(ArialMT_Plain_10);
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 10, "KlimaLogg Pro");
    display.drawString(64, 30, "Receiver");
    display.display();
    Serial.println("Display content updated");
  }
  
  Serial.println("Setup complete");
}

void loop() {
  delay(1000);
  Serial.println("Alive");
  
  // Periodically update the display to ensure it's working
  static int counter = 0;
  if(counter % 5 == 0) {  // Every 5 seconds
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 10, "KlimaLogg Pro");
    display.drawString(64, 30, "Receiver");
    display.drawString(64, 50, "Uptime: " + String(counter) + "s");
    display.display();
    Serial.println("Display refreshed");
  }
  counter++;
}