# Arad Dialog 3G Water Meter Decoder

ESPHome custom component + PCB for reading Arad Dialog 3G water meters using an SI4432 (XL4432) RF module and an ESP8266/ESP32.

## What is this?

The Arad Dialog 3G is a water meter widely deployed in Israel (916.3 MHz) and other countries (868/916 MHz). This project provides:

- **Custom PCB** connecting an XL4432 module to an ESP via SPI
- **ESPHome component** that receives and validates meter packets
- **GF(2) packet validation** — a reverse-engineered integrity check that detects RF bit errors without needing CRC (which Arad disabled)

## How it works

The meter transmits a 21-byte packet every ~30 seconds containing the meter ID and consumption reading. The SI4432 receives the packet via FSK/Manchester encoding. The ESPHome component:

1. Receives the first packet from your meter and **learns** a per-meter validation constant
2. Validates every subsequent packet using a GF(2) linear scramble check (bytes 15-19)
3. Only publishes **validated** readings to Home Assistant — RF-corrupted packets are silently discarded

This replaces the old "wait for 2 identical readings" approach with a much stronger check. Two packets with *different* consumption values can confirm each other.

## Hardware

- ESP8266 or ESP32 (~$4)
- XL4432 / SI4432 RF module (~$2)
- Custom PCB (see `PCB/` directory) or manual wiring via SPI

Total cost: ~$6

See the original post for wiring details: [Reddit post](https://www.reddit.com/r/RTLSDR/comments/10nri4r/dialog3g_water_meter_reading_now_with_dedicated/)

## RF Configuration (Israeli version)

| Parameter | Value |
|-----------|-------|
| Frequency | 916.3 MHz |
| Modulation | FSK |
| Encoding | Manchester |
| Data Rate | 59.45 kbps |
| Freq Deviation | 175 kHz |
| Rx Bandwidth | 600 kHz |
| Preamble | 7 nibbles |
| Sync Word | 0x3E 0x69 |
| CRC | Disabled |

## Quick Start

1. Build the PCB or wire the XL4432 to the ESP via SPI
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

4. Find your meter ID: read the decimal number on the meter, convert to hex, reverse bytes. E.g., `12345678` → hex `0xBC614E` → reversed `0x4E61BC`

## Packet Sniffer Mode

To capture raw packets from all nearby meters (useful for debugging or research):

```yaml
sensor:
  - platform: xl4432_spi_sensor
    name: sniffer
    cs_pin: GPIO15
    meter_id: "0x000000"
    packet_sniff: true
```

All received packets are logged at INFO level. No data is published to Home Assistant.

## Packet Validation

The component uses a reverse-engineered GF(2) linear function to validate packets. Bytes 15-19 of each packet contain a 40-bit scramble that depends on both the meter ID and the consumption value. The validation:

- Learns the per-meter constant from the first received packet
- Validates all subsequent packets by checking that the scramble matches the expected value
- Detects bit errors in the meter ID, consumption, or scramble bytes
- Works for any consumption value

## Links

- [rtl_433 Protocol 260](https://github.com/merbanan/rtl_433) — SDR-based decoder (no packet validation)
- [rtl_433 Issue #1992](https://github.com/merbanan/rtl_433/issues/1992) — Original protocol analysis
- [Master Meter Dialog 3G Manual](https://www.manualslib.com/manual/1555781/Master-Meter-Dialog-3g.html)
