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
| Transmission Interval| ~30 seconds        |
| Preamble             | 7 nibbles, pattern `01010` |
| Sync Word            | `0x3E 0x69`        |
| Payload              | 21 bytes, fixed length, no CRC |

---

## 2. Packet Layout

| Byte  | Field             | Notes |
|-------|-------------------|-------|
| 0–1   | Static header     | Always `0x0A 0xEC` |
| 2     | Header variant    | Usually `0x7A`. `0x6A` seen on some meters |
| 3     | Type byte         | Always `0xC8` |
| 4     | **Status / type** | `0x4B` normal. Bit 5 (`0x20`) = general alarm. Bit 7 (`0x80`) = reboot (consumption invalid). **Bit 5 feeds into scramble** |
| 5–7   | **Meter ID**      | 3 bytes, big-endian (see below) |
| 8–9   | Meter group       | `0x00 0x00` = standard (Group 1). `0x00 0x40` = Group 2. `0x3D 0x0C` = Group 3. Others exist (e.g. `0x0A 0x0C`) |
| 10–12 | **Consumption**   | 3 bytes, little-endian, 24-bit. Divide by 10 for m³. **Invalid when byte 4 bit 7 is set (reboot)** |
| 13    | Reserved          | Always `0x00` |
| 14    | **Status flags**  | Bit 3 (`0x08`) = battery OK. Bit 2 (`0x04`) = no leak (inverted: 0 = leak). Does **not** affect scramble |
| 15–19 | **Scrambled data** | GF(2) linear function of bytes 0–14 via LFSR (see §3) |
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
```

The divisor depends on the meter group:
- **Standard** (bytes 8–9 = `0x00 0x00`): `volume_m3 = reading / 10.0` (0.1 m³ resolution)
- **Sonata/x40** (bytes 8–9 = `0x00 0x40`): `volume_m3 = reading / 1000.0` (1 liter resolution)
- **3D0C** (bytes 8–9 = `0x3D 0x0C`): `volume_m3 = reading / 10.0` (same as standard)

---

## 3. Scrambled Data (Bytes 15–19)

### Algorithm

Bytes 15–19 are a **linear function over GF(2)** of all data bytes 0–14, computed via a single 40-bit LFSR:

```
scrambled = OFFSET ⊕ ⨁(LFSR_vec[pos] for each set bit in bytes 0–14)
```

Where:
- `OFFSET` = `0x6FF11521E8` — universal 40-bit constant, **same for all meter groups**
- LFSR vectors are consecutive states of a 40-bit LFSR with 3 feedback taps
- `⊕` = bitwise XOR (all arithmetic is over GF(2))

### LFSR Engine

All 120 basis vectors (one per data bit in bytes 0–14) are consecutive states of a single LFSR:

```
next = (v << 1) ⊕ (TAP_A if bit 39) ⊕ (TAP_B if bit 31) ⊕ (TAP_C if bit 23)
```

| Parameter | Value |
|-----------|-------|
| Seed | `0x51AAF3D980` (byte 12 bit 0) |
| TAP_A | `0x00014013F8` |
| TAP_B | `0x201080D890` |
| TAP_C | `0x00018F36C8` |

The forward chain runs: byte 12 → 11 → 10 → 9 → 8 → 7 → 6 → 5 → 4 → 3 → 2 → 1 → 0 (104 states).
The backward chain (LFSR inverse from seed) covers: byte 13 → 14 (16 states, stored as lookup).

The LFSR matrix has rank 39/40 (bits 0–2 are always zero), giving a 37-bit effective state.

**Important**: When byte 4 bit 7 is set (reboot alarm), the consumption field does not contain consumption data. These packets should be excluded from validation.

### Backward LFSR Vectors (Bytes 13–14)

These 16 vectors form a continuous chain with the forward LFSR. Byte 14 bit 7 connects to byte 13 bit 0 via a bridge vector, and byte 13 bit 7 connects to the seed (byte 12 bit 0).

| Byte 13 | Vector | Byte 14 | Vector |
|---------|--------|---------|--------|
| bit 0 | `0xB476FE6BB0` | bit 0 | `0x3037889DD8` |
| bit 1 | `0x68ED33F250` | bit 1 | `0x606E9E0D78` |
| bit 2 | `0xF1CAE73C30` | bit 2 | `0xC0DCB32C38` |
| bit 3 | `0xC3858185C0` | bit 3 | `0xA1A929A5D0` |
| bit 4 | `0xA71B4CF620` | bit 4 | `0x63439380C8` |
| bit 5 | `0x4E37D9FFB8` | bit 5 | `0xC686A83758` |
| bit 6 | `0x9C6E3CC9B8` | bit 6 | `0xAD1D1F9310` |
| bit 7 | `0x38DD398088` | bit 7 | `0x5A3B7F35D8` |

### Consumption Basis Vectors

LFSR positions for consumption bits 0–23 (bytes 10–12, little-endian). All groups share the same vectors.

| Bit | LFSR pos | Vector             | Confidence |
|-----|----------|--------------------|------------|
| 0   | 16       | `0x61B89FB6A0`     | Proven     |
| 1   | 17       | `0xE360308318`     | Proven     |
| 2   | 18       | `0xC6C12115C8`     | Proven     |
| 3   | 19       | `0xAD9382E0F8`     | Proven     |
| 4   | 20       | `0x7B374A3C50`     | Proven     |
| 5   | 21       | `0xF66E9478A0`     | Proven     |
| 6   | 22       | `0xECDDE7D470`     | Proven     |
| 7   | 23       | `0xF9AB805540`     | Proven     |
| 8   | 8        | `0xA045A72F80`     | Proven     |
| 9   | 9        | `0x408B817A30`     | Proven     |
| 10  | 10       | `0xA1060D1A38`     | Proven     |
| 11  | 11       | `0x420D5A2788`     | Proven     |
| 12  | 12       | `0x841AB44F10`     | Proven     |
| 13  | 13       | `0x0835A7BB10`     | Proven     |
| 14  | 14       | `0x106AC040E8`     | Proven     |
| 15  | 15       | `0x20D40FB718`     | Solid      |
| 16  | 0        | `0x51AAF3D980`     | Proven (= LFSR seed) |
| 17  | 1        | `0x8344E85D58`     | Proven (LFSR) |
| 18  | 2        | `0x06891F9F80`     | Proven (LFSR) |
| 19  | 3        | `0x2D02BFE790`     | Proven (LFSR) |
| 20  | 4        | `0x5A04F0F9E8`     | Proven (LFSR) |
| 21  | 5        | `0xB4086EC518`     | Proven (LFSR) |
| 22  | 6        | `0x68119D99C8`     | Proven (LFSR) |
| 23  | 7        | `0xD022B40558`     | Proven (LFSR) |

**How proven:** Bits 0–14 derived from transition analysis (ID-independent). Bit 15 from 4 standard + 1 3D0C meter. Bits 16–23 are LFSR-generated from the seed — confirmed by French researcher's independent full mask table (120/120 forward vectors match).

### Meter ID Basis Vectors

LFSR positions for meter ID bits 0–23 (bytes 5–7, big-endian). Bit 7 has zero effect on scrambling. All groups share the same vectors in the unified model.

| Bit | Vector             | Confidence |
|-----|--------------------|------------|
| 0   | `0x456FF2CC60`     | Proven     |
| 1   | `0x8ADE6AAE08`     | Proven     |
| 2   | `0x0149F2DC28`     | High (RANSAC 71/71) |
| 3   | `0x7FAE4CBD30`     | Proven     |
| 4   | `0x9694D8DA08`     | Proven     |
| 5   | `0x39DD1902E0`     | Proven     |
| 6   | `0x2E9694EEF8`     | Proven     |
| 7   | `0x0000000000`     | Zero (unused by protocol) |
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

---

## 4. How to Use

### Full Validation (Recommended)

Validate any packet from any group using the unified LFSR model:

```
expected = OFFSET ⊕ ⨁(LFSR_vec[pos] for each set bit in bytes 0–14)
valid = (expected == bytes[15:19])
```

This detects RF bit errors in any of bytes 0–19. Works for **all meter groups** with a single universal offset. Error correction up to 3 bits is possible via syndrome matching (see reference implementation).

### ID Recovery

When a packet fails validation but consumption and scramble bytes are clean (only meter ID corrupted):

1. Derive constant = scrambled ⊕ LFSR_contribution(bytes 10–12)
2. Look up the constant against known validated meters
3. If found, replace bytes 5–7 with the real meter ID and re-validate

### C Implementation

See `dialog3g_validate.c` for the full reference implementation with LFSR engine, backward vectors, and 3-bit error correction. Simplified core:

```c
#include <stdint.h>

static const uint64_t LFSR_SEED  = 0x51AAF3D980ULL;
static const uint64_t LFSR_TAP_A = 0x00014013F8ULL;
static const uint64_t LFSR_TAP_B = 0x201080D890ULL;
static const uint64_t LFSR_TAP_C = 0x00018F36C8ULL;
static const uint64_t OFFSET     = 0x6FF11521E8ULL;

static uint64_t lfsr_step(uint64_t v) {
    uint64_t fb = 0;
    if (v & (1ULL << 39)) fb ^= LFSR_TAP_A;
    if (v & (1ULL << 31)) fb ^= LFSR_TAP_B;
    if (v & (1ULL << 23)) fb ^= LFSR_TAP_C;
    return ((v << 1) & 0xFFFFFFFFFFULL) ^ fb;
}

/* Backward LFSR vectors for bytes 13-14 (see §3) */
static const uint64_t BYTE13_VEC[8] = { /* ... */ };
static const uint64_t BYTE14_VEC[8] = { /* ... */ };

/* Compute expected scramble from raw packet bytes 0-14 */
uint64_t d3g_expected(const uint8_t *pkt) {
    uint64_t result = OFFSET;
    uint64_t v;
    int i;
    for (i = 0; i < 8; i++)
        if (pkt[14] & (1 << i)) result ^= BYTE14_VEC[i];
    for (i = 0; i < 8; i++)
        if (pkt[13] & (1 << i)) result ^= BYTE13_VEC[i];
    v = LFSR_SEED;
    for (i = 0; i < 104; i++) {
        if (pkt[12 - i/8] & (1 << (i%8))) result ^= v;
        v = lfsr_step(v);
    }
    return result;
}
```

### Python Implementation

See `serial_terminal/validator.py` for the full implementation. Simplified core:

```python
LFSR_SEED  = 0x51AAF3D980
LFSR_TAP_A = 0x00014013F8
LFSR_TAP_B = 0x201080D890
LFSR_TAP_C = 0x00018F36C8
OFFSET     = 0x6FF11521E8

def lfsr_step(v):
    fb = 0
    if v & (1 << 39): fb ^= LFSR_TAP_A
    if v & (1 << 31): fb ^= LFSR_TAP_B
    if v & (1 << 23): fb ^= LFSR_TAP_C
    return ((v << 1) & 0xFFFFFFFFFF) ^ fb

BYTE13_VEC = [...]  # see §3
BYTE14_VEC = [...]  # see §3

def expected(pkt):
    result = OFFSET
    for i in range(8):
        if pkt[14] & (1 << i): result ^= BYTE14_VEC[i]
    for i in range(8):
        if pkt[13] & (1 << i): result ^= BYTE13_VEC[i]
    v = LFSR_SEED
    for i in range(104):
        if pkt[12 - i // 8] & (1 << (i % 8)): result ^= v
        v = lfsr_step(v)
    return result
```

---

## 5. Confidence & Validation

| Component | Status | Evidence |
|-----------|--------|----------|
| Consumption basis (bits 0–14) | **Proven** | 370+ sequential readings, transition analysis (ID-independent) |
| Consumption basis (bit 15) | **Solid** | 4 standard meters + 1 3D0C meter (51 packets) |
| Consumption basis (bit 16) | **Proven** | = LFSR seed. Confirmed by French researcher |
| Consumption basis (bits 17–23) | **Proven** | LFSR-generated from seed. Confirmed by independent French researcher (120/120 match) |
| ID basis (bits 0–1, 4, 8–23) | **Proven** | Stable across all solves, physically verified serial numbers |
| ID basis (bits 3, 5–6) | **Proven** | Physically verified: 93D9E9 (bits 3,5,6), 082CD3 (bit 6), 5E2EE8 (bits 3,5,6) |
| ID basis (bit 2) | **High** | RANSAC 71/71 (100% for standard meters). No physical verification yet |
| ID basis (bit 7) | **Zero** | Confirmed unused by protocol |
| Full validation (unified, with 3-bit correction) | **80%** STD, **83%** 3D0C, **78%** x40 | Remaining ~20% are RF errors beyond 3-bit correction |
| ID recovery | **Proven** | Derive constant by stripping consumption, look up known meter |

### Methodology

The scrambling algorithm was reverse-engineered through a strictly non-circular process:

1. **Consumption basis (bits 0–16)**: Derived from same-meter sequential packet pairs where only the consumption changed (transition analysis). This isolates each bit's contribution with zero dependency on the ID basis. Bits 0–14 from 370+ readings. Bit 15 from 4 standard meters + 3D0C confirmation. Bit 16 from 2 meters.

2. **ID basis (RANSAC)**: Solved over 71 standard meters using RANSAC (50,000 iterations). Result: **71/71 (100%)**. One non-standard meter (134C97, bytes 8-9 = `0x0A 0x0C`) was inadvertently included and was the sole failure — confirming non-standard meters use different ID matrices. Bits 0, 1, 4 additionally constrained by physically verified meter serial numbers. Bit 7 confirmed zero.

4. **Physical verification**: 3 standard meters physically read — 93D9E9 (bits 3,5,6), 082CD3 serial 13839368 (bit 6), 5E2EE8 serial 15216222 (bits 3,5,6). Proves ID bits 3, 5, 6. Bit 2 supported by RANSAC only (no standard meter with bit 2 has been physically verified). Additionally, 1 3D0C meter physically verified (6816F9).

5. **LFSR discovery**: All 120 basis vectors (bytes 0–14) are consecutive states of a single 40-bit LFSR with 3 feedback taps. This was independently confirmed by a French researcher who solved the full mask table from a different meter population — 120/120 vectors match. The 16 backward LFSR vectors (bytes 13–14) were provided by the French research.

6. **Unified model**: With all 120 data bits in the LFSR model, a single universal offset (`0x6FF11521E8`) works for all groups (STD, x40, 3D0C). No per-group corrections needed. 3-bit error correction using 160 syndrome bits achieves 75–83% validation across all groups.

7. **Field validation**: 1303/1734 packets validate across all groups (80% STD, 78% x40, 83% 3D0C).

### Avoiding Circular Logic

Earlier iterations of this research fell into circular reasoning: using the ID basis to validate meters, then using those validated meters to confirm the ID basis. The current solution avoids this by:

- CONS_BASIS derived only from transition analysis (no ID dependency)
- Per-meter constants derived by stripping consumption from validated packets
- ID_BASIS derived only from independently confirmed constants
- CONS_BASIS bits 17+ removed until they can be derived without depending on the ID basis

### Meter Groups

| Group | Bytes 8–9 | Cons Divisor | Pass Rate (with 3-bit correction) |
|-------|-----------|-------------|-----------------------------------|
| Standard | `0x00 0x00` | ÷10 (0.1 m³) | 80.0% (984/1230) |
| Sonata/x40 | `0x00 0x40` | ÷1000 (1 liter) | 77.6% (190/245) |
| 3D0C | `0x3D 0x0C` | ÷10 (0.1 m³) | 83.1% (98/118) |
| Other | Various | — | 22.0% (likely RF bit-flips of known groups) |

All groups use the **same universal LFSR model and offset** (`0x6FF11521E8`). Bytes 8–9 are just more data bits in the LFSR — no per-group detection or offset selection needed.

3D0C meters appear to be an older model. x40 meters are **Arad Sonata** ultrasonic meters — typically commercial/street installations. They report consumption in liters (÷1000 for m³) rather than 0.1 m³ units (÷10). Physically verified: FA7509 (serial 620026) reads 2148.253 m³ = raw 2148253. Standard meters are residential Dialog 3G models.

---

## 6. Known Limitations

- **Bytes 0–4**: Included in the LFSR model. Bytes 0–3 are static header, byte 4 has status flags. All feed into the scramble via pure LFSR.
- **Bytes 13–14**: Backward LFSR vectors provided by French research. Byte 13 is typically `0x00`. Byte 14 carries status flags (battery, leak).
- **Byte 20**: Low nibble unreliable due to Manchester timing drift. Should be masked to `& 0xF0`.
- **~20% failure rate**: Remaining invalid packets are RF errors beyond 3-bit correction capability.
- **Regional variants**: Only tested on Israeli version (916.3 MHz). A French researcher has independently confirmed the same LFSR on their meters. US/European versions may use different frequencies and/or different basis vectors.

---

## 7. References

- [Dialog3g-Decoder](https://github.com/assafms/Dialog3g-Decoder) — ESPHome + SI4432 hardware implementation
- [rtl_433](https://github.com/merbanan/rtl_433) — SDR decoder (Protocol 260, decodes ID + consumption but not scrambled bytes)
- [rtl_433 Issue #1992](https://github.com/merbanan/rtl_433/issues/1992) — Original protocol analysis
- [rtl_433 Discussion #3414](https://github.com/merbanan/rtl_433/discussions/3414) — Arad Sonata analysis
