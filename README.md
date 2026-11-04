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
lib_deps = https://github.com/coolbox13/klimalogg_pro_esp.git#v1.0.0
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
    Serial.printf("%04X %.1f C %u %%\n", r.id, r.temperatureC, r.humidity);
  }
}
```

A full example with OLED display and JSON output is in
[`examples/serial_oled`](examples/serial_oled).

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
| `begin(pins, config)` | Set up the radio. Returns a RadioLib status code, 0 = ok. Safe to retry |
| `poll(&telegram)` | Process received signal; true when a telegram completed. Never waits |
| `end()` | Stop and release the radio |
| `takeStats()` | Edges seen / dropped since the last call |
| `instantRssi()` | Current signal level in dBm |

`Telegram::rssi` is an approximate peak in dBm (`kRssiUnknown` = -200 if not
sampled). `Telegram::result.status` is `Ok`, `BadCrc`, `BadType`, `BadTemperature` or
`ShortBuffer`. `Reading` holds `id`, `temperatureC`, `hasHumidity`,
`humidity`, `batteryLow` and `sequence`. `SensorTable` keeps the latest
reading per sensor if you want it.

One `Receiver` per board. Call `begin()` and `poll()` from the same task and
keep the CPU frequency fixed.

**Call `poll()` often.** The radio produces up to ~100,000 edges per second
(noise included) into a 4096-entry buffer. Blocking the loop for more than a
few tens of milliseconds drops edges and loses telegrams; check
`takeStats().droppedEdges`. When adding WiFi/MQTT, run the network code in a
separate FreeRTOS task.

`ReceiverConfig` options:

| Field | Default | Notes |
|-------|---------|-------|
| `frequencyMHz` | 868.30 | Sensors measured at 868.31 MHz |
| `bandwidthKHz` | 125 | Single-sided SX1276 filter; keep ≥ 62.5 |
| `ookFloor` | 15 | Raise to cut idle noise edges, lower for weak sensors |

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
pio test -e native            # host unit tests, incl. a real rtl_433 capture
pio run -e t3_v161 -t upload  # build + flash examples/serial_oled
pio device monitor
```

If uploads fail with `Bad data checksum`, use another USB port; monitor
built-in hubs are a known cause.

## License

MIT, see [LICENSE](LICENSE).
