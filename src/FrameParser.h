#include <Arduino.h>
#include "KlimaLoggDecode.h"

// Class for parsing KlimaLogg frames
class KlimaLoggFrameParser {
private:
    // Structure for current weather data
    struct CurrentData {
        uint32_t timestamp;
        uint8_t signalQuality;
        float temperature[9];
        float temperatureMax[9];
        float temperatureMin[9];
        uint32_t temperatureMaxTS[9];
        uint32_t temperatureMinTS[9];
        uint8_t humidity[9];
        uint8_t humidityMax[9];
        uint8_t humidityMin[9];
        uint32_t humidityMaxTS[9];
        uint32_t humidityMinTS[9];
        uint8_t alarmData[12];
        
        CurrentData() {
            // Initialize with default values
            timestamp = 0;
            signalQuality = 0;
            
            for (int i = 0; i < 9; i++) {
                temperature[i] = KlimaLoggDecode::TEMPERATURE_NP;
                temperatureMax[i] = KlimaLoggDecode::TEMPERATURE_NP;
                temperatureMin[i] = KlimaLoggDecode::TEMPERATURE_NP;
                temperatureMaxTS[i] = 0;
                temperatureMinTS[i] = 0;
                
                humidity[i] = KlimaLoggDecode::HUMIDITY_NP;
                humidityMax[i] = KlimaLoggDecode::HUMIDITY_NP;
                humidityMin[i] = KlimaLoggDecode::HUMIDITY_NP;
                humidityMaxTS[i] = 0;
                humidityMinTS[i] = 0;
            }
            
            memset(alarmData, 0, sizeof(alarmData));
        }
    };
    
    // Map to translate between buffer positions and data values for current weather data
    static const uint16_t BUFMAP[9][10];

public:
    // Parse a current weather data frame
    static CurrentData parseCurrentWeatherFrame(uint8_t* buffer, size_t length) {
        CurrentData data;
        
        // Check if buffer has enough data
        if (length < 230) {
            Serial.println("Current weather frame too short");
            return data;
        }
        
        // Set timestamp to current time
        data.timestamp = millis() / 1000;
        
        // Get signal quality
        data.signalQuality = buffer[4] & 0x7F;
        
        // Parse sensor data for all 9 sensors (base + 8 remote)
        for (int x = 0; x < 9; x++) {
            // Temperature data
            data.temperatureMax[x] = KlimaLoggDecode::toTemperature_3_1(buffer, BUFMAP[x][0], 0);
            data.temperatureMin[x] = KlimaLoggDecode::toTemperature_3_1(buffer, BUFMAP[x][1], 1);
            data.temperature[x] = KlimaLoggDecode::toTemperature_3_1(buffer, BUFMAP[x][2], 0);
            
            // Temperature timestamps
            if (KlimaLoggDecode::isValidTemperature(data.temperatureMax[x])) {
                data.temperatureMaxTS[x] = KlimaLoggDecode::toDateTime8(buffer, BUFMAP[x][3], 0, "TemperatureMax");
            }
            
            if (KlimaLoggDecode::isValidTemperature(data.temperatureMin[x])) {
                data.temperatureMinTS[x] = KlimaLoggDecode::toDateTime8(buffer, BUFMAP[x][4], 0, "TemperatureMin");
            }
            
            // Humidity data
            data.humidityMax[x] = KlimaLoggDecode::toHumidity_2_0(buffer, BUFMAP[x][5], 1);
            data.humidityMin[x] = KlimaLoggDecode::toHumidity_2_0(buffer, BUFMAP[x][6], 1);
            data.humidity[x] = KlimaLoggDecode::toHumidity_2_0(buffer, BUFMAP[x][7], 1);
            
            // Humidity timestamps
            if (KlimaLoggDecode::isValidHumidity(data.humidityMax[x])) {
                data.humidityMaxTS[x] = KlimaLoggDecode::toDateTime8(buffer, BUFMAP[x][8], 1, "HumidityMax");
            }
            
            if (KlimaLoggDecode::isValidHumidity(data.humidityMin[x])) {
                data.humidityMinTS[x] = KlimaLoggDecode::toDateTime8(buffer, BUFMAP[x][9], 1, "HumidityMin");
            }
        }
        
        // Copy alarm data (12 bytes)
        memcpy(data.alarmData, &buffer[223], 12);
        
        return data;
    }
    
    // Get battery status from alarm data
    static bool getBatteryStatus(uint8_t* alarmData, int sensorIndex) {
        if (sensorIndex == 0) {
            // Base station
            return ((alarmData[1] & 0x80) == 0);
        } else if (sensorIndex >= 1 && sensorIndex <= 8) {
            // Remote sensors (1-8)
            uint8_t bitmask = 1 << (sensorIndex - 1);
            return ((alarmData[0] & bitmask) == 0);
        }
        return false;
    }
};

// Define the buffer map for current weather data
// Maps sensor index to buffer positions for different data fields
// From the KlimaLogg protocol: BUFMAP = {0: ( 26, 28, 29, 18, 22, 15, 16, 17,  7, 11), ... }
const uint16_t KlimaLoggFrameParser::BUFMAP[9][10] = {
    { 26, 28, 29, 18, 22, 15, 16, 17,  7, 11 }, // Sensor 0 (base)
    { 50, 52, 53, 42, 46, 39, 40, 41, 31, 35 }, // Sensor 1
    { 74, 76, 77, 66, 70, 63, 64, 65, 55, 59 }, // Sensor 2
    { 98,100,101, 90, 94, 87, 88, 89, 79, 83 }, // Sensor 3
    {122,124,125,114,118,111,112,113,103,107 }, // Sensor 4
    {146,148,149,138,142,135,136,137,127,131 }, // Sensor 5
    {170,172,173,162,166,159,160,161,151,155 }, // Sensor 6
    {194,196,197,186,190,183,184,185,175,179 }, // Sensor 7
    {218,220,221,210,214,207,208,209,199,203 }  // Sensor 8
};