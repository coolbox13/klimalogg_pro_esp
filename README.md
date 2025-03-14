# KlimaLogg Pro Receiver for TTGO LoRa32

This project implements a receiver for TFA KlimaLogg Pro weather stations using a TTGO LoRa32 board with SX1278 radio and OLED display.

## Features

- Receives current weather data from KlimaLogg Pro base station and up to 8 remote sensors
- Displays temperature and humidity readings on the OLED display
- Shows battery status for all sensors
- Configurable for EU (868.33 MHz) or US (915 MHz) frequencies

## Hardware

- TTGO LoRa32 v2.0/v2.1 board
- SX1278 LoRa module (built into the TTGO board)
- SSD1306 OLED display (built into the TTGO board)

## Display Configuration

The TTGO LoRa32 boards come in different versions with different pin configurations. This project uses a v2.0/v2.1 board which requires specific pin settings for the OLED display:

```cpp
// Undefine the board's default pin definitions to avoid warnings
#undef OLED_SDA
#undef OLED_SCL
#undef OLED_RST

// Define custom pin configuration for TTGO LoRa32 v2.0/v2.1
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST -1  // No reset pin needed

    ## Important Note: If you're using a TTGO LoRa32 v1 board, you might need to use different pin configurations:
    - For v1: SDA=4, SCL=15, RST=16
    - For v2.0/v2.1: SDA=21, SCL=22, no reset pin
    Using incorrect pin configurations can cause the board to continuously reset with a watchdog timer error (TG1WDT_SYS_RESET).

## Software Requirements

- PlatformIO
- Arduino framework
- Required libraries:
  - RadioLib
  - SSD1306 driver for OLED display

## Installation

1. Clone this repository
2. Open the project in PlatformIO
3. Configure the frequency in `KlimaLoggRadioHandler` constructor (EU or US)
4. Build and upload to your TTGO LoRa32 board

## Usage

1. Power on your TTGO LoRa32 board
2. Ensure your KlimaLogg Pro base station is active and transmitting
3. The display will show:
   - Current temperature and humidity from detected sensors
   - Signal quality indicator
   - Battery status (! indicates low battery)
   - Last reception time

## Configuration

- Edit `platformio.ini` to change build options
- By default, EU frequency (868.33 MHz) is used
- For US, change the `radioHandler` initialization to `true`

## Radio Parameters

The radio configuration is based on the rtl_433 settings that work with KlimaLogg:
```
rtl_433 -s 2.5e6 -f 868330K -H 30 -M hires -M level -R 150 -v -F json
```

These translate to:
- Frequency: 868.33 MHz
- Sample rate: 2.5 MHz
- Device protocol: 150 (KlimaLogg)

The implementation uses these parameters to configure the SX1278 radio for optimal reception.

## Troubleshooting

- Ensure your KlimaLogg Pro base station is within range
- The KlimaLogg Pro broadcasts data every ~15 seconds
- If no data is received, try pressing the USB button on the KlimaLogg Pro console
- Check that the frequency setting matches your region
- Try adjusting the radio parameters if reception is poor

## Implementation Details

The implementation is based on reverse engineering of the KlimaLogg Pro protocol, with the following key components:

- `KlimaLoggDecode.h`: Implements decoding functions for temperature, humidity, and timestamps
- `KlimaLoggRadioHandler.h`: Configures the SX1278 radio for KlimaLogg reception
- `main.cpp`: Main application that receives and displays sensor data

## Protocol Reference

The KlimaLogg Pro uses a proprietary protocol on 868.33 MHz with FSK modulation. Key details:

- Packet structure: 
  - Device ID (2 bytes) 
  - Logger ID (1 byte)
  - Response type (1 byte)
  - Signal quality (1 byte)
  - Data (variable length)
  - Checksum (2 bytes)

- Response types:
  - 0x20: Configuration data
  - 0x30: Current weather data
  - 0x40: History data
  - 0x50: Request data

The protocol implementation is derived from the original KlimaLogg driver for weewx.


This README provides clear information about the board type and the specific display configuration needed to make it work. It highlights the difference between v1 and v2.0/v2.1 boards and explains that using incorrect pin configurations can cause the board to reset continuously.