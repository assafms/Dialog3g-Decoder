# PCB Design

Custom PCB connecting an XL4432 (SI4432) RF module to an ESP8266/ESP32 via SPI.

## Contents

| Directory | Description |
|-----------|-------------|
| `Eagle/` | Source schematic and board files (Eagle CAD) |
| `Gerber/` | Production-ready Gerber files (PCBWay compatible) |

## Assembly Notes

- **R1, R2, R3**: Use 0-ohm resistors or solder bridges (short the pads)
- See `schematic.png` and `pcb.png` for reference
- `completed board.png` shows an assembled example

## Bill of Materials

See `Eagle/BOM` for the full component list.
