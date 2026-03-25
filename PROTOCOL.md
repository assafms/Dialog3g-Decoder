# Arad Dialog 3G Water Meter — RF Protocol Reference

Reverse-engineered protocol documentation for the Arad Dialog 3G water meter (Israeli version).

> **Disclaimer**: This protocol is proprietary to Arad Ltd. This document is the result of independent reverse engineering for research and interoperability purposes. Not affiliated with Arad.

---

## 1. RF / Physical Layer

| Parameter            | Value              |
|----------------------|--------------------|
| Frequency            | 916.3 MHz          |
| Modulation           | FSK                |
| Encoding             | Manchester (zero-bit variant, IEEE 802.3) |
| Data Rate            | 59.45 kbps         |
| Frequency Deviation  | 175 kHz            |
| Receiver Bandwidth   | 600 kHz            |
| Transmission Interval| ~11 seconds        |
| Preamble             | 7 nibbles, pattern `01010` |
| Sync Word            | `0x3E 0x69`        |
| Payload              | 21 bytes, fixed length, no CRC |

---

## 2. Packet Layout

| Byte  | Field             | Notes |
|-------|-------------------|-------|
| 0–1   | Static header     | Always `0x0A 0xEC` |
| 2     | Header variant    | Usually `0x7A`. `0x6A` seen on some meters |
| 3–4   | Type bytes        | Usually `0xC8 0x4B`. `0x6B` for some meter types |
| 5–7   | **Meter ID**      | 3 bytes, big-endian (see below) |
| 8–9   | Flags / unknown   | `0x00 0x00` standard. `0x3D 0x0C` on some types |
| 10–12 | **Consumption**   | 3 bytes, little-endian, 24-bit. Divide by 10 for m³ |
| 13    | Unknown           | Almost always `0x00` |
| 14    | Type / firmware   | `0x05` standard, `0x08` on `0x6B` meters |
| 15–19 | **Scrambled data** | GF(2) linear function of meter ID + consumption |
| 20    | Trailer           | High nibble varies. **Low nibble unreliable** (Manchester timing drift) |

### Meter ID Encoding

The physical decimal serial number is converted to hex and transmitted **big-endian** (MSB first).

Example: serial `09444602` → hex `0x901CFA` → transmitted as bytes `90 1C FA`.

**Important**: The meter ID for the scramble algorithm uses big-endian byte order:
```
meter_id = (pkt[5] << 16) | (pkt[6] << 8) | pkt[7]
```

Note: consumption is still **little-endian**.

### Consumption

```
reading = byte[10] | (byte[11] << 8) | (byte[12] << 16)
volume_m3 = reading / 10.0
```

---

## 3. Scrambled Data (Bytes 15–19)

### Algorithm

Bytes 15–19 are a **linear function over GF(2)** of both the meter ID and the consumption reading:

```
scrambled = OFFSET ⊕ M_id(meter_id) ⊕ M_cons(consumption)
```

Where:
- `OFFSET` = global 40-bit constant
- `M_id` = 40×24 binary matrix applied to the 24-bit meter ID
- `M_cons` = 40×15 binary matrix applied to the lower 15 bits of consumption
- `⊕` = bitwise XOR (all arithmetic is over GF(2))

### Constants

```
OFFSET = 0xDF750DC2C0
```

### Consumption Basis Vectors (M_cons)

Applied to bits 0–14 of the consumption value. Bits 15–23 have no effect (zero vectors).

| Bit | Vector             |
|-----|--------------------|
| 0   | `0x61B89FB6A0`     |
| 1   | `0xE360308318`     |
| 2   | `0xC6C12115C8`     |
| 3   | `0xAD9382E0F8`     |
| 4   | `0x7B374A3C50`     |
| 5   | `0xF66E9478A0`     |
| 6   | `0xECDDE7D470`     |
| 7   | `0xF9AB805540`     |
| 8   | `0xA045A72F80`     |
| 9   | `0x408B817A30`     |
| 10  | `0xA1060D1A38`     |
| 11  | `0x420D5A2788`     |
| 12  | `0x841AB44F10`     |
| 13  | `0x0835A7BB10`     |
| 14  | `0x106AC040E8`     |

### Meter ID Basis Vectors (M_id)

Applied to bits 0–23 of the meter ID.

| Bit | Vector             | Confidence |
|-----|--------------------|------------|
| 0   | `0x456FF2CC60`     | Solid      |
| 1   | `0x8ADE6AAE08`     | Solid      |
| 2   | `0x35AD159778`     | RANSAC 105/126 |
| 3   | `0x4B4AABF660`     | RANSAC 105/126 |
| 4   | `0x9694D8DA08`     | RANSAC 105/126 |
| 5   | `0x0D39FE49B0`     | RANSAC 105/126 |
| 6   | `0x1A7273A5A8`     | RANSAC 105/126 |
| 7   | `0x34E4E74B50`     | RANSAC 105/126 |
| 8   | `0x49D8C178F8`     | Proven     |
| 9   | `0xB3A08D1FA8`     | Proven     |
| 10  | `0x475155C2F0`     | Proven     |
| 11  | `0x8EA2AB85E0`     | Proven     |
| 12  | `0x3D5518F660`     | Proven     |
| 13  | `0x7AAA31ECC0`     | Proven     |
| 14  | `0xD544E30110`     | Proven     |
| 15  | `0xAA89092710`     | Proven     |
| 16  | `0x7503D28548`     | Proven     |
| 17  | `0xEA062A3C58`     | Proven     |
| 18  | `0xD40D146B48`     | Proven     |
| 19  | `0xA81B68C568`     | Proven     |
| 20  | `0x5037919928`     | Proven     |
| 21  | `0xA06EAC0498`     | Proven     |
| 22  | `0x40DD972C00`     | Proven     |
| 23  | `0xA1AA21B658`     | Proven     |

**Confidence levels:**
- **Proven**: Confirmed via physically verified meter serial numbers and/or stable across all RANSAC solves with 86+ independent field validations.
- **High**: RANSAC majority vote (47/56 meters agree, 84%), constrained by verified meters. Bits 3, 6, 7 require additional meters with those bits = 0 for full proof.

---

## 4. How to Use

### Full Validation (Recommended)

If you know the meter ID (from bytes 5–7), validate any packet:

```
expected = OFFSET ⊕ M_id(meter_id) ⊕ M_cons(consumption)
valid = (expected == bytes[15:19])
```

This detects any RF bit error in the meter ID, consumption, or scrambled bytes.

### Two-Packet Method (No Prior Knowledge)

Useful when the ID basis matrix is not available, or for maximum reliability:

1. Receive two packets from the same meter (match bytes 5–7).
2. For each, derive the per-meter constant:
   ```
   constant = scrambled ⊕ M_cons(consumption)
   ```
3. If both constants match → both packets are valid and the constant is confirmed.
4. Store the constant for future single-packet validation of this meter.

### C Implementation

```c
#include <stdint.h>
#include <stdbool.h>

static const uint64_t OFFSET = 0xDF750DC2C0ULL;

static const uint64_t CONS_BASIS[15] = {
    0x61B89FB6A0ULL, 0xE360308318ULL, 0xC6C12115C8ULL, 0xAD9382E0F8ULL,
    0x7B374A3C50ULL, 0xF66E9478A0ULL, 0xECDDE7D470ULL, 0xF9AB805540ULL,
    0xA045A72F80ULL, 0x408B817A30ULL, 0xA1060D1A38ULL, 0x420D5A2788ULL,
    0x841AB44F10ULL, 0x0835A7BB10ULL, 0x106AC040E8ULL,
};

static const uint64_t ID_BASIS[24] = {
    0x456FF2CC60ULL, 0x8ADE6AAE08ULL, 0x35AD159778ULL, 0x4B4AABF660ULL,
    0x9694D8DA08ULL, 0x0D39FE49B0ULL, 0x1A7273A5A8ULL, 0x34E4E74B50ULL,
    0x49D8C178F8ULL, 0xB3A08D1FA8ULL, 0x475155C2F0ULL, 0x8EA2AB85E0ULL,
    0x3D5518F660ULL, 0x7AAA31ECC0ULL, 0xD544E30110ULL, 0xAA89092710ULL,
    0x7503D28548ULL, 0xEA062A3C58ULL, 0xD40D146B48ULL, 0xA81B68C568ULL,
    0x5037919928ULL, 0xA06EAC0498ULL, 0x40DD972C00ULL, 0xA1AA21B658ULL,
};

/* Compute expected scrambled bytes from meter ID and consumption.
   meter_id is big-endian: (pkt[5] << 16) | (pkt[6] << 8) | pkt[7]
   consumption is little-endian: pkt[10] | (pkt[11] << 8) | (pkt[12] << 16) */
uint64_t expected_scramble(uint32_t meter_id, uint32_t consumption) {
    uint64_t result = OFFSET;
    for (int i = 0; i < 24; i++) {
        if (meter_id & (1 << i))
            result ^= ID_BASIS[i];
    }
    for (int i = 0; i < 15; i++) {
        if (consumption & (1 << i))
            result ^= CONS_BASIS[i];
    }
    return result;
}

/* Validate a packet */
bool validate(uint32_t meter_id, uint32_t consumption, uint64_t scrambled) {
    return expected_scramble(meter_id, consumption) == scrambled;
}

/* Derive per-meter constant (for two-packet method) */
uint64_t derive_constant(uint32_t consumption, uint64_t scrambled) {
    uint64_t result = scrambled;
    for (int i = 0; i < 15; i++) {
        if (consumption & (1 << i))
            result ^= CONS_BASIS[i];
    }
    return result;
}
```

### Python Implementation

```python
OFFSET = 0xDF750DC2C0

CONS_BASIS = [
    0x61B89FB6A0, 0xE360308318, 0xC6C12115C8, 0xAD9382E0F8,
    0x7B374A3C50, 0xF66E9478A0, 0xECDDE7D470, 0xF9AB805540,
    0xA045A72F80, 0x408B817A30, 0xA1060D1A38, 0x420D5A2788,
    0x841AB44F10, 0x0835A7BB10, 0x106AC040E8,
]

ID_BASIS = [
    0x456FF2CC60, 0x8ADE6AAE08, 0x35AD159778, 0x4B4AABF660,
    0x9694D8DA08, 0x0D39FE49B0, 0x1A7273A5A8, 0x34E4E74B50,
    0x49D8C178F8, 0xB3A08D1FA8, 0x475155C2F0, 0x8EA2AB85E0,
    0x3D5518F660, 0x7AAA31ECC0, 0xD544E30110, 0xAA89092710,
    0x7503D28548, 0xEA062A3C58, 0xD40D146B48, 0xA81B68C568,
    0x5037919928, 0xA06EAC0498, 0x40DD972C00, 0xA1AA21B658,
]

def expected_scramble(meter_id, consumption):
    r = OFFSET
    for i in range(24):
        if meter_id & (1 << i):
            r ^= ID_BASIS[i]
    for i in range(15):
        if consumption & (1 << i):
            r ^= CONS_BASIS[i]
    return r

def validate(meter_id, consumption, scrambled):
    return expected_scramble(meter_id, consumption) == scrambled

def derive_constant(consumption, scrambled):
    r = scrambled
    for i in range(15):
        if consumption & (1 << i):
            r ^= CONS_BASIS[i]
    return r
```

---

## 5. Confidence & Validation

| Component | Status | Evidence |
|-----------|--------|----------|
| Consumption basis (bits 0–14) | **Proven** | 370+ sequential readings across multiple meters, 100% match |
| Consumption basis (bits 15–23) | **Zero** | High consumption bits have no effect on scrambled data |
| ID basis (bits 8–23) | **Proven** | Stable across all RANSAC solves, 86 independent field validations |
| ID basis (bits 0–2, 4–5) | **Proven** | Constrained by physically verified meter serial numbers |
| ID basis (bits 3, 6–7) | **High** | RANSAC 47/56 agreement (84%), 86 field validations |
| Full validation | **61%** raw packet pass rate | Remaining 39% are RF-corrupted packets (bit errors in ID, consumption, or scramble) |
| Two-packet method | **Proven** | Works on all standard meter types |

### Methodology

The scrambling algorithm was reverse-engineered through:

1. **Consumption basis**: Derived from sequential packet pairs where only the consumption changed, isolating each bit's contribution.
2. **ID basis (bits 8–23)**: Solved via RANSAC over 57 meters with consumption-verified constants. Stable across 50,000+ random iterations.
3. **ID basis (bits 0–7)**: Constrained by two physically verified meter serial numbers (bits 0, 1, 2, 4, 5 proven). Remaining bits (3, 6, 7) determined by majority vote across 56 equations with 84% agreement.
4. **Field validation**: 86 unique meters passed full validation during walk testing, providing independent confirmation.

---

## 6. Known Limitations

- **Bytes 2–4**: Vary across meters. Purpose unknown (possibly hardware/firmware version).
- **Bytes 8–9**: `0x3D 0x0C` on some meter types. These meters use the same consumption basis but a **different ID basis / OFFSET** — not yet solved.
- **Byte 14**: `0x05` standard, `0x08` on `0x6B`-type meters. May indicate meter generation.
- **Byte 20**: Low nibble unreliable due to Manchester timing drift. Should be masked.
- **ID basis bits 3, 6, 7**: High confidence but not yet proven by physical serial verification. Requires a meter with any of these bits = 0 in its ID.
- **Regional variants**: Only tested on Israeli version (916.3 MHz). US/European versions may use different frequencies and/or different basis vectors.

---

## 7. References

- [Dialog3g-Decoder](https://github.com/assafms/Dialog3g-Decoder) — ESPHome + SI4432 hardware implementation
- [rtl_433](https://github.com/merbanan/rtl_433) — SDR decoder (Protocol 260, decodes ID + consumption but not scrambled bytes)
- [rtl_433 Issue #1992](https://github.com/merbanan/rtl_433/issues/1992) — Original protocol analysis
- [rtl_433 Discussion #3414](https://github.com/merbanan/rtl_433/discussions/3414) — Arad Sonata analysis
