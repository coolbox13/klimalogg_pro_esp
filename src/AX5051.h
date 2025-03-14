// AX5051.h
#ifndef AX5051_H
#define AX5051_H

#include <Arduino.h>
#include <RadioLib.h>

// AX5051 Register names taken from KlimaLogg protocol
class AX5051RegisterNames {
public:
    static const uint8_t REVISION     = 0x0;
    static const uint8_t SCRATCH      = 0x1;
    static const uint8_t POWERMODE    = 0x2;
    static const uint8_t XTALOSC      = 0x3;
    static const uint8_t FIFOCTRL     = 0x4;
    static const uint8_t FIFODATA     = 0x5;
    static const uint8_t IRQMASK      = 0x6;
    static const uint8_t IFMODE       = 0x8;
    static const uint8_t PINCFG1      = 0x0C;
    static const uint8_t PINCFG2      = 0x0D;
    static const uint8_t MODULATION   = 0x10;
    static const uint8_t ENCODING     = 0x11;
    static const uint8_t FRAMING      = 0x12;
    static const uint8_t CRCINIT3     = 0x14;
    static const uint8_t CRCINIT2     = 0x15;
    static const uint8_t CRCINIT1     = 0x16;
    static const uint8_t CRCINIT0     = 0x17;
    static const uint8_t FREQ3        = 0x20;
    static const uint8_t FREQ2        = 0x21;
    static const uint8_t FREQ1        = 0x22;
    static const uint8_t FREQ0        = 0x23;
    static const uint8_t FSKDEV2      = 0x25;
    static const uint8_t FSKDEV1      = 0x26;
    static const uint8_t FSKDEV0      = 0x27;
    static const uint8_t IFFREQHI     = 0x28;
    static const uint8_t IFFREQLO     = 0x29;
    static const uint8_t PLLLOOP      = 0x2C;
    static const uint8_t PLLRANGING   = 0x2D;
    static const uint8_t PLLRNGCLK    = 0x2E;
    static const uint8_t TXPWR        = 0x30;
    static const uint8_t TXRATEHI     = 0x31;
    static const uint8_t TXRATEMID    = 0x32;
    static const uint8_t TXRATELO     = 0x33;
    static const uint8_t MODMISC      = 0x34;
    static const uint8_t FIFOCONTROL2 = 0x37;
    static const uint8_t ADCMISC      = 0x38;
    static const uint8_t AGCTARGET    = 0x39;
    static const uint8_t AGCATTACK    = 0x3A;
    static const uint8_t AGCDECAY     = 0x3B;
    static const uint8_t AGCCOUNTER   = 0x3C;
    static const uint8_t CICDEC       = 0x3F;
    static const uint8_t DATARATEHI   = 0x40;
    static const uint8_t DATARATELO   = 0x41;
    static const uint8_t TMGGAINHI    = 0x42;
    static const uint8_t TMGGAINLO    = 0x43;
    static const uint8_t PHASEGAIN    = 0x44;
    static const uint8_t FREQGAIN     = 0x45;
    static const uint8_t FREQGAIN2    = 0x46;
    static const uint8_t AMPLGAIN     = 0x47;
    static const uint8_t TRKFREQHI    = 0x4C;
    static const uint8_t TRKFREQLO    = 0x4D;
    static const uint8_t XTALCAP      = 0x4F;
    static const uint8_t SPAREOUT     = 0x60;
    static const uint8_t TESTOBS      = 0x68;
    static const uint8_t APEOVER      = 0x70;
    static const uint8_t TMMUX        = 0x71;
    static const uint8_t PLLVCOI      = 0x72;
    static const uint8_t PLLCPEN      = 0x73;
    static const uint8_t PLLRNGMISC   = 0x74;
    static const uint8_t AGCMANUAL    = 0x78;
    static const uint8_t ADCDCLEVEL   = 0x79;
    static const uint8_t RFMISC       = 0x7A;
    static const uint8_t TXDRIVER     = 0x7B;
    static const uint8_t REF          = 0x7C;
    static const uint8_t RXMISC       = 0x7D;
};

class AX5051Emulator {
private:
    SX1278* radio;
    uint8_t registers[128]; // Register storage
    
public:
    AX5051Emulator(SX1278* _radio) : radio(_radio) {
        // Initialize registers with default values from KlimaLogg
        configureRegisterNames();
    }
    
    void configureRegisterNames() {
        // Initialize register map with values from KlimaLogg driver
        registers[AX5051RegisterNames::IFMODE]     = 0x00;
        registers[AX5051RegisterNames::MODULATION] = 0x41;  // FSK
        registers[AX5051RegisterNames::ENCODING]   = 0x07;
        registers[AX5051RegisterNames::FRAMING]    = 0x84;
        registers[AX5051RegisterNames::CRCINIT3]   = 0xff;
        registers[AX5051RegisterNames::CRCINIT2]   = 0xff;
        registers[AX5051RegisterNames::CRCINIT1]   = 0xff;
        registers[AX5051RegisterNames::CRCINIT0]   = 0xff;
        registers[AX5051RegisterNames::FREQ3]      = 0x38;
        registers[AX5051RegisterNames::FREQ2]      = 0x90;
        registers[AX5051RegisterNames::FREQ1]      = 0x00;
        registers[AX5051RegisterNames::FREQ0]      = 0x01;
        registers[AX5051RegisterNames::PLLLOOP]    = 0x1d;
        registers[AX5051RegisterNames::PLLRANGING] = 0x08;
        registers[AX5051RegisterNames::PLLRNGCLK]  = 0x03;
        registers[AX5051RegisterNames::MODMISC]    = 0x03;
        registers[AX5051RegisterNames::SPAREOUT]   = 0x00;
        registers[AX5051RegisterNames::TESTOBS]    = 0x00;
        registers[AX5051RegisterNames::APEOVER]    = 0x00;
        registers[AX5051RegisterNames::TMMUX]      = 0x00;
        registers[AX5051RegisterNames::PLLVCOI]    = 0x01;
        registers[AX5051RegisterNames::PLLCPEN]    = 0x01;
        registers[AX5051RegisterNames::RFMISC]     = 0xb0;
        registers[AX5051RegisterNames::REF]        = 0x23;
        registers[AX5051RegisterNames::IFFREQHI]   = 0x20;
        registers[AX5051RegisterNames::IFFREQLO]   = 0x00;
        registers[AX5051RegisterNames::ADCMISC]    = 0x01;
        registers[AX5051RegisterNames::AGCTARGET]  = 0x0e;
        registers[AX5051RegisterNames::AGCATTACK]  = 0x11;
        registers[AX5051RegisterNames::AGCDECAY]   = 0x0e;
        registers[AX5051RegisterNames::CICDEC]     = 0x3f;
        registers[AX5051RegisterNames::DATARATEHI] = 0x19;
        registers[AX5051RegisterNames::DATARATELO] = 0x66;
        registers[AX5051RegisterNames::TMGGAINHI]  = 0x01;
        registers[AX5051RegisterNames::TMGGAINLO]  = 0x96;
        registers[AX5051RegisterNames::PHASEGAIN]  = 0x03;
        registers[AX5051RegisterNames::FREQGAIN]   = 0x04;
        registers[AX5051RegisterNames::FREQGAIN2]  = 0x0a;
        registers[AX5051RegisterNames::AMPLGAIN]   = 0x06;
        registers[AX5051RegisterNames::AGCMANUAL]  = 0x00;
        registers[AX5051RegisterNames::ADCDCLEVEL] = 0x10;
        registers[AX5051RegisterNames::RXMISC]     = 0x35;
        registers[AX5051RegisterNames::FSKDEV2]    = 0x00;
        registers[AX5051RegisterNames::FSKDEV1]    = 0x31;
        registers[AX5051RegisterNames::FSKDEV0]    = 0x27;
        registers[AX5051RegisterNames::TXPWR]      = 0x03;
        registers[AX5051RegisterNames::TXRATEHI]   = 0x00;
        registers[AX5051RegisterNames::TXRATEMID]  = 0x51;
        registers[AX5051RegisterNames::TXRATELO]   = 0xec;
        registers[AX5051RegisterNames::TXDRIVER]   = 0x88;
    }
    
    void setFrequency(float frequency) {
        // Convert frequency to the AX5051 register format
        uint32_t freqVal = (uint32_t)(frequency / 16000000.0 * 16777216.0);
        
        // Apply the frequency correction
        int32_t corVal = 96416; // Example value, should be read from flash in real implementation
        
        freqVal += corVal;
        if (!(freqVal % 2)) {
            freqVal += 1;
        }
        
        // Set the frequency registers
        registers[AX5051RegisterNames::FREQ3] = (freqVal >> 24) & 0xFF;
        registers[AX5051RegisterNames::FREQ2] = (freqVal >> 16) & 0xFF;
        registers[AX5051RegisterNames::FREQ1] = (freqVal >> 8)  & 0xFF;
        registers[AX5051RegisterNames::FREQ0] = (freqVal >> 0)  & 0xFF;
        
        // Set the frequency in the actual radio module
        float actualFreq = (float)freqVal * 16000000.0 / 16777216.0;
        radio->setFrequency(actualFreq / 1000000.0); // Convert to MHz for RadioLib
    }
    
    uint8_t readRegister(uint8_t address) {
        return registers[address];
    }
    
    void writeRegister(uint8_t address, uint8_t value) {
        registers[address] = value;
        
        // Implement special handling for certain registers
        // that need to be applied to the actual radio
        switch (address) {
            case AX5051RegisterNames::MODULATION:
                // Set modulation based on value
                if (value == 0x41) {
                    // FSK modulation
                    radio->setOOK(false);
                }
                break;
                
            case AX5051RegisterNames::FSKDEV1:
            case AX5051RegisterNames::FSKDEV0:
                // Recalculate FSK frequency deviation
                uint32_t fskDev = 
                    ((uint32_t)registers[AX5051RegisterNames::FSKDEV1] << 8) | 
                    registers[AX5051RegisterNames::FSKDEV0];
                // Convert to kHz for RadioLib
                radio->setFrequencyDeviation(fskDev / 10.0);
                break;
        }
    }
    
    void applyAllRegisters() {
        // Apply all register settings to the radio module
        // This would require translating all AX5051 settings to SX1278 equivalents
        
        // Set FSK modulation
        radio->setOOK(false);
        
        // Set frequency using the register values
        uint32_t freqVal = 
            ((uint32_t)registers[AX5051RegisterNames::FREQ3] << 24) |
            ((uint32_t)registers[AX5051RegisterNames::FREQ2] << 16) |
            ((uint32_t)registers[AX5051RegisterNames::FREQ1] << 8) |
            registers[AX5051RegisterNames::FREQ0];
        
        float frequency = (float)freqVal * 16000000.0 / 16777216.0 / 1000000.0;
        radio->setFrequency(frequency);
        
        // Set FSK deviation
        uint32_t fskDev = 
            ((uint32_t)registers[AX5051RegisterNames::FSKDEV1] << 8) | 
            registers[AX5051RegisterNames::FSKDEV0];
        radio->setFrequencyDeviation(fskDev / 10.0);
        
        // Set other parameters as needed
    }
};

#endif // AX5051_H