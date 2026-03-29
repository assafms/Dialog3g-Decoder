/*
 * Arad Dialog 3G — unified packet validation with up to 3-bit error correction
 *
 * All basis vectors are consecutive states of a 40-bit LFSR with 3 feedback taps.
 * LFSR: next = (v << 1) ^ (TAP_A if bit39) ^ (TAP_B if bit31) ^ (TAP_C if bit23)
 * Seed: 0x51AAF3D980 (byte 12 bit 0)
 *
 * Forward chain:  byte12 → byte11 → byte10 → byte9 → byte8 → byte7 → byte6 →
 *                 byte5 → byte4 → byte3 → byte2 → byte1 → byte0
 * Backward chain: byte13 → byte14  (LFSR inverse from seed)
 *
 * One model for all groups (STD, x40, 3D0C):
 *   - Pure LFSR vectors for ALL data bytes 0-14 (120 bits)
 *   - Single universal offset: 0x6FF11521E8
 *   - No per-group corrections
 *
 * Usage:
 *   uint8_t pkt[21];
 *   int ok = d3g_validate(pkt);
 */

#include <stdint.h>

/* ---- LFSR engine ---- */

static const uint64_t LFSR_SEED  = 0x51AAF3D980ULL;
static const uint64_t LFSR_TAP_A = 0x00014013F8ULL;
static const uint64_t LFSR_TAP_B = 0x201080D890ULL;
static const uint64_t LFSR_TAP_C = 0x00018F36C8ULL;

static uint64_t lfsr_step(uint64_t v)
{
    uint64_t fb = 0;
    if (v & (1ULL << 39)) fb ^= LFSR_TAP_A;
    if (v & (1ULL << 31)) fb ^= LFSR_TAP_B;
    if (v & (1ULL << 23)) fb ^= LFSR_TAP_C;
    return ((v << 1) & 0xFFFFFFFFFFULL) ^ fb;
}

/* ---- Backward LFSR vectors (bytes 13-14, from French research) ---- */

static const uint64_t BYTE13_VEC[8] = {
    0xB476FE6BB0ULL, 0x68ED33F250ULL, 0xF1CAE73C30ULL, 0xC3858185C0ULL,
    0xA71B4CF620ULL, 0x4E37D9FFB8ULL, 0x9C6E3CC9B8ULL, 0x38DD398088ULL
};
static const uint64_t BYTE14_VEC[8] = {
    0x3037889DD8ULL, 0x606E9E0D78ULL, 0xC0DCB32C38ULL, 0xA1A929A5D0ULL,
    0x63439380C8ULL, 0xC686A83758ULL, 0xAD1D1F9310ULL, 0x5A3B7F35D8ULL
};

/* ---- Universal offset ---- */

#define OFFSET          0x6FF11521E8ULL
#define NUM_DATA_BITS   120           /* bytes 0-14 = 15 bytes */
#define NUM_TOTAL_BITS  160           /* 120 data + 40 scramble */

/* ---- Compute expected scramble ---- */

static uint64_t d3g_expected(const uint8_t *pkt)
{
    uint64_t result = OFFSET;
    uint64_t v;
    int i;

    /* byte 14 (8 bits, backward LFSR) */
    for (i = 0; i < 8; i++)
        if (pkt[14] & (1 << i)) result ^= BYTE14_VEC[i];

    /* byte 13 (8 bits, backward LFSR) */
    for (i = 0; i < 8; i++)
        if (pkt[13] & (1 << i)) result ^= BYTE13_VEC[i];

    /* bytes 12 down to 0 (104 bits, forward LFSR from seed) */
    v = LFSR_SEED;
    for (i = 0; i < 104; i++) {
        if (pkt[12 - i/8] & (1 << (i%8))) result ^= v;
        v = lfsr_step(v);
    }

    return result;
}

/* ---- Build syndrome table ---- */

static void d3g_build_syndromes(uint64_t *syn)
{
    int i;
    uint64_t v;

    /* byte 14 */
    for (i = 0; i < 8; i++) syn[i] = BYTE14_VEC[i];

    /* byte 13 */
    for (i = 0; i < 8; i++) syn[8 + i] = BYTE13_VEC[i];

    /* bytes 12 down to 0 */
    v = LFSR_SEED;
    for (i = 0; i < 104; i++) {
        syn[16 + i] = v;
        v = lfsr_step(v);
    }

    /* scramble bits (bytes 15-19) */
    for (i = 0; i < 40; i++) syn[120 + i] = 1ULL << i;
}

/* ---- Index to packet position mapping ---- */

static void idx_to_pos(int idx, int *bp, int *bi)
{
    if (idx < 8) {
        /* byte 14 */
        *bp = 14; *bi = idx;
    } else if (idx < 16) {
        /* byte 13 */
        *bp = 13; *bi = idx - 8;
    } else if (idx < 120) {
        /* bytes 12 down to 0 */
        int k = idx - 16;
        *bp = 12 - k/8;
        *bi = k % 8;
    } else {
        /* scramble bytes 15-19 */
        int s = idx - 120;
        *bp = 19 - s/8;
        *bi = s % 8;
    }
}

/* ---- Error correction (up to 3 bits) ---- */

static int correct_packet(uint8_t *pkt, uint64_t syndrome, uint64_t *syn)
{
    int bp, bi;

    for (int i = 0; i < NUM_TOTAL_BITS; i++) {
        if (syn[i] == syndrome) {
            idx_to_pos(i, &bp, &bi);
            pkt[bp] ^= (1 << bi);
            return 1;
        }
    }
    for (int i = 0; i < NUM_TOTAL_BITS; i++) {
        uint64_t r1 = syndrome ^ syn[i];
        for (int j = i+1; j < NUM_TOTAL_BITS; j++) {
            if (syn[j] == r1) {
                idx_to_pos(i, &bp, &bi); pkt[bp] ^= (1 << bi);
                idx_to_pos(j, &bp, &bi); pkt[bp] ^= (1 << bi);
                return 2;
            }
        }
    }
    for (int i = 0; i < NUM_TOTAL_BITS; i++) {
        uint64_t r1 = syndrome ^ syn[i];
        for (int j = i+1; j < NUM_TOTAL_BITS; j++) {
            uint64_t r2 = r1 ^ syn[j];
            for (int k = j+1; k < NUM_TOTAL_BITS; k++) {
                if (syn[k] == r2) {
                    idx_to_pos(i, &bp, &bi); pkt[bp] ^= (1 << bi);
                    idx_to_pos(j, &bp, &bi); pkt[bp] ^= (1 << bi);
                    idx_to_pos(k, &bp, &bi); pkt[bp] ^= (1 << bi);
                    return 3;
                }
            }
        }
    }
    return -2;
}

/*
 * Validate any Dialog 3G packet. Returns 0=valid, 1/2/3=corrected, -1=reboot, -2=bad.
 */
int d3g_validate(uint8_t *pkt)
{
    if (pkt[4] & 0x80) return -1;

    uint64_t exp = d3g_expected(pkt);
    uint64_t act = ((uint64_t)pkt[15]<<32)|((uint64_t)pkt[16]<<24)|
                   ((uint64_t)pkt[17]<<16)|((uint64_t)pkt[18]<<8)|pkt[19];
    uint64_t syn_val = exp ^ act;

    if (syn_val == 0) return 0;

    uint64_t syn[NUM_TOTAL_BITS];
    d3g_build_syndromes(syn);
    return correct_packet(pkt, syn_val, syn);
}

/*
 * Derive the meter constant (strips consumption contribution only).
 */
uint64_t d3g_derive_constant(const uint8_t *pkt)
{
    uint64_t actual = ((uint64_t)pkt[15]<<32)|((uint64_t)pkt[16]<<24)|
                      ((uint64_t)pkt[17]<<16)|((uint64_t)pkt[18]<<8)|pkt[19];
    uint64_t result = actual;
    uint64_t v = LFSR_SEED;
    int i;
    for (i = 0; i < 24; i++) {
        if (pkt[12 - i/8] & (1 << (i%8))) result ^= v;
        v = lfsr_step(v);
    }
    return result;
}
