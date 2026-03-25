# ESPHome Dialog 3G Component

Custom ESPHome component for receiving and validating Arad Dialog 3G water meter transmissions using an XL4432/SI4432 RF module.

## Features

- Single-packet GF(2) validation — every packet is independently verified
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
| `accuracy_decimals` | No | `1` | Decimal places for the consumption reading |

### Finding Your Meter ID

1. Read the decimal serial number printed on the meter
2. Convert to hexadecimal
3. Reverse the byte order
4. Use in YAML config

Example: decimal `5136830` -> hex `0x4E61BE` -> reversed `0xBE614E` -> `meter_id: "0xBE614E"`

## How Validation Works

Bytes 15-19 of each packet contain a 40-bit scrambled value that is a deterministic GF(2) linear function of the meter ID and consumption reading. The component computes the expected scramble from the packet data and compares it to the received value.

- **Valid**: expected scramble matches received — reading is published to Home Assistant
- **Invalid**: mismatch detected — packet is discarded (logged as warning)
- **ID mismatch**: packet is from a different meter — silently ignored

Every packet is validated on its own. No learning phase, no warm-up, no dependency on previous packets.

## Packet Sniffer Mode

Set `packet_sniff: true` to log all received packets at INFO level without filtering or validation. Useful for:

- Discovering nearby meter IDs
- Collecting packet data for analysis
- Debugging RF reception

## SPI Wiring

| ESP Pin | XL4432 Pin | Function |
|---------|------------|----------|
| GPIO14 | SCK | SPI Clock |
| GPIO13 | SDI | SPI MOSI |
| GPIO12 | SDO | SPI MISO |
| GPIO15 | nSEL | Chip Select |
| GPIO5 | nIRQ | Interrupt |
