# Arad Dialog 3G Water Meter Decoder

ESPHome custom component and PCB for reading Arad Dialog 3G water meters using an SI4432 (XL4432) RF module and an ESP8266/ESP32.

## Overview

The Arad Dialog 3G is a water meter deployed in Israel and other countries operating at 916.3 MHz (868 MHz in some regions). This project provides:

- **Custom PCB** connecting an XL4432 module to an ESP via SPI
- **ESPHome component** that receives and decodes meter packets
- **Single-packet GF(2) validation** — a reverse-engineered integrity check that detects RF bit errors without CRC

## How It Works

The meter transmits a 21-byte FSK/Manchester-encoded packet every ~11 seconds containing the meter ID and consumption reading. The ESPHome component validates every received packet independently using a GF(2) linear function over the meter ID and consumption value. Only validated readings are published to Home Assistant.

Unlike simpler approaches that require multiple matching readings, this method detects any bit error in a single packet — no learning phase or prior data needed.

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

4. Find your meter ID: read the decimal serial number on the meter, convert to hex, reverse the byte order. Example: decimal `5136830` converts to hex `0x4E61BE`, reverse bytes to get `0xBE614E`.

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

All received packets are logged at INFO level. No validation is performed and no data is published to Home Assistant.

## Packet Validation

Each 21-byte packet contains a 40-bit scrambled field (bytes 15-19) that is a deterministic function of the meter ID and consumption value. The validation computes the expected scramble using the full GF(2) matrix and compares it to the received value. Any bit error in the meter ID, consumption, or scramble bytes causes a mismatch, and the packet is discarded.

This provides strong error detection from the very first packet — no warm-up period or repeated readings required.

## Packet Structure

| Byte(s) | Field | Description |
|---------|-------|-------------|
| 0-1 | Header | Static (`0x0A 0xEC`) |
| 2-4 | Type | Meter type / firmware variant |
| 5-7 | Meter ID | 3 bytes, little-endian |
| 8-9 | Flags | Usually `0x00 0x00` |
| 10-12 | Consumption | 3 bytes, little-endian. Divide by 10 for m3 |
| 13-14 | Status | Usually `0x00 0x05` |
| 15-19 | Scramble | GF(2) integrity field |
| 20 | Trailer | High nibble only (low nibble unreliable) |

## GF(2) Scrambling Algorithm

Bytes 15-19 of each packet contain a 40-bit value that is a **linear function over GF(2)** (the binary field) of the meter ID and consumption reading:

```
scrambled = OFFSET ^ M_id(meter_id) ^ M_cons(consumption)
```

Where:
- **OFFSET** is a global 40-bit constant (`0x2CA2C00A20`)
- **M_id** is a 40x24 binary matrix applied to the 24-bit meter ID
- **M_cons** is a 40x15 binary matrix applied to the lower 15 bits of the consumption value
- **^** is bitwise XOR (all arithmetic is over GF(2))

Each matrix is defined by its basis vectors — one 40-bit vector per input bit. If bit _i_ of the input is set, the corresponding basis vector is XORed into the result.

### Consumption Basis Vectors (bits 0-14)

| Bit | Vector | Bit | Vector |
|-----|--------|-----|--------|
| 0 | `0x61B89FB6A0` | 8 | `0xA045A72F80` |
| 1 | `0xE360308318` | 9 | `0x408B817A30` |
| 2 | `0xC6C12115C8` | 10 | `0xA1060D1A38` |
| 3 | `0xAD9382E0F8` | 11 | `0x420D5A2788` |
| 4 | `0x7B374A3C50` | 12 | `0x841AB44F10` |
| 5 | `0xF66E9478A0` | 13 | `0x0835A7BB10` |
| 6 | `0xECDDE7D470` | 14 | `0x106AC040E8` |
| 7 | `0xF9AB805540` | | |

Consumption bits 15-23 have no effect on the scrambled output.

### Meter ID Basis Vectors (bits 0-23)

| Bit | Vector | Bit | Vector |
|-----|--------|-----|--------|
| 0 | `0x456FF2CC60` | 12 | `0x3D5518F660` |
| 1 | `0x8ADE6AAE08` | 13 | `0x7AAA31ECC0` |
| 2 | `0xCE131FAF10` | 14 | `0xD544E30110` |
| 3 | `0x43236C06E8` | 15 | `0xAA89092710` |
| 4 | `0x6D4316D2E0` | 16 | `0x7503D28548` |
| 5 | `0xFE87F7B1D0` | 17 | `0xEA062A3C58` |
| 6 | `0x121BB45520` | 18 | `0xD40D146B48` |
| 7 | `0x348D237BD0` | 19 | `0xA81B68C568` |
| 8 | `0x49D8C178F8` | 20 | `0x5037919928` |
| 9 | `0xB3A08D1FA8` | 21 | `0xA06EAC0498` |
| 10 | `0x475155C2F0` | 22 | `0x40DD972C00` |
| 11 | `0x8EA2AB85E0` | 23 | `0xA1AA21B658` |

### Validation

To validate a packet, compute the expected scramble from the meter ID (bytes 5-7) and consumption (bytes 10-12), then compare to the actual bytes 15-19. A mismatch means at least one bit was corrupted during RF transmission. This catches errors that would otherwise produce a silently wrong meter reading.

### Methodology

The algorithm was reverse-engineered by:
1. Isolating consumption basis vectors from sequential packet pairs where only the reading changed
2. Solving the ID basis matrix via RANSAC over 57 meters with known constants
3. Constraining against physically verified meter serial numbers
4. Field-validating across 86 independent meters

## Links

- [rtl_433 Protocol 260](https://github.com/merbanan/rtl_433) — SDR-based decoder
- [rtl_433 Issue #1992](https://github.com/merbanan/rtl_433/issues/1992) — Original protocol analysis
- [rtl_433 Discussion #3414](https://github.com/merbanan/rtl_433/discussions/3414) — Arad Sonata analysis

## License

This project is the result of independent reverse engineering for research and interoperability purposes. Not affiliated with Arad Ltd.
