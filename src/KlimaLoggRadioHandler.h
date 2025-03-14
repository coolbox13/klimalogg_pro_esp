#include <Arduino.h>
#include <rtl_433_ESP.h>

// RadioHandler class for configuring the radio module for KlimaLogg reception using rtl_433_ESP
class KlimaLoggRadioHandler {
private:
    rtl_433_ESP* rtl433;
    float frequency;      // MHz
    bool isReceiving;
    int lastRssi;
    int packetCount;
    unsigned long lastPacketTime;
    
    // Default radio settings based on KlimaLogg protocol values
    static constexpr float DEFAULT_FREQ_EU = 868.3;  // EU frequency in MHz
    static constexpr float DEFAULT_FREQ_US = 915.0;  // US frequency in MHz
    
    // Callback function for rtl_433 data
    static void rtl433Callback(char* message) {
        // This will be handled in the main code
    }
    
public:
    KlimaLoggRadioHandler(bool useUSFrequency = false) : 
        frequency(useUSFrequency ? DEFAULT_FREQ_US : DEFAULT_FREQ_EU),
        isReceiving(false),
        lastRssi(-120),
        packetCount(0),
        lastPacketTime(0)
    {
        rtl433 = new rtl_433_ESP();
    }
    
    // Initialize radio for KlimaLogg reception
    bool begin(int csPin, int irqPin, int rstPin, void (*callback)(char*)) {
        Serial.println("Initializing rtl_433_ESP for KlimaLogg reception...");
        
        // Initialize rtl_433_ESP
        rtl433->initReceiver(csPin, irqPin, rstPin);
        
        // Set the callback function
        rtl433->setCallback(callback, "KlimaLogg");
        
        // Set the frequency
        rtl433->setFrequency(frequency);
        
        // Enable the receiver
        rtl433->enableReceiver(true);
        
        isReceiving = true;
        Serial.println("rtl_433_ESP configured for KlimaLogg reception");
        return true;
    }
    
    // Process incoming data (call this in loop)
    void loop() {
        rtl433->loop();
    }
    
    // Start receiving
    void startReceive() {
        if (!isReceiving) {
            rtl433->enableReceiver(true);
            isReceiving = true;
        }
    }
    
    // Stop receiving
    void stopReceive() {
        if (isReceiving) {
            rtl433->enableReceiver(false);
            isReceiving = false;
        }
    }
    
    // Check if data is available (always returns false as rtl_433_ESP uses callbacks)
    bool available() {
        return false; // rtl_433_ESP uses callbacks instead
    }
    
    // Get last RSSI value (not directly available in rtl_433_ESP)
    int getRssi() {
        return lastRssi;
    }
    
    // Set RSSI value (to be called from main code if RSSI is available)
    void setRssi(int rssi) {
        lastRssi = rssi;
    }
    
    // Get signal quality as percentage (0-100)
    int getSignalQuality() {
        // Convert RSSI to percentage (rough approximation)
        // Typical RSSI range: -120 dBm (weak) to -30 dBm (strong)
        int quality = map(lastRssi, -120, -30, 0, 100);
        quality = constrain(quality, 0, 100);
        return quality;
    }
    
    // Increment packet count
    void incrementPacketCount() {
        packetCount++;
        lastPacketTime = millis();
    }
    
    // Get packet count
    int getPacketCount() {
        return packetCount;
    }
    
    // Get time since last packet
    unsigned long getTimeSinceLastPacket() {
        return millis() - lastPacketTime;
    }
    
    // Get last packet time
    unsigned long getLastPacketTime() {
        return lastPacketTime;
    }
};