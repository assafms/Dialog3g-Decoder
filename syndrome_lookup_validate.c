/*
 * Arad Dialog 3G — syndrome lookup table validator
 *
 * Precomputes all 1/2/3-bit error syndromes into a hash table (~683K entries).
 * Correction is a single O(1) hash lookup instead of nested for-loops.
 *
 * Hash table: open addressing, linear probing, 2^20 = 1M slots.
 * Key = 40-bit syndrome, Value = up to 3 bit indices to flip.
 *
 * API:
 *   d3g_lookup_init()     — build tables (call once)
 *   d3g_lookup_validate() — validate and correct packet
 *
 * Standalone build:
 *   gcc -O2 -o syndrome_validate syndrome_lookup_validate.c
 *   ./syndrome_validate <42-hex-packet>
 */

#include <stdint.h>
#include <string.h>

/* ---- LFSR engine ---- */

static const uint64_t LFSR_SEED  = 0x51AAF3D980ULL;
static const uint64_t LFSR_TAP_A = 0x00014013F8ULL;
static const uint64_t LFSR_TAP_B = 0x201080D890ULL;
static const uint64_t LFSR_TAP_C = 0x00018F36C8ULL;
static const uint64_t MASK40     = 0xFFFFFFFFFFULL;

#define OFFSET          0x6FF11521E8ULL
#define NUM_TOTAL       160

static uint64_t lfsr_step(uint64_t v)
{
    uint64_t fb = 0;
    if (v & (1ULL << 39)) fb ^= LFSR_TAP_A;
    if (v & (1ULL << 31)) fb ^= LFSR_TAP_B;
    if (v & (1ULL << 23)) fb ^= LFSR_TAP_C;
    return ((v << 1) & MASK40) ^ fb;
}

/* ---- Backward LFSR vectors (bytes 13-14) ---- */

static const uint64_t BYTE13_VEC[8] = {
    0xB476FE6BB0ULL, 0x68ED33F250ULL, 0xF1CAE73C30ULL, 0xC3858185C0ULL,
    0xA71B4CF620ULL, 0x4E37D9FFB8ULL, 0x9C6E3CC9B8ULL, 0x38DD398088ULL
};
static const uint64_t BYTE14_VEC[8] = {
    0x3037889DD8ULL, 0x606E9E0D78ULL, 0xC0DCB32C38ULL, 0xA1A929A5D0ULL,
    0x63439380C8ULL, 0xC686A83758ULL, 0xAD1D1F9310ULL, 0x5A3B7F35D8ULL
};

/* ---- Syndrome vectors ---- */

static uint64_t syn_vec[NUM_TOTAL];

static void build_syndromes(void)
{
    int i;
    uint64_t v;
    for (i = 0; i < 8; i++) syn_vec[i] = BYTE14_VEC[i];
    for (i = 0; i < 8; i++) syn_vec[8 + i] = BYTE13_VEC[i];
    v = LFSR_SEED;
    for (i = 0; i < 104; i++) {
        syn_vec[16 + i] = v;
        v = lfsr_step(v);
    }
    for (i = 0; i < 40; i++) syn_vec[120 + i] = 1ULL << i;
}

/* ---- Hash table ---- */

#define HT_BITS     20
#define HT_SIZE     (1 << HT_BITS)     /* 1,048,576 slots */
#define HT_MASK     (HT_SIZE - 1)

typedef struct {
    uint64_t key;       /* syndrome (0 = empty slot) */
    uint8_t  count;     /* number of bits to flip (1-3) */
    uint8_t  idx[3];    /* bit indices (0-159) */
} ht_slot_t;

static ht_slot_t ht[HT_SIZE];

static uint32_t ht_hash(uint64_t key)
{
    /* Multiply-xor-shift hash for 40-bit keys */
    key ^= key >> 21;
    key *= 0x9E3779B97F4A7C15ULL;
    key ^= key >> 17;
    return (uint32_t)(key & HT_MASK);
}

static void ht_insert(uint64_t key, int count, int i0, int i1, int i2)
{
    uint32_t slot = ht_hash(key);
    while (ht[slot].key != 0) {
        if (ht[slot].key == key)
            return;             /* already exists, keep shortest correction */
        slot = (slot + 1) & HT_MASK;
    }
    ht[slot].key = key;
    ht[slot].count = (uint8_t)count;
    ht[slot].idx[0] = (uint8_t)i0;
    ht[slot].idx[1] = (uint8_t)i1;
    ht[slot].idx[2] = (uint8_t)i2;
}

static const ht_slot_t *ht_find(uint64_t key)
{
    uint32_t slot = ht_hash(key);
    while (ht[slot].key != 0) {
        if (ht[slot].key == key)
            return &ht[slot];
        slot = (slot + 1) & HT_MASK;
    }
    return 0;
}

/* ---- Build lookup tables ---- */

static int lookup_ready = 0;

void d3g_lookup_init(void)
{
    int i, j, k;
    uint64_t si, sij, s;

    if (lookup_ready) return;

    memset(ht, 0, sizeof(ht));
    build_syndromes();

    /* 1-bit: 160 entries */
    for (i = 0; i < NUM_TOTAL; i++)
        ht_insert(syn_vec[i], 1, i, 0, 0);

    /* 2-bit: C(160,2) = 12,720 entries */
    for (i = 0; i < NUM_TOTAL; i++) {
        si = syn_vec[i];
        for (j = i + 1; j < NUM_TOTAL; j++)
            ht_insert(si ^ syn_vec[j], 2, i, j, 0);
    }

    /* 3-bit: C(160,3) = 680,680 entries */
    for (i = 0; i < NUM_TOTAL; i++) {
        si = syn_vec[i];
        for (j = i + 1; j < NUM_TOTAL; j++) {
            sij = si ^ syn_vec[j];
            for (k = j + 1; k < NUM_TOTAL; k++)
                ht_insert(sij ^ syn_vec[k], 3, i, j, k);
        }
    }

    lookup_ready = 1;
}

/* ---- Compute expected scramble ---- */

static uint64_t d3g_expected(const uint8_t *pkt)
{
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

/* ---- Index to packet position ---- */

static void idx_to_pos(int idx, int *bp, int *bi)
{
    if (idx < 8) {
        *bp = 14; *bi = idx;
    } else if (idx < 16) {
        *bp = 13; *bi = idx - 8;
    } else if (idx < 120) {
        int k = idx - 16;
        *bp = 12 - k/8; *bi = k % 8;
    } else {
        int s = idx - 120;
        *bp = 19 - s/8; *bi = s % 8;
    }
}

/* ---- Validate packet ---- */

/*
 * Validate and correct a Dialog 3G packet using syndrome lookup.
 * Single O(1) hash lookup replaces nested for-loops.
 * Returns: 0=valid, 1/2/3=corrected, -1=reboot, -2=bad.
 */
int d3g_lookup_validate(uint8_t *pkt)
{
    const ht_slot_t *hit;
    uint64_t exp, act, syndrome;
    int bp, bi, c;

    if (!lookup_ready) d3g_lookup_init();

    if (pkt[4] & 0x80) return -1;

    exp = d3g_expected(pkt);
    act = ((uint64_t)pkt[15]<<32)|((uint64_t)pkt[16]<<24)|
          ((uint64_t)pkt[17]<<16)|((uint64_t)pkt[18]<<8)|pkt[19];
    syndrome = exp ^ act;

    if (syndrome == 0) return 0;

    hit = ht_find(syndrome);
    if (!hit) return -2;

    for (c = 0; c < hit->count; c++) {
        idx_to_pos(hit->idx[c], &bp, &bi);
        pkt[bp] ^= (1 << bi);
    }

    return hit->count;
}

/*
 * Derive per-meter constant (strips consumption bytes 10-12).
 */
uint64_t d3g_lookup_derive_constant(const uint8_t *pkt)
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

/* ---- Standalone test harness ---- */

#ifdef STANDALONE
#include <stdio.h>
#include <stdlib.h>

static int hex2bytes(const char *hex, uint8_t *out, int max)
{
    int len = (int)strlen(hex);
    int i;
    if (len % 2 != 0 || len / 2 > max) return -1;
    for (i = 0; i < len / 2; i++) {
        unsigned int b;
        sscanf(hex + 2*i, "%02x", &b);
        out[i] = (uint8_t)b;
    }
    return len / 2;
}

int main(int argc, char **argv)
{
    uint8_t pkt[21];
    const char *hex;
    int n, hexlen, result;
    uint32_t meter_id, consumption;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <hex-packet> [more packets...]\n", argv[0]);
        return 1;
    }

    printf("Building lookup tables...\n");
    d3g_lookup_init();
    printf("Ready.\n\n");

    for (int a = 1; a < argc; a++) {
        hex = argv[a];
        hexlen = (int)strlen(hex);

        /* Strip preamble if longer than 42 chars */
        if (hexlen > 42) hex += (hexlen - 42);

        n = hex2bytes(hex, pkt, 21);
        if (n != 21) {
            printf("[%s] Error: expected 21 bytes, got %d\n\n", argv[a], n);
            continue;
        }
        pkt[20] &= 0xF0;

        meter_id = (pkt[5] << 16) | (pkt[6] << 8) | pkt[7];
        consumption = pkt[10] | (pkt[11] << 8) | (pkt[12] << 16);

        result = d3g_lookup_validate(pkt);

        printf("Packet:      %s\n", argv[a]);
        printf("Meter ID:    %06X\n", meter_id);
        printf("Consumption: %u -> %.1f m3\n", consumption,
               pkt[9] == 0x40 ? consumption / 1000.0 : consumption / 10.0);
        printf("Byte14:      0x%02X  Battery=%s  Leak=%s\n",
               pkt[14],
               (pkt[14] & 0x08) ? "OK" : "LOW",
               (pkt[14] & 0x04) ? "no" : "YES");

        const char *msg;
        switch (result) {
            case  0: msg = "VALID (exact)";    break;
            case  1: msg = "1-bit corrected";  break;
            case  2: msg = "2-bit corrected";  break;
            case  3: msg = "3-bit corrected";  break;
            case -1: msg = "REBOOT (invalid)"; break;
            default: msg = "FAILED";           break;
        }
        printf("Result:      %s\n\n", msg);
    }

    return 0;
}
#endif
