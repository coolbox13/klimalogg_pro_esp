# KlimaLogg Pro Receiver for TTGO LoRa32

This project implements a receiver for TFA KlimaLogg Pro weather stations using a TTGO LoRa32 board with SX1278 radio and OLED display.

## System Architecture

![KlimaLogg Pro Receiver Flow Diagram](flow-diagram.png)

The system has these main components:

1. **main.cpp** - Core application that initializes components and handles the main loop
2. **KlimaLoggRadioHandler.h** - Configures the SX1278 radio with proper parameters
3. **KlimaLoggDecode.h** - Functions to decode temperature, humidity and timestamps
4. **FrameParser.h** - Parses the different message types from KlimaLogg
5. **AX5051.h** - Optional component that emulates the AX5051 chip used in KlimaLogg

The AX5051.h file provides emulation for the AX5051 radio chip used in the original KlimaLogg station. It's considered optional because:

1. Direct functionality: Our implementation directly configures the SX1278 chip on the TTGO board using RadioLib, without needing to fully emulate the AX5051.
2. Register mapping: The essential register values from the AX5051 (frequency, modulation parameters, etc.) are already incorporated into the KlimaLoggRadioHandler.h.
3. Partial implementation: The AX5051 implementation primarily serves as a reference to understand how the original chip was configured but isn't required for actual operation.

## Features

- Receives current weather data from KlimaLogg Pro base station and up to 8 remote sensors
- Displays temperature and humidity readings on the OLED display
- Shows battery status for all sensors
- Configurable for EU (868.33 MHz) or US (915 MHz) frequencies

## Hardware Requirements

- TTGO LoRa32 V1.3 board with SX1278 radio and OLED display
- USB cable for power and programming

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

The protocol implementation is deriv# KlimaLogg Pro Receiver for TTGO LoRa32

This project implements a receiver for TFA KlimaLogg Pro weather stations using a TTGO LoRa32 board with SX1278 radio and OLED display.

## Features

- Receives current weather data from KlimaLogg Pro base station and up to 8 remote sensors
- Displays temperature and humidity readings on the OLED display
- Shows battery status for all sensors
- Configurable for EU (868.33 MHz) or US (915 MHz) frequencies

## Hardware Requirements

- TTGO LoRa32 V1.3 board with SX1278 radio and OLED display
- USB cable for power and programming

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