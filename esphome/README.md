# ESPHome Dialog 3G Component

Custom ESPHome component for receiving and validating Arad Dialog 3G water meter transmissions using an XL4432/SI4432 RF module.

## Features

- Unified LFSR validation with up to 3-bit error correction — all meter groups, single model
- Configurable meter ID via YAML
- Packet sniffer mode for discovering meters and debugging
- Automatic low-nibble masking on byte 20 (unreliable due to Manchester timing drift)

## Installation

Copy `custom_components/xl4432_spi_sensor/` to your ESPHome config directory:

```
config/
  your_device.yaml
  custom_components/
    xl4432_spi_sensor/
      __init__.py
      sensor.py
      xl4432.cpp
      xl4432.h
      xl4432_spi_sensor.cpp
      xl4432_spi_sensor.h
```

## Configuration

```yaml
sensor:
  - platform: xl4432_spi_sensor
    name: water_meter
    cs_pin: GPIO15
    meter_id: "0x4E61BC"
    accuracy_decimals: 1
```

### Options

| Option | Required | Default | Description |
|--------|----------|---------|-------------|
| `meter_id` | Yes | — | Meter ID as hex string (e.g., `"0x4E61BC"`) |
| `cs_pin` | Yes | — | SPI chip select pin |
| `packet_sniff` | No | `false` | Log all packets without filtering or publishing |
| `tcp_server` | No | `false` | Stream packets over WiFi (TCP port 4321) for the Android app |
| `accuracy_decimals` | No | `1` | Decimal places for the consumption reading |

### Finding Your Meter ID

1. Read the decimal serial number printed on the meter
2. Convert to hexadecimal
3. Reverse the byte order
4. Use in YAML config

Example: decimal `5136830` -> hex `0x4E61BE` -> reversed `0xBE614E` -> `meter_id: "0xBE614E"`

## How Validation Works

Bytes 15-19 of each packet contain a 40-bit scrambled value computed via a unified LFSR-based GF(2) linear function of all data bytes 0-14. The component computes the expected scramble and compares it to the received value. If they don't match, it attempts to correct up to 3 bit errors using syndrome matching across 160 bit positions.

- **Valid**: expected scramble matches — reading is published to Home Assistant
- **Corrected (1-3 bits)**: bit errors detected and fixed — corrected reading is published
- **Invalid**: more than 3 bit errors — packet is discarded (logged as warning)
- **ID mismatch**: packet is from a different meter — silently ignored

Works for all meter groups (STD, x40/Sonata, 3D0C) with a single universal offset. No per-group configuration needed.

## Packet Sniffer Mode

Set `packet_sniff: true` to log all received packets at INFO level without filtering or validation. Useful for:

- Discovering nearby meter IDs
- Collecting packet data for analysis
- Debugging RF reception

## Wireless Connection (TCP Server)

Set `tcp_server: true` to stream all received packets over WiFi to the Android collector app — no USB cable needed.

```yaml
sensor:
  - platform: xl4432_spi_sensor
    name: water_meter
    cs_pin: GPIO15
    meter_id: "0x93D9E9"
    packet_sniff: true
    tcp_server: true
```

Setup:
1. Enable phone hotspot
2. Configure ESPHome WiFi with the hotspot SSID and password
3. Power on the ESP — it connects to the hotspot
4. Open the Android app — it auto-discovers the ESP on port 4321

The TCP server streams packets in the same format as the serial log, so both USB and WiFi connections work with the same app.

## SPI Wiring

| ESP Pin | XL4432 Pin | Function |
|---------|------------|----------|
| GPIO14 | SCK | SPI Clock |
| GPIO13 | SDI | SPI MOSI |
| GPIO12 | SDO | SPI MISO |
| GPIO15 | nSEL | Chip Select |
| GPIO5 | nIRQ | Interrupt |
