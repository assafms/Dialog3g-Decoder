# ESPHome Dialog 3G Component

Custom ESPHome component for receiving Arad Dialog 3G water meter data using an XL4432/SI4432 RF module.

## Features

- Configurable meter ID via YAML
- **GF(2) packet validation** — detects RF bit errors using reverse-engineered scramble check
- Learns per-meter constant automatically from first packet
- Packet sniffer mode for capturing all nearby meters
- Last-nibble masking (unreliable due to Manchester timing drift)

## Installation

Copy `custom_components/xl4432_spi_sensor/` to your ESPHome config directory:

```
config/
  esphome/
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
| `packet_sniff` | No | `false` | Log all packets, don't publish to HA |
| `cs_pin` | Yes | — | SPI chip select pin |
| `accuracy_decimals` | No | `1` | Decimal places for the reading |

### Finding Your Meter ID

1. Read the decimal serial number from your water meter
2. Convert to hex: e.g., `12345678` → `0xBC614E`
3. Reverse byte order: `0xBC614E` → `0x4E61BC`
4. Use in YAML: `meter_id: "0x4E61BC"`

## How Validation Works

The old method required 2 identical consecutive readings before publishing. The new GF(2) validation is stronger:

1. **First packet**: the component derives a 40-bit constant from the consumption and scramble bytes (15-19). This constant is stored but not yet confirmed.
2. **Second packet**: the constant is re-derived. If it matches the stored value, the constant is **locked in** and the reading is published. If different, the new constant replaces the old one and the process restarts.
3. **All subsequent packets**: validated against the locked constant. Matches are published, mismatches are discarded with a warning log.

This means:
- Two packets with **different** consumption values can validate each other
- Any bit flip in meter ID, consumption, or scramble bytes is detected
- No need to wait for the exact same reading twice

## Packet Sniffer Mode

Set `packet_sniff: true` to log all received packets at INFO level without filtering by meter ID or publishing to Home Assistant. Useful for:
- Discovering nearby meter IDs
- Collecting data for protocol research
- Debugging reception issues

## SPI Wiring

| ESP Pin | XL4432 Pin |
|---------|------------|
| GPIO14 (CLK) | SCK |
| GPIO13 (MOSI) | SDI |
| GPIO12 (MISO) | SDO |
| GPIO15 (CS) | nSEL |
| GPIO5 | nIRQ |
