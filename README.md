# Arad Dialog 3G Water Meter Decoder

ESPHome custom component and PCB for reading Arad Dialog 3G water meters using an SI4432 (XL4432) RF module and an ESP8266/ESP32.

## Overview

The Arad Dialog 3G is a water meter deployed in Israel and other countries operating at 916.3 MHz (868 MHz in some regions). This project provides:

- **Custom PCB** connecting an XL4432 module to an ESP via SPI
- **ESPHome component** that receives and decodes meter packets
- **Unified LFSR validation** for all meter groups — single model, no per-group configuration
- **3-bit error correction** — recovers packets with up to 3 corrupted bits
- **Two-packet confirmation** as fallback when error correction is insufficient

## Meter Groups

| Group | Bytes 8-9 | Cons Divisor | Notes |
|-------|-----------|-------------|-------|
| Standard | `0x00 0x00` | ÷10 (0.1 m³) | Residential meters |
| Sonata/x40 | `0x00 0x40` | ÷1000 (1 liter) | Commercial/street meters |
| 3D0C | `0x3D 0x0C` | ÷10 (0.1 m³) | Older model |

All groups use the same unified LFSR model with a single universal offset (`0x6FF11521E8`). No per-group detection or configuration needed — bytes 8-9 are just more data bits in the LFSR.

## How It Works

The meter transmits a 21-byte FSK/Manchester-encoded packet every ~30 seconds containing the meter ID and consumption reading. The ESPHome component validates every packet using a unified LFSR-based GF(2) model that works for all meter groups. It also applies up to 3-bit error correction to recover packets with minor RF corruption. Only validated readings are published to Home Assistant.

## Hardware

| Component | Approximate Cost |
|-----------|-----------------|
| ESP8266 or ESP32 | ~$4 |
| XL4432 / SI4432 RF module | ~$2 |
| Custom PCB (see `PCB/`) or manual SPI wiring | ~$0.50 |

See `PCB/` for Eagle source files, Gerber outputs, and board photos.

## RF Parameters

| Parameter | Value |
|-----------|-------|
| Frequency | 916.3 MHz |
| Modulation | FSK |
| Encoding | Manchester (IEEE 802.3) |
| Data Rate | 59.45 kbps |
| Frequency Deviation | 175 kHz |
| Receiver Bandwidth | 600 kHz |
| Transmission Interval | ~30 seconds |
| Preamble | 7 nibbles |
| Sync Word | `0x3E 0x69` |
| Payload | 21 bytes, fixed length, no CRC |

## Quick Start

1. Build the PCB or wire the XL4432 to the ESP via SPI (see `esphome/README.md` for pinout)
2. Copy `esphome/custom_components/xl4432_spi_sensor/` to your ESPHome `custom_components/` directory
3. Add to your YAML:

```yaml
sensor:
  - platform: xl4432_spi_sensor
    name: water_meter
    cs_pin: GPIO15
    meter_id: "0x4E61BC"
    accuracy_decimals: 1
```

4. Find your meter ID: read the decimal serial number on the meter, convert to hex. Example: decimal `5136830` → hex `0x4E61BE`. The packet transmits this big-endian.

## Packet Sniffer Mode

To capture raw packets from all nearby meters:

```yaml
sensor:
  - platform: xl4432_spi_sensor
    name: sniffer
    cs_pin: GPIO15
    meter_id: "0x000000"
    packet_sniff: true
```

All received packets are logged at INFO level. No validation or filtering is performed.

## Wireless Connection

Add `tcp_server: true` to stream packets over WiFi to the Android collector app instead of USB:

```yaml
sensor:
  - platform: xl4432_spi_sensor
    name: sniffer
    cs_pin: GPIO15
    meter_id: "0x000000"
    packet_sniff: true
    tcp_server: true
```

Enable phone hotspot, configure ESPHome WiFi with the hotspot credentials, and the Android app auto-discovers the ESP on port 4321.

## Packet Structure

| Byte(s) | Field | Description |
|---------|-------|-------------|
| 0-1 | Header | Static (`0x0A 0xEC`) |
| 2-4 | Type | Meter type / firmware variant |
| 5-7 | Meter ID | 3 bytes, big-endian |
| 8-9 | Group | `0x00 0x00` standard, `0x00 0x40` x40, `0x3D 0x0C` 3D0C |
| 10-12 | Consumption | 3 bytes, little-endian. STD/3D0C: ÷10 for m³. Sonata/x40: ÷1000 for m³ |
| 13-14 | Status | Usually `0x00 0x05` |
| 15-19 | Scramble | GF(2) integrity field |
| 20 | Trailer | High nibble only (low nibble unreliable due to Manchester timing drift) |

## GF(2) Scrambling Algorithm

Bytes 15-19 of each packet contain a 40-bit value that is a **linear function over GF(2)** of all data bytes 0-14, computed via a single 40-bit LFSR:

```
scrambled = OFFSET ^ ⨁(LFSR_vec[pos] for each set bit in bytes 0-14)
```

Where:
- **OFFSET** = `0x6FF11521E8` — universal, same for all meter groups
- All 120 basis vectors are consecutive states of a 40-bit LFSR (seed `0x51AAF3D980`, 3 feedback taps)
- Forward chain: bytes 12→11→10→9→8→7→6→5→4→3→2→1→0 (104 states)
- Backward chain: bytes 13→14 (16 states, stored as lookup)
- **^** is bitwise XOR

### Validation

Single model for all groups: compute expected scramble via LFSR from bytes 0-14, compare to received bytes 15-19. Up to 3 corrupted bits can be automatically corrected using 160 syndrome bits (120 data + 40 scramble). Achieves 80% STD, 78% x40, 83% 3D0C validation rate. See `PROTOCOL.md` for full details and basis vector tables.

## Links

- [Dialog3g-Decoder-research](https://github.com/assafms/Dialog3g-Decoder-research) — Research repo with analysis scripts, Android collector app, and detailed protocol documentation
- [rtl_433 Protocol 260](https://github.com/merbanan/rtl_433) — SDR-based decoder
- [rtl_433 Issue #1992](https://github.com/merbanan/rtl_433/issues/1992) — Original protocol analysis
- [rtl_433 Discussion #3414](https://github.com/merbanan/rtl_433/discussions/3414) — Arad Sonata analysis

## License

This project is the result of independent reverse engineering for research and interoperability purposes. Not affiliated with Arad Ltd.
