/*
 * Arad Dialog 3G / Sonata — packet validation
 *
 * The 5 scrambled bytes (15-19) are a GF(2) linear function of packet
 * bytes 5-12 plus a flag bit.  All 56 basis vectors for bytes 5-11
 * are consecutive states of a single LFSR; byte 12 uses a separate
 * lookup table.
 *
 * scrambled = OFFSET
 *           ^ M(byte11) ^ M(byte10)          -- consumption low/mid
 *           ^ M(byte9)  ^ M(byte8)           -- meter group
 *           ^ M(byte7)  ^ M(byte6) ^ M(byte5)-- meter ID
 *           ^ CONS_HI(byte12)                -- consumption high
 *           ^ (ALARM_VEC if byte4 bit5 set)  -- general alarm flag
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

static const uint64_t LFSR_SEED = 0xA045A72F80ULL;

static const uint64_t LFSR_SUBMASK[8] = {
    0x0000000000ULL, 0x00018F36C8ULL, 0x201080D890ULL, 0x20110FEE58ULL,
    0x00014013F8ULL, 0x0000CF2530ULL, 0x2011C0CB68ULL, 0x20104FFDA0ULL,
};

static uint64_t lfsr_step(uint64_t v)
{
    int key = ((v >> 39) & 1) * 4 + ((v >> 31) & 1) * 2 + ((v >> 23) & 1);
    return ((v << 1) & 0xFFFFFFFFFFULL) ^ LFSR_SUBMASK[key];
}

/* ---- Constants ---- */

/* Byte 12 (consumption bits 16-23) — not part of the LFSR */
static const uint64_t CONS_HI[8] = {
    0x51AAF3D980ULL, 0x2826118BE0ULL, 0xADEBE64938ULL, 0x2D02BFE790ULL,
    0x5A04F0F9E8ULL, 0xB4086EC518ULL, 0xC0E088FEF8ULL, 0xD022B40558ULL,
};

/* Global offset (with STD byte-7 correction baked in) */
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

    /* byte 12 — consumption high (separate table) */
    for (i = 0; i < 8; i++) {
        if (pkt[12] & (1 << i))
            result ^= CONS_HI[i];
    }

    /* byte 4 bit 5 — general alarm flag */
    if (pkt[4] & 0x20)
        result ^= ALARM_VEC;

    return result;
}

/*
 * Validate a 21-byte Dialog 3G packet.
 *
 * pkt:        21-byte raw packet
 * byte7_corr: byte-7 correction mask (D3G_GROUP_STD, etc.)
 *
 * Returns:
 *   1  = valid
 *   0  = invalid
 *  -1  = reboot packet (consumption field is not consumption; skip)
 */
int d3g_validate(const uint8_t *pkt, uint8_t byte7_corr)
{
    /* Reboot alarm — consumption field contains other data */
    if (pkt[4] & 0x80)
        return -1;

    uint64_t expected = d3g_expected(pkt, byte7_corr);

    uint64_t actual = ((uint64_t)pkt[15] << 32) | ((uint64_t)pkt[16] << 24) |
                      ((uint64_t)pkt[17] << 16) | ((uint64_t)pkt[18] << 8) |
                      (uint64_t)pkt[19];

    return (expected == actual) ? 1 : 0;
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

    /* Strip byte 12 */
    for (i = 0; i < 8; i++) {
        if (pkt[12] & (1 << i))
            result ^= CONS_HI[i];
    }

    /* Strip alarm flag */
    if (pkt[4] & 0x20)
        result ^= ALARM_VEC;

    return result;
}
