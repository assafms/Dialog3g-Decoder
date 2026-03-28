/*
 * Arad Dialog 3G / Sonata — packet validation with up to 3-bit error correction
 *
 * All basis vectors are consecutive states of a 40-bit LFSR with 3 feedback taps.
 * LFSR: next = (v << 1) ^ (TAP_A if bit39) ^ (TAP_B if bit31) ^ (TAP_C if bit23)
 * Seed: 0x51AAF3D980 (byte 12 bit 0)
 * Sequence: byte12, byte11, byte10, byte9, byte8, byte7, byte6, byte5, byte4, ...
 *
 * Per-group differences:
 *   STD  (0x0000): offset=0xDF750DC2C0, byte7 corr=0x34E4E74B50 mask=0xEC,
 *                  ALARM_VEC for byte4 bit5, byte4 not in LFSR
 *   x40  (0x0040): offset=0x61759A89E8, byte7 corr=0xC0DCB32C38 mask=0x2F,
 *                  byte4 included in LFSR (no separate alarm handling)
 *
 * Usage:
 *   uint8_t pkt[21];
 *   int ok = d3g_validate_std(pkt);   // for STD meters
 *   int ok = d3g_validate_x40(pkt);   // for x40/Sonata meters
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

/* ---- STD group (0x00 0x00) ---- */

#define STD_OFFSET      0xDF750DC2C0ULL
#define STD_BYTE7_CORR  0x34E4E74B50ULL
#define STD_BYTE7_MASK  0xEC              /* bits 2,3,5,6,7 */
#define STD_ALARM_VEC   0xA8F1156730ULL   /* byte4 bit5 contribution */
#define STD_NUM_BITS    105               /* 65 input + 40 scramble */

static uint64_t std_expected(const uint8_t *pkt)
{
    uint64_t result = STD_OFFSET;
    uint64_t v = LFSR_SEED;
    int i;

    for (i = 0; i < 40; i++) {  /* bytes 12,11,10,9,8 */
        if (pkt[12 - i/8] & (1 << (i%8))) result ^= v;
        v = lfsr_step(v);
    }
    for (i = 0; i < 8; i++) {   /* byte 7 with correction */
        uint64_t b = (STD_BYTE7_MASK & (1<<i)) ? (v ^ STD_BYTE7_CORR) : v;
        if (pkt[7] & (1<<i)) result ^= b;
        v = lfsr_step(v);
    }
    for (i = 0; i < 16; i++) {  /* bytes 6, 5 */
        if (pkt[6 - i/8] & (1 << (i%8))) result ^= v;
        v = lfsr_step(v);
    }
    if (pkt[4] & 0x20) result ^= STD_ALARM_VEC;
    return result;
}

static void std_build_syndromes(uint64_t *syn)
{
    uint64_t v = LFSR_SEED;
    int i;
    for (i = 0; i < 40; i++) { syn[i] = v; v = lfsr_step(v); }
    for (i = 0; i < 8; i++) {
        syn[40+i] = (STD_BYTE7_MASK & (1<<i)) ? (v ^ STD_BYTE7_CORR) : v;
        v = lfsr_step(v);
    }
    for (i = 0; i < 16; i++) { syn[48+i] = v; v = lfsr_step(v); }
    syn[64] = STD_ALARM_VEC;
    for (i = 0; i < 40; i++) syn[65+i] = 1ULL << i;
}

/* ---- x40 group (0x00 0x40) ---- */

#define X40_OFFSET      0x61759A89E8ULL
#define X40_BYTE7_CORR  0xC0DCB32C38ULL
#define X40_BYTE7_MASK  0x2F              /* bits 0,1,2,3,5 */
#define X40_NUM_BITS    112               /* 72 input + 40 scramble */

static uint64_t x40_expected(const uint8_t *pkt)
{
    uint64_t result = X40_OFFSET;
    uint64_t v = LFSR_SEED;
    int i;

    for (i = 0; i < 40; i++) {  /* bytes 12,11,10,9,8 */
        if (pkt[12 - i/8] & (1 << (i%8))) result ^= v;
        v = lfsr_step(v);
    }
    for (i = 0; i < 8; i++) {   /* byte 7 with correction */
        uint64_t b = (X40_BYTE7_MASK & (1<<i)) ? (v ^ X40_BYTE7_CORR) : v;
        if (pkt[7] & (1<<i)) result ^= b;
        v = lfsr_step(v);
    }
    for (i = 0; i < 16; i++) {  /* bytes 6, 5 */
        if (pkt[6 - i/8] & (1 << (i%8))) result ^= v;
        v = lfsr_step(v);
    }
    for (i = 0; i < 8; i++) {   /* byte 4 (in LFSR for x40) */
        if (pkt[4] & (1<<i)) result ^= v;
        v = lfsr_step(v);
    }
    return result;
}

static void x40_build_syndromes(uint64_t *syn)
{
    uint64_t v = LFSR_SEED;
    int i;
    for (i = 0; i < 40; i++) { syn[i] = v; v = lfsr_step(v); }
    for (i = 0; i < 8; i++) {
        syn[40+i] = (X40_BYTE7_MASK & (1<<i)) ? (v ^ X40_BYTE7_CORR) : v;
        v = lfsr_step(v);
    }
    for (i = 0; i < 16; i++) { syn[48+i] = v; v = lfsr_step(v); }
    for (i = 0; i < 8; i++) { syn[64+i] = v; v = lfsr_step(v); }
    for (i = 0; i < 40; i++) syn[72+i] = 1ULL << i;
}

/* ---- Error correction (shared) ---- */

static void idx_to_pos_std(int idx, int *bp, int *bi)
{
    static const int bm[] = {
        12,12,12,12,12,12,12,12, 11,11,11,11,11,11,11,11,
        10,10,10,10,10,10,10,10,  9, 9, 9, 9, 9, 9, 9, 9,
         8, 8, 8, 8, 8, 8, 8, 8,  7, 7, 7, 7, 7, 7, 7, 7,
         6, 6, 6, 6, 6, 6, 6, 6,  5, 5, 5, 5, 5, 5, 5, 5, 4
    };
    if (idx < 65) { *bp = bm[idx]; *bi = (idx==64) ? 5 : idx%8; }
    else { int s = idx-65; *bp = 19-s/8; *bi = s%8; }
}

static void idx_to_pos_x40(int idx, int *bp, int *bi)
{
    static const int bm[] = {
        12,12,12,12,12,12,12,12, 11,11,11,11,11,11,11,11,
        10,10,10,10,10,10,10,10,  9, 9, 9, 9, 9, 9, 9, 9,
         8, 8, 8, 8, 8, 8, 8, 8,  7, 7, 7, 7, 7, 7, 7, 7,
         6, 6, 6, 6, 6, 6, 6, 6,  5, 5, 5, 5, 5, 5, 5, 5,
         4, 4, 4, 4, 4, 4, 4, 4
    };
    if (idx < 72) { *bp = bm[idx]; *bi = idx%8; }
    else { int s = idx-72; *bp = 19-s/8; *bi = s%8; }
}

static int correct_packet(uint8_t *pkt, uint64_t syndrome,
                          uint64_t *syn, int num_bits,
                          void (*idx_to_pos)(int, int*, int*))
{
    int bp, bi;

    for (int i = 0; i < num_bits; i++) {
        if (syn[i] == syndrome) {
            idx_to_pos(i, &bp, &bi);
            pkt[bp] ^= (1 << bi);
            return 1;
        }
    }
    for (int i = 0; i < num_bits; i++) {
        uint64_t r1 = syndrome ^ syn[i];
        for (int j = i+1; j < num_bits; j++) {
            if (syn[j] == r1) {
                idx_to_pos(i, &bp, &bi); pkt[bp] ^= (1<<bi);
                idx_to_pos(j, &bp, &bi); pkt[bp] ^= (1<<bi);
                return 2;
            }
        }
    }
    for (int i = 0; i < num_bits; i++) {
        uint64_t r1 = syndrome ^ syn[i];
        for (int j = i+1; j < num_bits; j++) {
            uint64_t r2 = r1 ^ syn[j];
            for (int k = j+1; k < num_bits; k++) {
                if (syn[k] == r2) {
                    idx_to_pos(i, &bp, &bi); pkt[bp] ^= (1<<bi);
                    idx_to_pos(j, &bp, &bi); pkt[bp] ^= (1<<bi);
                    idx_to_pos(k, &bp, &bi); pkt[bp] ^= (1<<bi);
                    return 3;
                }
            }
        }
    }
    return -2;
}

/*
 * Validate STD packet. Returns 0=valid, 1/2/3=corrected, -1=reboot, -2=bad.
 */
int d3g_validate_std(uint8_t *pkt)
{
    if (pkt[4] & 0x80) return -1;
    uint64_t exp = std_expected(pkt);
    uint64_t act = ((uint64_t)pkt[15]<<32)|((uint64_t)pkt[16]<<24)|
                   ((uint64_t)pkt[17]<<16)|((uint64_t)pkt[18]<<8)|pkt[19];
    uint64_t syn_val = exp ^ act;
    if (syn_val == 0) return 0;
    uint64_t syn[STD_NUM_BITS];
    std_build_syndromes(syn);
    return correct_packet(pkt, syn_val, syn, STD_NUM_BITS, idx_to_pos_std);
}

/*
 * Validate x40/Sonata packet. Returns 0=valid, 1/2/3=corrected, -1=reboot, -2=bad.
 */
int d3g_validate_x40(uint8_t *pkt)
{
    if (pkt[4] & 0x80) return -1;
    uint64_t exp = x40_expected(pkt);
    uint64_t act = ((uint64_t)pkt[15]<<32)|((uint64_t)pkt[16]<<24)|
                   ((uint64_t)pkt[17]<<16)|((uint64_t)pkt[18]<<8)|pkt[19];
    uint64_t syn_val = exp ^ act;
    if (syn_val == 0) return 0;
    uint64_t syn[X40_NUM_BITS];
    x40_build_syndromes(syn);
    return correct_packet(pkt, syn_val, syn, X40_NUM_BITS, idx_to_pos_x40);
}

/*
 * Auto-detect group and validate.
 */
int d3g_validate(uint8_t *pkt)
{
    if (pkt[8] == 0x00 && pkt[9] == 0x00) return d3g_validate_std(pkt);
    if (pkt[9] == 0x40)                    return d3g_validate_x40(pkt);
    return -2;  /* unknown group */
}

/*
 * Derive the meter constant (strips consumption contribution).
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
