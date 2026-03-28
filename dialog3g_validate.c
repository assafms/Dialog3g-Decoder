/*
 * Arad Dialog 3G / Sonata — packet validation with up to 3-bit error correction
 *
 * The 5 scrambled bytes (15-19) are a GF(2) linear function of packet
 * bytes 5-12 plus a flag bit.  All 64 basis vectors for bytes 5-12
 * are consecutive states of a 40-bit LFSR with 3 feedback taps.
 *
 * LFSR: next = (v << 1) ^ (TAP_A if bit39) ^ (TAP_B if bit31) ^ (TAP_C if bit23)
 * Seed: 0x51AAF3D980 (byte 12 bit 0)
 * Sequence: byte12, byte11, byte10, byte9, byte8, byte7, byte6, byte5
 *
 * scrambled = OFFSET
 *           ^ M(byte12) ^ M(byte11) ^ M(byte10) -- consumption
 *           ^ M(byte9)  ^ M(byte8)              -- meter group
 *           ^ M(byte7)  ^ M(byte6) ^ M(byte5)   -- meter ID
 *           ^ (ALARM_VEC if byte4 bit5 set)      -- general alarm flag
 *
 * Byte 7 (ID LSB) vectors receive a per-group XOR correction.
 * Currently only the standard group (0x00 0x00) correction is known.
 *
 * Usage:
 *   uint8_t pkt[21];          // raw 21-byte packet
 *   int ok = d3g_validate(pkt, D3G_GROUP_STD);
 *   // ok == 1 means valid, 0 means invalid, -1 means reboot (skip)
 */

#include <stdint.h>

/* ---- LFSR engine ---- */

/*
 * 40-bit LFSR with 3 independent feedback taps at bit positions 39, 31, 23.
 * Seed: 0xA045A72F80. Generates 56 basis vectors for bytes 5-11 in sequence:
 * byte11[0-7], byte10[0-7], byte9[0-7], byte8[0-7], byte7[0-7], byte6[0-7], byte5[0-7]
 */
static const uint64_t LFSR_SEED  = 0x51AAF3D980ULL;  /* byte 12 bit 0 */
static const uint64_t LFSR_TAP_A = 0x00014013F8ULL;  /* feedback when bit 39 = 1 */
static const uint64_t LFSR_TAP_B = 0x201080D890ULL;  /* feedback when bit 31 = 1 */
static const uint64_t LFSR_TAP_C = 0x00018F36C8ULL;  /* feedback when bit 23 = 1 */

static uint64_t lfsr_step(uint64_t v)
{
    uint64_t fb = 0;
    if (v & (1ULL << 39)) fb ^= LFSR_TAP_A;
    if (v & (1ULL << 31)) fb ^= LFSR_TAP_B;
    if (v & (1ULL << 23)) fb ^= LFSR_TAP_C;
    return ((v << 1) & 0xFFFFFFFFFFULL) ^ fb;
}

/* ---- Constants ---- */

/* No lookup tables — all 64 basis vectors generated from LFSR seed */

/* Global offset (for group 0x0000 with STD byte-7 correction applied) */
static const uint64_t OFFSET = 0xDF750DC2C0ULL;

/* Byte-7 correction value */
static const uint64_t BYTE7_CORR = 0x34E4E74B50ULL;

/* General alarm flag contribution (byte 4 bit 5) */
static const uint64_t ALARM_VEC = 0xA8F1156730ULL;

/* ---- Group definitions ---- */

/*
 * Each group has a correction bitmask for byte 7.
 * The mask selects which bit positions of byte 7 get XOR'd
 * with BYTE7_CORR before applying to the scramble.
 *
 * STD (0x00 0x00): bits 2,3,5,6,7 corrected = 0xEC
 * Other groups: not yet fully solved.
 */
#define D3G_GROUP_STD  0xEC   /* standard meters (0x00 0x00) */
/* #define D3G_GROUP_X40  0x??   x40/Sonata — not yet solved */

/* ---- Validation ---- */

/*
 * Compute expected scramble for the given packet and group.
 *
 * pkt:        21-byte raw packet
 * byte7_corr: byte-7 correction mask for this group (e.g. D3G_GROUP_STD)
 *
 * Returns the expected 40-bit scramble value.
 */
static uint64_t d3g_expected(const uint8_t *pkt, uint8_t byte7_corr)
{
    uint64_t result = OFFSET;
    uint64_t v = LFSR_SEED;
    int i;

    /* LFSR sequence: byte12, byte11, byte10, byte9, byte8, byte7, byte6, byte5 */

    /* byte 12 — consumption high */
    for (i = 0; i < 8; i++) {
        if (pkt[12] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* byte 11 — consumption mid */
    for (i = 0; i < 8; i++) {
        if (pkt[11] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* byte 10 — consumption low */
    for (i = 0; i < 8; i++) {
        if (pkt[10] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* byte 9 — group low */
    for (i = 0; i < 8; i++) {
        if (pkt[9] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* byte 8 — group high */
    for (i = 0; i < 8; i++) {
        if (pkt[8] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* byte 7 — ID LSB (group-specific correction) */
    for (i = 0; i < 8; i++) {
        uint64_t basis = v;
        if (byte7_corr & (1 << i))
            basis ^= BYTE7_CORR;
        if (pkt[7] & (1 << i))
            result ^= basis;
        v = lfsr_step(v);
    }

    /* byte 6 — ID mid */
    for (i = 0; i < 8; i++) {
        if (pkt[6] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* byte 5 — ID MSB */
    for (i = 0; i < 8; i++) {
        if (pkt[5] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* byte 4 bit 5 — general alarm flag */
    if (pkt[4] & 0x20)
        result ^= ALARM_VEC;

    return result;
}

/*
 * Build the syndrome table: 65 entries for input bits, 40 for scramble bits.
 * Each entry is the syndrome (40-bit value) produced by flipping that bit.
 *
 * Input bit layout (0-64):
 *   0-7:   byte 12 (consumption high)
 *   8-15:  byte 11 (consumption mid)
 *   16-23: byte 10 (consumption low)
 *   24-31: byte 9  (group low)
 *   32-39: byte 8  (group high)
 *   40-47: byte 7  (ID LSB, with correction)
 *   48-55: byte 6  (ID mid)
 *   56-63: byte 5  (ID MSB)
 *   64:    byte 4 bit 5 (alarm flag)
 *
 * Scramble bit layout (65-104):
 *   65+i:  bit i of the 40-bit scramble (bit 39 = MSB)
 */
#define D3G_NUM_INPUT_BITS  65
#define D3G_NUM_TOTAL_BITS  105

static void d3g_build_syndromes(uint8_t byte7_corr, uint64_t *syn)
{
    uint64_t v = LFSR_SEED;
    int i;

    /* Bytes 12,11,10,9,8 — LFSR vectors directly */
    for (i = 0; i < 40; i++) {
        syn[i] = v;
        v = lfsr_step(v);
    }

    /* Byte 7 — with correction */
    for (i = 0; i < 8; i++) {
        syn[40 + i] = (byte7_corr & (1 << i)) ? (v ^ BYTE7_CORR) : v;
        v = lfsr_step(v);
    }

    /* Bytes 6, 5 — LFSR vectors */
    for (i = 0; i < 16; i++) {
        syn[48 + i] = v;
        v = lfsr_step(v);
    }

    /* Alarm flag */
    syn[64] = ALARM_VEC;

    /* Scramble bits: flipping bit i of scramble produces syndrome with just that bit */
    for (i = 0; i < 40; i++)
        syn[65 + i] = 1ULL << i;
}

/*
 * Map a syndrome table index back to a packet byte and bit position.
 * Fills *byte_pos and *bit_pos. For scramble bits, byte_pos is 15-19.
 */
static void d3g_index_to_pos(int idx, int *byte_pos, int *bit_pos)
{
    static const int byte_map[] = {
        12,12,12,12,12,12,12,12,  /* 0-7 */
        11,11,11,11,11,11,11,11,  /* 8-15 */
        10,10,10,10,10,10,10,10,  /* 16-23 */
         9, 9, 9, 9, 9, 9, 9, 9,  /* 24-31 */
         8, 8, 8, 8, 8, 8, 8, 8,  /* 32-39 */
         7, 7, 7, 7, 7, 7, 7, 7,  /* 40-47 */
         6, 6, 6, 6, 6, 6, 6, 6,  /* 48-55 */
         5, 5, 5, 5, 5, 5, 5, 5,  /* 56-63 */
         4,                        /* 64: alarm flag */
    };

    if (idx < D3G_NUM_INPUT_BITS) {
        *byte_pos = byte_map[idx];
        *bit_pos = (idx == 64) ? 5 : (idx % 8);
    } else {
        /* Scramble bits: index 65+i -> byte 15 + (39-i)/8, bit (39-i)%8 */
        int scr_bit = idx - 65;  /* 0=bit0(LSB) .. 39=bit39(MSB) */
        *byte_pos = 19 - scr_bit / 8;
        *bit_pos = scr_bit % 8;
    }
}

/*
 * Validate a 21-byte Dialog 3G packet with up to 3-bit error correction.
 *
 * pkt:        21-byte raw packet (MODIFIED in place if correction applied)
 * byte7_corr: byte-7 correction mask (D3G_GROUP_STD, etc.)
 *
 * Returns:
 *   0  = valid (no errors)
 *   1  = corrected 1-bit error
 *   2  = corrected 2-bit error
 *   3  = corrected 3-bit error
 *  -1  = reboot packet (skip)
 *  -2  = uncorrectable (4+ bit errors)
 */
int d3g_validate(uint8_t *pkt, uint8_t byte7_corr)
{
    /* Reboot alarm — consumption field contains other data */
    if (pkt[4] & 0x80)
        return -1;

    uint64_t expected = d3g_expected(pkt, byte7_corr);

    uint64_t actual = ((uint64_t)pkt[15] << 32) | ((uint64_t)pkt[16] << 24) |
                      ((uint64_t)pkt[17] << 16) | ((uint64_t)pkt[18] << 8) |
                      (uint64_t)pkt[19];

    uint64_t syndrome = expected ^ actual;

    if (syndrome == 0)
        return 0;

    /* Build syndrome table */
    uint64_t syn[D3G_NUM_TOTAL_BITS];
    d3g_build_syndromes(byte7_corr, syn);

    int bp, bi;

    /* Try 1-bit correction */
    for (int i = 0; i < D3G_NUM_TOTAL_BITS; i++) {
        if (syn[i] == syndrome) {
            d3g_index_to_pos(i, &bp, &bi);
            pkt[bp] ^= (1 << bi);
            return 1;
        }
    }

    /* Try 2-bit correction */
    for (int i = 0; i < D3G_NUM_TOTAL_BITS; i++) {
        uint64_t r1 = syndrome ^ syn[i];
        for (int j = i + 1; j < D3G_NUM_TOTAL_BITS; j++) {
            if (syn[j] == r1) {
                d3g_index_to_pos(i, &bp, &bi);
                pkt[bp] ^= (1 << bi);
                d3g_index_to_pos(j, &bp, &bi);
                pkt[bp] ^= (1 << bi);
                return 2;
            }
        }
    }

    /* Try 3-bit correction */
    for (int i = 0; i < D3G_NUM_TOTAL_BITS; i++) {
        uint64_t r1 = syndrome ^ syn[i];
        for (int j = i + 1; j < D3G_NUM_TOTAL_BITS; j++) {
            uint64_t r2 = r1 ^ syn[j];
            for (int k = j + 1; k < D3G_NUM_TOTAL_BITS; k++) {
                if (syn[k] == r2) {
                    d3g_index_to_pos(i, &bp, &bi);
                    pkt[bp] ^= (1 << bi);
                    d3g_index_to_pos(j, &bp, &bi);
                    pkt[bp] ^= (1 << bi);
                    d3g_index_to_pos(k, &bp, &bi);
                    pkt[bp] ^= (1 << bi);
                    return 3;
                }
            }
        }
    }

    return -2;  /* uncorrectable */
}

/*
 * Derive the meter constant (strips consumption contribution).
 * Useful for two-packet confirmation on unsolved groups.
 *
 * Returns OFFSET ^ M_id(meter_id) ^ M_group(group) ^ M_flag(flags)
 * which is constant for a given meter in a given alarm state.
 */
uint64_t d3g_derive_constant(const uint8_t *pkt)
{
    uint64_t actual = ((uint64_t)pkt[15] << 32) | ((uint64_t)pkt[16] << 24) |
                      ((uint64_t)pkt[17] << 16) | ((uint64_t)pkt[18] << 8) |
                      (uint64_t)pkt[19];

    uint64_t result = actual;
    uint64_t v = LFSR_SEED;
    int i;

    /* Strip byte 12 */
    for (i = 0; i < 8; i++) {
        if (pkt[12] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* Strip byte 11 */
    for (i = 0; i < 8; i++) {
        if (pkt[11] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* Strip byte 10 */
    for (i = 0; i < 8; i++) {
        if (pkt[10] & (1 << i))
            result ^= v;
        v = lfsr_step(v);
    }

    /* Strip alarm flag */
    if (pkt[4] & 0x20)
        result ^= ALARM_VEC;

    return result;
}
