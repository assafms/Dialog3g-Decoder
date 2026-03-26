# Arad Dialog 3G Water Meter Decoder

ESPHome custom component and PCB for reading Arad Dialog 3G water meters using an SI4432 (XL4432) RF module and an ESP8266/ESP32.

## Overview

The Arad Dialog 3G is a water meter deployed in Israel and other countries operating at 916.3 MHz (868 MHz in some regions). This project provides:

- **Custom PCB** connecting an XL4432 module to an ESP via SPI
- **ESPHome component** that receives and decodes meter packets
- **Single-packet GF(2) validation** for standard meters — detects RF bit errors without CRC
- **Two-packet validation** for non-standard meters (3D0C, x40) — no ID matrix needed

## Meter Groups

| Group | Bytes 8-9 | Validation | Notes |
|-------|-----------|------------|-------|
| Standard | `0x00 0x00` | Full GF(2) (single packet) | Residential meters, fully proven |
| x40 | `0x00 0x40` | Two-packet method | Likely commercial/street meters |
| 3D0C | `0x3D 0x0C` | Two-packet method | Older model |

All groups share the same consumption basis vectors. Standard meters have a fully proven ID basis matrix. Non-standard meters use different (unsolved) ID matrices, so they fall back to the two-packet method: if two packets from the same meter yield the same derived constant, both readings are confirmed valid.

## How It Works

The meter transmits a 21-byte FSK/Manchester-encoded packet every ~30 seconds containing the meter ID and consumption reading. For standard meters, the ESPHome component validates every received packet independently using a GF(2) linear function. For non-standard meters, it stores the derived constant from the first packet and validates subsequent packets by comparing constants. Only validated readings are published to Home Assistant.

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

## Packet Structure

| Byte(s) | Field | Description |
|---------|-------|-------------|
| 0-1 | Header | Static (`0x0A 0xEC`) |
| 2-4 | Type | Meter type / firmware variant |
| 5-7 | Meter ID | 3 bytes, big-endian |
| 8-9 | Group | `0x00 0x00` standard, `0x00 0x40` x40, `0x3D 0x0C` 3D0C |
| 10-12 | Consumption | 3 bytes, little-endian. Divide by 10 for m³ |
| 13-14 | Status | Usually `0x00 0x05` |
| 15-19 | Scramble | GF(2) integrity field |
| 20 | Trailer | High nibble only (low nibble unreliable due to Manchester timing drift) |

## GF(2) Scrambling Algorithm

Bytes 15-19 of each packet contain a 40-bit value that is a **linear function over GF(2)** of the meter ID and consumption reading:

```
scrambled = OFFSET ^ M_id(meter_id) ^ M_cons(consumption)
```

Where:
- **OFFSET** is a global 40-bit constant (`0xDF750DC2C0`)
- **M_id** is a 40x24 binary matrix applied to the 24-bit meter ID (proven for standard meters)
- **M_cons** is a 40x17 binary matrix applied to bits 0-16 of consumption (shared across all groups)
- **^** is bitwise XOR

### Consumption Basis Vectors (bits 0-16, all groups)

| Bit | Vector | Bit | Vector |
|-----|--------|-----|--------|
| 0 | `0x61B89FB6A0` | 9 | `0x408B817A30` |
| 1 | `0xE360308318` | 10 | `0xA1060D1A38` |
| 2 | `0xC6C12115C8` | 11 | `0x420D5A2788` |
| 3 | `0xAD9382E0F8` | 12 | `0x841AB44F10` |
| 4 | `0x7B374A3C50` | 13 | `0x0835A7BB10` |
| 5 | `0xF66E9478A0` | 14 | `0x106AC040E8` |
| 6 | `0xECDDE7D470` | 15 | `0x20D40FB718` |
| 7 | `0xF9AB805540` | 16 | `0x51AAF3D980` |
| 8 | `0xA045A72F80` | | |

Bits 0-14 proven via transition analysis. Bits 15-16 confirmed by multiple meters. Bits 17-23 unsolved for standard meters (no meters with consumption > 13107 m³ observed). All groups share these vectors.

### Meter ID Basis Vectors (bits 0-23, standard meters only)

| Bit | Vector | Bit | Vector |
|-----|--------|-----|--------|
| 0 | `0x456FF2CC60` | 12 | `0x3D5518F660` |
| 1 | `0x8ADE6AAE08` | 13 | `0x7AAA31ECC0` |
| 2 | `0x0149F2DC28` | 14 | `0xD544E30110` |
| 3 | `0x7FAE4CBD30` | 15 | `0xAA89092710` |
| 4 | `0x9694D8DA08` | 16 | `0x7503D28548` |
| 5 | `0x39DD1902E0` | 17 | `0xEA062A3C58` |
| 6 | `0x2E9694EEF8` | 18 | `0xD40D146B48` |
| 7 | `0x0000000000` | 19 | `0xA81B68C568` |
| 8 | `0x49D8C178F8` | 20 | `0x5037919928` |
| 9 | `0xB3A08D1FA8` | 21 | `0xA06EAC0498` |
| 10 | `0x475155C2F0` | 22 | `0x40DD972C00` |
| 11 | `0x8EA2AB85E0` | 23 | `0xA1AA21B658` |

Bits 0,1,4 proven by physical serial numbers. Bits 3,5,6 proven by physical meter readings (93D9E9, 082CD3, 5E2EE8). Bit 2 supported by RANSAC 71/71 — 100% for standard meters (no physical verification yet). Bit 7 confirmed zero. Bits 8-23 stable across all solves. Non-standard meters use different ID matrices (unsolved).

### Validation Methods

**Standard meters (single-packet):** Compute expected scramble from meter ID and consumption, compare to received bytes 15-19. Any mismatch = RF bit error. Works from the first packet.

**Non-standard meters (two-packet):** Derive constant = scrambled ^ M_cons(consumption). Store from first packet. If second packet gives same constant, both are valid. No ID matrix needed.

### Methodology

The algorithm was reverse-engineered through a strictly non-circular process:
1. Consumption basis vectors from same-meter transition analysis (ID-independent)
2. Per-meter constants via two-packet method (ID-independent)
3. ID basis matrix via RANSAC over 71 standard two-packet-confirmed meters (71/71, 100%)
4. Physical verification against 4 meters with known serial numbers
5. Field validation: 294 standard meters pass full validation (70% raw packet rate)

## Links

- [Dialog3g-Decoder-research](https://github.com/assafms/Dialog3g-Decoder-research) — Research repo with analysis scripts, Android collector app, and detailed protocol documentation
- [rtl_433 Protocol 260](https://github.com/merbanan/rtl_433) — SDR-based decoder
- [rtl_433 Issue #1992](https://github.com/merbanan/rtl_433/issues/1992) — Original protocol analysis
- [rtl_433 Discussion #3414](https://github.com/merbanan/rtl_433/discussions/3414) — Arad Sonata analysis

## License

This project is the result of independent reverse engineering for research and interoperability purposes. Not affiliated with Arad Ltd.
