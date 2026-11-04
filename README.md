# KlimaLogg — receive TFA KlimaLogg Pro sensors on an ESP32

A PlatformIO library that receives **TFA KlimaLogg Pro** wireless sensors
(30.3180.IT temperature/humidity, 30.3181.IT temperature) directly with an
**ESP32 + SX1276** (RFM95 / LoRa module). No base station, USB transceiver,
SDR or PC needed.

Tested on a **LilyGO T3 V1.6.1** (TTGO LoRa32 V2.1_1.6, 868 MHz version)
with three 30.3180.IT sensors. Classic ESP32 only (not the RISC-V C3/C6/H2).

```
{"model":"KlimaLogg-Pro","id":"1D21","temperature_C":20.6,"humidity":55,"battery_ok":true,"sequence":2,"rssi":-54.5}
```

## Quick start

```ini
; platformio.ini
[env:t3_v161]
platform = espressif32 @ 6.13.0
board = ttgo-lora32-v21
framework = arduino
lib_deps = https://github.com/coolbox13/klimalogg_pro_esp.git#v1.1.0
```

```cpp
#include <Arduino.h>
#include <KlimaLoggReceiver.h>

klimalogg::Receiver receiver;

void setup() {
  Serial.begin(115200);
  receiver.begin(klimalogg::ReceiverPins::lilygoT3V161());
}

void loop() {
  klimalogg::Telegram t;
  while (receiver.poll(&t)) {
    if (t.result.status != klimalogg::Status::Ok) continue;
    const klimalogg::Reading& r = t.result.reading;
    Serial.printf("%04X %.1f C", r.id, r.temperatureC);
    if (r.hasHumidity) Serial.printf(" %u %%", r.humidity);  // not on 30.3181
    Serial.println();
  }
}
```

Examples:
- [`examples/serial_oled`](examples/serial_oled): OLED display + one JSON
  line per reading.
- [`examples/tune_floor`](examples/tune_floor): measures the best
  `ookFloor` for your location (see *Tuning* below).

### Other boards

Pass your own pins. DIO2 carries the data and must be wired to an
interrupt-capable GPIO; DIO1 is not used.

```cpp
klimalogg::ReceiverPins pins{/*sck*/ 5, /*miso*/ 19, /*mosi*/ 27,
                             /*cs*/ 18, /*reset*/ 23, /*dio0*/ 26, /*dio2*/ 32};
receiver.begin(pins);
```

Use an 868 MHz radio module and antenna. A 433 MHz SX1278 board will not work.

## API

| Call | What it does |
|------|--------------|
| `begin(pins, config)` | Set up the radio. 0 = ok, `kErrAlreadyRunning`, `kErrInvalidPins` or a RadioLib code. Safe to retry |
| `poll(&telegram)` | Process received signal; true when a telegram completed. Never waits |
| `end()` | Stop and release the radio |
| `setOokFloor(floor)` | Change the noise threshold at runtime |
| `takeStats()` | Edges seen / dropped since the last call |
| `instantRssi()` | Current signal level in dBm |

`Telegram::rssi` is an approximate peak in dBm (`kRssiUnknown` = -200 if not
sampled). `Telegram::result.status` is `Ok`, `BadCrc`, `BadType`, `BadTemperature` or
`ShortBuffer`. `Reading` holds `id`, `temperatureC`, `hasHumidity`,
`humidity`, `batteryLow`, `sequence` and `learning` (0–F while a sensor is
pairing, 8 once learned). `SensorTable` keeps the latest reading per sensor
(up to 8; when full, a sensor silent for 30 minutes makes room for a new one).

One running `Receiver` per board. Call `begin()` and `poll()` from the same
task and keep the CPU frequency fixed.

**Call `poll()` often.** Each telegram is ~470 edges in ~6 ms into a
4096-entry buffer. Blocking the polling task for long, or setting `ookFloor`
so low that noise gets through (~100,000 edges/s), drops edges and loses
telegrams; check `takeStats().droppedEdges`. When adding WiFi/MQTT, run the
network code in a separate FreeRTOS task. The examples print every rejected
telegram to serial (`KL_DEBUG_FRAMES`); one line takes ~4 ms, well within the
buffer.

`ReceiverConfig` options:

| Field | Default | Notes |
|-------|---------|-------|
| `frequencyMHz` | 868.30 | Sensors measured at 868.31 MHz |
| `bandwidthKHz` | 125 | Single-sided SX1276 filter; keep ≥ 62.5 |
| `ookFloor` | 90 | Noise threshold. Too low: noise causes ~100k interrupts/s. Too high: weak sensors drop out |

### Tuning

Flash [`examples/tune_floor`](examples/tune_floor) and open the serial
monitor. It scans idle noise per floor value, then counts valid telegrams per
sensor around the first quiet value. Measured on a T3 V1.6.1 indoors:

| `ookFloor` | Idle edges/s | 3 sensors (−30…−75 dBm) |
|-----------|--------------|------------------------|
| 15–60 | ~106,000 | received |
| 80 | ~90 | received |
| **90** | **0** | **received** |
| 110 | 0 | weakest sensor mostly lost |

Pick the highest value that still receives all your sensors, then go one
step (10) lower for margin.

## Protocol

Sensors transmit every ~10 s.

| Item       | Value |
|------------|-------|
| Frequency  | 868.31 MHz |
| Modulation | OOK (on/off keying) |
| Bit        | 26 µs (38400 baud): 13 µs carrier on, then 13 µs on = 1, off = 0 |
| Byte order | LSB first |
| Telegram   | `2D D4 \| ID ID \| sT TT \| HH \| BB \| SS \| 56 \| CRC` |
| CRC        | CRC8, poly 0x31, init 0, over ID..0x56 |

Fields: 15-bit ID (top bit ignored), BCD temperature in 0.1 °C with +40 °C
offset, humidity in % (0x6A = not available), low-battery flag in BB bit 7,
sequence number in SS bits 7:4.

The SX1276 runs in OOK continuous mode. An interrupt timestamps every edge
of the demodulator output on DIO2 and `PulseDecoder` turns pulse widths into
bits, finds the sync word and checks the CRC.

## Base station link

`ConsoleLink` talks to the KlimaLogg Pro **base station** (30.3039) the way
TFA's USB transceiver does, without the stick or a PC. `ConsoleProtocol`
parses its frames (current readings of all 9 channels, configuration with
the channel -> sensor ID mapping, history records) and `ConsoleSession`
downloads the history (up to 51,200 records) and sets the console clock.
`examples/console_listen` prints the current readings.

On air (measured with an RTL-SDR and confirmed on the ESP32):

| | |
|---|---|
| Frequency | 868.2815 MHz; tones at only ±5 kHz |
| Modulation | 2-FSK, ~19.9 kbit/s |
| Coding | NRZI + scrambler 1+x^12+x^17, HDLC framing, CRC-16/X-25 |
| Exchange | the console starts; the transceiver replies 10–150 ms later |

The small deviation makes the SX1276's crystal error matter: set
`ConsoleLinkConfig::frequencyMHz` to 868.2815 MHz plus your board's offset
(a T3 V1.6.1 needed +9 kHz). `ConsoleLink` needs DIO1 (the bit clock) in
`ReceiverPins`; it and `Receiver` share the radio, so `end()` one before
`begin()` of the other. The frame layout follows the weewx KlimaLogg driver
(`kl.py`) and was checked against real frames. Keep replies under the 1 %
duty cycle of 868.0–868.6 MHz (`ConsoleSession` tolerates skipped replies:
the console repeats the frame).

## Background

Earlier documentation describes these sensors as FSK with NRZS coding. On air
they are OOK, which is why FSK receivers and the KlimaLogg decoder in
rtl_433_ESP don't receive them (the latter also drops pulses shorter than
30 µs; these are 13 µs). The protocol details come from
[tfrec](https://github.com/baycom/tfrec), rtl_433's `klimalogg.c` and
`rtl_433 -A` captures of real sensors.

To see what's on air with an RTL-SDR dongle:
`rtl_433 -f 868.3M -s 1024k -R 150`

## Development

```
pio test -e native               # host unit tests, incl. a real rtl_433 capture
pio run -e t3_v161 -t upload     # build + flash examples/serial_oled
pio run -e tune_floor -t upload  # build + flash examples/tune_floor
pio run -e console_listen -t upload  # base-station listener
pio device monitor
```

If uploads fail with `Bad data checksum` or `Not enough data`, use another
USB port; monitor hubs and busy USB hubs (e.g. with an SDR dongle on them)
are known causes.

## License

MIT, see [LICENSE](LICENSE).
