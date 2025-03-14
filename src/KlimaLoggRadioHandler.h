#include <Arduino.h>
#include <RadioLib.h>

// RadioHandler class for configuring the LoRa radio module for KlimaLogg reception
class KlimaLoggRadioHandler {
private:
    SX1278* radio;
    float frequency;      // MHz
    float deviation;      // kHz
    float rxBandwidth;    // kHz
    float bitRate;        // kbps
    uint8_t syncWord[2];  // Sync/preamble pattern
    bool isReceiving;
    int lastRssi;
    
    // Default radio settings based on KlimaLogg protocol values
    static constexpr float DEFAULT_FREQ_EU = 868.3;  // EU frequency in MHz
    static constexpr float DEFAULT_FREQ_US = 915.0;  // US frequency in MHz
    static constexpr float DEFAULT_DEVIATION = 50.0; // kHz, approximate based on KlimaLogg AX5051 registers
    static constexpr float DEFAULT_BANDWIDTH = 100.0; // kHz
    static constexpr float DEFAULT_BITRATE = 17.241; // kbps, from AX5051 configuration
    
public:
    KlimaLoggRadioHandler(SX1278* _radio, bool useUSFrequency = false) : 
        radio(_radio),
        frequency(useUSFrequency ? DEFAULT_FREQ_US : DEFAULT_FREQ_EU),
        deviation(DEFAULT_DEVIATION),
        rxBandwidth(DEFAULT_BANDWIDTH),
        bitRate(DEFAULT_BITRATE),
        isReceiving(false),
        lastRssi(-120)
    {
        // Default sync word/preamble pattern from KlimaLogg (0xAA)
        syncWord[0] = 0xAA;
        syncWord[1] = 0xAA;
    }
    
    // Initialize radio for KlimaLogg reception
    bool begin() {
        Serial.println("Initializing radio for KlimaLogg reception...");
        
        // Initialize radio with FSK modulation
        int state = radio->beginFSK(frequency, bitRate, deviation, rxBandwidth);
        
        if (state != RADIOLIB_ERR_NONE) {
            Serial.print("Failed to initialize radio, code: ");
            Serial.println(state);
            return false;
        }
        
        // Configure specific radio settings for KlimaLogg reception
        radio->setOOK(false);                 // Use FSK not OOK
        radio->setDataShaping(0.0);           // No data shaping
        radio->setSyncWord(syncWord, 2);      // Set sync word
        radio->disableCrc();                  // KlimaLogg uses its own checksum
        radio->setEncoding(RADIOLIB_ENCODING_NRZ); // NRZ encoding
        
        // Apply frequency correction
        applyFrequencyCorrection();
        
        // Set radio to receive mode
        startReceive();
        
        Serial.println("Radio configured for KlimaLogg reception");
        return true;
    }
    
    // Start receiving
    void startReceive() {
        if (!isReceiving) {
            radio->startReceive();
            isReceiving = true;
        }
    }
    
    // Stop receiving
    void stopReceive() {
        if (isReceiving) {
            radio->standby();
            isReceiving = false;
        }
    }
    
    // Check if data is available
    bool available() {
        if (isReceiving) {
            return radio->available();
        }
        return false;
    }
    
    // Read received data into buffer
    int readData(uint8_t* buffer, size_t& length) {
        int state = radio->readData(buffer, length);
        
        // Get RSSI
        lastRssi = radio->getRSSI();
        
        // Restart reception
        startReceive();
        
        return state;
    }
    
    // Get last RSSI value (signal strength)
    int getRssi() {
        return lastRssi;
    }
    
    // Get signal quality as percentage (0-100)
    int getSignalQuality() {
        // Convert RSSI to percentage (rough approximation)
        // Typical RSSI range: -120 dBm (weak) to -30 dBm (strong)
        int quality = map(lastRssi, -120, -30, 0, 100);
        quality = constrain(quality, 0, 100);
        return quality;
    }
    
    // Apply frequency correction
    void applyFrequencyCorrection() {
        // In real KlimaLogg receiver, this reads from flash memory
        // Here we use a hardcoded correction factor for demonstration
        
        // Replicate the KlimaLogg frequency calculation logic
        uint32_t freqVal = (uint32_t)(frequency * 1000000.0 / 16000000.0 * 16777216.0);
        
        // Apply correction (example value from KlimaLogg code)
        int32_t corVal = 96416;
        freqVal += corVal;
        
        // Ensure odd value
        if (!(freqVal % 2)) {
            freqVal += 1;
        }
        
        // Calculate corrected frequency
        float correctedFreq = (float)freqVal * 16000000.0 / 16777216.0 / 1000000.0;
        
        // Set corrected frequency
        radio->setFrequency(correctedFreq);
        
        Serial.print("Applied frequency correction. Original: ");
        Serial.print(frequency, 3);
        Serial.print(" MHz, Corrected: ");
        Serial.print(correctedFreq, 3);
        Serial.println(" MHz");
    }
};