// KlimaLoggDecode.h
#ifndef KLIMALOGG_DECODE_H
#define KLIMALOGG_DECODE_H

#include <Arduino.h>

// Based on the provided Python code, this is a C++ version of the KlimaLogg decoding functions
class KlimaLoggDecode {
public:
    // Temperature and humidity limit constants
    static constexpr float TEMPERATURE_OFFSET = 40.0;
    static constexpr float TEMPERATURE_NP = 81.1;    // Not present
    static constexpr float TEMPERATURE_OFL = 136.0;  // Outside factory limits
    static constexpr float HUMIDITY_NP = 110.0;      // Not present
    static constexpr float HUMIDITY_OFL = 121.0;     // Outside factory limits

    // Character map for decoding sensor text (from Python code)
    static const char CHARMAP[64];
    static const char* CHARSTR;

    // Check if temperature or humidity value is valid
    static bool isValidTemperature(float value) {
        return (value != TEMPERATURE_NP && value != TEMPERATURE_OFL);
    }
    
    static bool isValidHumidity(float value) {
        return (value != HUMIDITY_NP && value != HUMIDITY_OFL);
    }
    
    // Temperature decoding - 3 nibbles with 1 decimal place
    static float toTemperature_3_1(uint8_t* buf, int start, bool startOnHiNibble) {
        if (isErr3(buf, start, startOnHiNibble)) {
            return TEMPERATURE_NP;
        }
        else if (isOFL3(buf, start, startOnHiNibble)) {
            return TEMPERATURE_OFL;
        }
        else {
            float rawtemp;
            if (startOnHiNibble) {
                rawtemp = (buf[start] >> 4) * 10 
                        + (buf[start + 0] & 0xF) * 1 
                        + (buf[start + 1] >> 4) * 0.1;
            }
            else {
                rawtemp = (buf[start] & 0xF) * 10 
                        + (buf[start + 1] >> 4) * 1 
                        + (buf[start + 1] & 0xF) * 0.1;
            }
            return rawtemp - TEMPERATURE_OFFSET;
        }
    }
    
    // Humidity decoding - 2 nibbles with 0 decimal places
    static uint8_t toHumidity_2_0(uint8_t* buf, int start, bool startOnHiNibble) {
        if (isErr2(buf, start, startOnHiNibble)) {
            return HUMIDITY_NP;
        }
        else if (isOFL2(buf, start, startOnHiNibble)) {
            return HUMIDITY_OFL;
        }
        else {
            if (startOnHiNibble) {
                return (buf[start] >> 4) * 10 + (buf[start] & 0xF);
            }
            else {
                return (buf[start] & 0xF) * 10 + (buf[start + 1] >> 4);
            }
        }
    }
    
    // DateTime conversion - 10 nibbles
    static uint32_t toDateTime10(uint8_t* buf, int start, bool startOnHiNibble, const char* label) {
        if (isErr2(buf, start + 0, startOnHiNibble) || 
            isErr2(buf, start + 1, startOnHiNibble) || 
            isErr2(buf, start + 2, startOnHiNibble) || 
            isErr2(buf, start + 3, startOnHiNibble) || 
            isErr2(buf, start + 4, startOnHiNibble)) {
            
            Serial.print("ToDateTime: bogus date for ");
            Serial.println(label);
            return 0; // Invalid timestamp
        }
        else {
            int year = toInt_2(buf, start + 0, startOnHiNibble) + 2000;
            int month = toInt_2(buf, start + 1, startOnHiNibble);
            int days = toInt_2(buf, start + 2, startOnHiNibble);
            int hours = toInt_2(buf, start + 3, startOnHiNibble);
            int minutes = toInt_2(buf, start + 4, startOnHiNibble);
            
            // Basic bounds checking
            if (month < 1 || month > 12 || days < 1 || days > 31 || 
                hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
                Serial.print("ToDateTime: bad date conversion for ");
                Serial.println(label);
                return 0; // Invalid timestamp
            }
            
            // Convert to Unix timestamp (simplified, not accounting for all edge cases)
            struct tm timeinfo = { 0 };
            timeinfo.tm_year = year - 1900;
            timeinfo.tm_mon = month - 1;
            timeinfo.tm_mday = days;
            timeinfo.tm_hour = hours;
            timeinfo.tm_min = minutes;
            timeinfo.tm_sec = 0;
            
            time_t timestamp = mktime(&timeinfo);
            return (uint32_t)timestamp;
        }
    }
    
    // DateTime conversion - 8 nibbles
    static uint32_t toDateTime8(uint8_t* buf, int start, bool startOnHiNibble, const char* label) {
        if (isErr8(buf, start + 0, startOnHiNibble)) {
            Serial.print("ToDateTime: ");
            Serial.print(label);
            Serial.println(": no valid date");
            return 0; // Invalid timestamp
        }
        else {
            int year, month, days, tim1, tim2, tim3;
            
            if (startOnHiNibble) {
                year = toInt_2(buf, start + 0, 1) + 2000;
                month = toInt_1(buf, start + 1, 1);
                days = toInt_2(buf, start + 1, 0);
                tim1 = toInt_1(buf, start + 2, 0);
                tim2 = toInt_1(buf, start + 3, 1);
                tim3 = toInt_1(buf, start + 3, 0);
            }
            else {
                year = toInt_2(buf, start + 0, 0) + 2000;
                month = toInt_1(buf, start + 1, 0);
                days = toInt_2(buf, start + 2, 1);
                tim1 = toInt_1(buf, start + 3, 1);
                tim2 = toInt_1(buf, start + 3, 0);
                tim3 = toInt_1(buf, start + 4, 1);
            }
            
            int hours, minutes;
            if (tim1 >= 10) {
                hours = tim1 + 10;
            }
            else {
                hours = tim1;
            }
            
            if (tim2 >= 10) {
                hours += 10;
                minutes = (tim2 - 10) * 10;
            }
            else {
                minutes = tim2 * 10;
            }
            minutes += tim3;
            
            // Basic bounds checking
            if (month < 1 || month > 12 || days < 1 || days > 31 || 
                hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
                Serial.print("ToDateTime: bad date conversion for ");
                Serial.println(label);
                return 0; // Invalid timestamp
            }
            
            // Convert to Unix timestamp (simplified, not accounting for all edge cases)
            struct tm timeinfo = { 0 };
            timeinfo.tm_year = year - 1900;
            timeinfo.tm_mon = month - 1;
            timeinfo.tm_mday = days;
            timeinfo.tm_hour = hours;
            timeinfo.tm_min = minutes;
            timeinfo.tm_sec = 0;
            
            time_t timestamp = mktime(&timeinfo);
            return (uint32_t)timestamp;
        }
    }
    
    // Helper functions
    static bool isOFL2(uint8_t* buf, int start, bool startOnHiNibble) {
        if (startOnHiNibble) {
            return ((buf[start + 0] >> 4) == 15 || (buf[start + 0] & 0xF) == 15);
        }
        else {
            return ((buf[start + 0] & 0xF) == 15 || (buf[start + 1] >> 4) == 15);
        }
    }
    
    static bool isOFL3(uint8_t* buf, int start, bool startOnHiNibble) {
        if (startOnHiNibble) {
            return ((buf[start + 0] >> 4) == 15 || 
                   (buf[start + 0] & 0xF) == 15 || 
                   (buf[start + 1] >> 4) == 15);
        }
        else {
            return ((buf[start + 0] & 0xF) == 15 || 
                   (buf[start + 1] >> 4) == 15 || 
                   (buf[start + 1] & 0xF) == 15);
        }
    }
    
    static bool isErr2(uint8_t* buf, int start, bool startOnHiNibble) {
        if (startOnHiNibble) {
            return ((buf[start + 0] >> 4) >= 10 && (buf[start + 0] >> 4) != 15 || 
                   (buf[start + 0] & 0xF) >= 10 && (buf[start + 0] & 0xF) != 15);
        }
        else {
            return ((buf[start + 0] & 0xF) >= 10 && (buf[start + 0] & 0xF) != 15 || 
                   (buf[start + 1] >> 4) >= 10 && (buf[start + 1] >> 4) != 15);
        }
    }
    
    static bool isErr3(uint8_t* buf, int start, bool startOnHiNibble) {
        if (startOnHiNibble) {
            return ((buf[start + 0] >> 4) >= 10 && (buf[start + 0] >> 4) != 15 || 
                   (buf[start + 0] & 0xF) >= 10 && (buf[start + 0] & 0xF) != 15 || 
                   (buf[start + 1] >> 4) >= 10 && (buf[start + 1] >> 4) != 15);
        }
        else {
            return ((buf[start + 0] & 0xF) >= 10 && (buf[start + 0] & 0xF) != 15 || 
                   (buf[start + 1] >> 4) >= 10 && (buf[start + 1] >> 4) != 15 || 
                   (buf[start + 1] & 0xF) >= 10 && (buf[start + 1] & 0xF) != 15);
        }
    }
    
    static bool isErr8(uint8_t* buf, int start, bool startOnHiNibble) {
        if (startOnHiNibble) {
            return ((buf[start + 0] >> 4) == 10 && 
                   (buf[start + 0] & 0xF) == 10 && 
                   (buf[start + 1] >> 4) == 4  && 
                   (buf[start + 1] & 0xF) == 10 && 
                   (buf[start + 2] >> 4) == 10 && 
                   (buf[start + 2] & 0xF) == 4  && 
                   (buf[start + 3] >> 4) == 10 && 
                   (buf[start + 3] & 0xF) == 10);
        }
        else {
            return ((buf[start + 0] & 0xF) == 10 && 
                   (buf[start + 1] >> 4) == 10 && 
                   (buf[start + 1] & 0xF) == 4  && 
                   (buf[start + 2] >> 4) == 10 && 
                   (buf[start + 2] & 0xF) == 10 && 
                   (buf[start + 3] >> 4) == 4  && 
                   (buf[start + 3] & 0xF) == 10 && 
                   (buf[start + 4] >> 4) == 10);
        }
    }
    
    static uint8_t toInt_1(uint8_t* buf, int start, bool startOnHiNibble) {
        if (startOnHiNibble) {
            return (buf[start] >> 4);
        }
        else {
            return (buf[start] & 0xF);
        }
    }
    
    static uint8_t toInt_2(uint8_t* buf, int start, bool startOnHiNibble) {
        if (startOnHiNibble) {
            return (buf[start] >> 4) * 10 + (buf[start + 0] & 0xF) * 1;
        }
        else {
            return (buf[start] & 0xF) * 10 + (buf[start + 1] >> 4) * 1;
        }
    }
};

// Define static constants
const char KlimaLoggDecode::CHARMAP[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    '0', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
    'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
    'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '-', '+', '(',
    ')', 'o', '*', ',', '/', '\\', ' ', '.', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', '@'
};

const char* KlimaLoggDecode::CHARSTR = "!1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ-+()o*,/\\ .";

#endif // KLIMALOGG_DECODE_H