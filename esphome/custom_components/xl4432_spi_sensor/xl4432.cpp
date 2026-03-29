#include "esphome/core/log.h"
#include "xl4432_spi_sensor.h"
#include "xl4432.h"

// 40-bit LFSR with 3 feedback taps — generates all basis vectors
// Forward chain: bytes 12→11→10→9→8→7→6→5→4→3→2→1→0 (104 states)
// Backward chain: bytes 13→14 (16 states, stored as lookup)
static const uint64_t LFSR_SEED  = 0x51AAF3D980ULL;
static const uint64_t LFSR_TAP_A = 0x00014013F8ULL;
static const uint64_t LFSR_TAP_B = 0x201080D890ULL;
static const uint64_t LFSR_TAP_C = 0x00018F36C8ULL;

// Backward LFSR vectors (bytes 13-14, from French research)
static const uint64_t BYTE13_VEC[8] = {
	0xB476FE6BB0ULL, 0x68ED33F250ULL, 0xF1CAE73C30ULL, 0xC3858185C0ULL,
	0xA71B4CF620ULL, 0x4E37D9FFB8ULL, 0x9C6E3CC9B8ULL, 0x38DD398088ULL
};
static const uint64_t BYTE14_VEC[8] = {
	0x3037889DD8ULL, 0x606E9E0D78ULL, 0xC0DCB32C38ULL, 0xA1A929A5D0ULL,
	0x63439380C8ULL, 0xC686A83758ULL, 0xAD1D1F9310ULL, 0x5A3B7F35D8ULL
};

// Universal offset — same for all meter groups
static const uint64_t OFFSET = 0x6FF11521E8ULL;

// Syndrome table sizes: 120 data + 40 scramble = 160
static const int NUM_DATA  = 120;
static const int NUM_TOTAL = 160;

Xl4432::Xl4432(char id[3], bool use_id_as_sync)
{
	nIRQState = 0;
	meterMeasurment = -1;
	packetReady = 0;
	lastMeterMeasurment = 0;
	storedConstant = 0;
	hasStoredConstant = false;
	METER_ID[0] = id[0];
	METER_ID[1] = id[1];
	METER_ID[2] = id[2];
	useIdAsSync = use_id_as_sync;
	packetSniff = false;
}

static uint64_t lfsr_next(uint64_t v)
{
	uint64_t fb = 0;
	if (v & (1ULL << 39)) fb ^= LFSR_TAP_A;
	if (v & (1ULL << 31)) fb ^= LFSR_TAP_B;
	if (v & (1ULL << 23)) fb ^= LFSR_TAP_C;
	return ((v << 1) & 0xFFFFFFFFFFULL) ^ fb;
}

uint64_t Xl4432::expectedScramble()
{
	uint64_t result = OFFSET;

	// Byte 14 (backward LFSR)
	for (int i = 0; i < 8; i++)
		if (packet[14] & (1 << i)) result ^= BYTE14_VEC[i];

	// Byte 13 (backward LFSR)
	for (int i = 0; i < 8; i++)
		if (packet[13] & (1 << i)) result ^= BYTE13_VEC[i];

	// Bytes 12 down to 0 (forward LFSR, 104 bits)
	uint64_t v = LFSR_SEED;
	for (int i = 0; i < 104; i++) {
		if (packet[12 - i/8] & (1 << (i%8)))
			result ^= v;
		v = lfsr_next(v);
	}

	return result;
}

uint64_t Xl4432::deriveConstant()
{
	uint64_t actual = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
	                  ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
	                  (uint64_t)packet[19];
	uint64_t result = actual;

	// Strip consumption contribution (bytes 12, 11, 10 = 24 LFSR states)
	uint64_t v = LFSR_SEED;
	for (int i = 0; i < 24; i++) {
		if (packet[12 - i/8] & (1 << (i%8)))
			result ^= v;
		v = lfsr_next(v);
	}

	return result;
}

static void idx_to_pos(int idx, int *bp, int *bi)
{
	if (idx < 8) {
		*bp = 14; *bi = idx;
	} else if (idx < 16) {
		*bp = 13; *bi = idx - 8;
	} else if (idx < 120) {
		int k = idx - 16;
		*bp = 12 - k/8;
		*bi = k % 8;
	} else {
		int s = idx - 120;
		*bp = 19 - s/8;
		*bi = s % 8;
	}
}

PacketStatus Xl4432::validatePacket()
{
	if (packetSniff)
		return PKT_SNIFF;

	bool idMatch;
	if (useIdAsSync) {
		idMatch = true;
	} else {
		idMatch = (packet[5] == METER_ID[0] &&
		           packet[6] == METER_ID[1] &&
		           packet[7] == METER_ID[2]);
	}
	if (!idMatch)
		return PKT_ID_MISMATCH;

	// Skip reboot packets (byte 4 bit 7) — consumption field is invalid
	if (packet[4] & 0x80)
		return PKT_NON_STANDARD;

	// Unified validation: pure LFSR, all bytes 0-14, all groups
	uint64_t expected = expectedScramble();
	uint64_t actual = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
	                  ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
	                  (uint64_t)packet[19];
	uint64_t syndrome = expected ^ actual;

	if (syndrome == 0)
		return PKT_VALID;

	// Build syndrome table: 16 backward + 104 forward + 40 scramble = 160
	uint64_t syn[NUM_TOTAL];
	for (int i = 0; i < 8; i++) syn[i] = BYTE14_VEC[i];
	for (int i = 0; i < 8; i++) syn[8 + i] = BYTE13_VEC[i];
	uint64_t v = LFSR_SEED;
	for (int i = 0; i < 104; i++) { syn[16 + i] = v; v = lfsr_next(v); }
	for (int i = 0; i < 40; i++) syn[120 + i] = 1ULL << i;

	auto flip = [&](int idx) {
		int bp, bi;
		idx_to_pos(idx, &bp, &bi);
		packet[bp] ^= (1 << bi);
	};

	// 1-bit correction
	for (int i = 0; i < NUM_TOTAL; i++) {
		if (syn[i] == syndrome) {
			flip(i);
			return PKT_CORRECTED_1;
		}
	}

	// 2-bit correction
	for (int i = 0; i < NUM_TOTAL; i++) {
		uint64_t r1 = syndrome ^ syn[i];
		for (int j = i+1; j < NUM_TOTAL; j++) {
			if (syn[j] == r1) {
				flip(i); flip(j);
				return PKT_CORRECTED_2;
			}
		}
	}

	// 3-bit correction (~680K iterations worst case)
	for (int i = 0; i < NUM_TOTAL; i++) {
		uint64_t r1 = syndrome ^ syn[i];
		for (int j = i+1; j < NUM_TOTAL; j++) {
			uint64_t r2 = r1 ^ syn[j];
			for (int k = j+1; k < NUM_TOTAL; k++) {
				if (syn[k] == r2) {
					flip(i); flip(j); flip(k);
					return PKT_CORRECTED_3;
				}
			}
		}
	}

	return PKT_INVALID;
}

float Xl4432::extractMeterReading()
{
	if (useIdAsSync) {
		return float((packet[3] << 16) + (packet[2] << 8) + packet[1]) / 10;
	} else {
		float raw = float((packet[12] << 16) + (packet[11] << 8) + packet[10]);
		// Sonata/x40 meters report in liters, standard in 0.1 m³
		return (packet[9] == 0x40) ? raw / 1000 : raw / 10;
	}
}

void Xl4432::readPacketFromFifo()
{
	spiWriteRegister(0x07, 0x01);
	spiReadPacketFromFifo(PACKET_SIZE);
	spiWriteRegister(0x08, 0x02);
	spiWriteRegister(0x08, 0x00);
	spiWriteRegister(0x07, 0x05);
}

void Xl4432::spiInitRadio()
{
	spiWriteRegister(0x07, 0x80);
	delay(100);
	spiReadRegister(0x03);
	spiReadRegister(0x04);
	delay(100);
}

void Xl4432::spiDisableReciver()
{
	spiWriteRegister(0x07, 0x01);
}

void Xl4432::spiEnableReciver()
{
	spiWriteRegister(0x07, 0x05);
	spiWriteRegister(0x05, 0x03);
	spiWriteRegister(0x06, 0x00);
	spiReadRegister(0x03);
	spiReadRegister(0x04);
}

void Xl4432::initXl4432Registers()
{
	spiDisableReciver();
	spiXl4432Fifo();
	spiWriteRegister(0x1C, 0x8C);
	spiWriteRegister(0x1D, 0x00);
	spiWriteRegister(0x20, 0x65);
	spiWriteRegister(0x21, 0x00);
	spiWriteRegister(0x22, 0xA2);
	spiWriteRegister(0x23, 0x57);
	spiWriteRegister(0x24, 0x00);
	spiWriteRegister(0x25, 0xDE);
	spiWriteRegister(0x30, 0xA8);
	spiWriteRegister(0x32, 0x8C);

	if (useIdAsSync) {
		spiWriteRegister(0x33, 0x0E);
		spiWriteRegister(0x34, 0x00);
		spiWriteRegister(0x35, 0x22);
		spiWriteRegister(0x36, 0x4B);
		spiWriteRegister(0x37, METER_ID[0]);
		spiWriteRegister(0x38, METER_ID[1]);
		spiWriteRegister(0x39, METER_ID[2]);
		ESP_LOGD("custom", "ID-as-sync: 0x4B 0x%02X 0x%02X 0x%02X",
		         METER_ID[0], METER_ID[1], METER_ID[2]);
	} else {
		spiWriteRegister(0x33, 0x0A);
		spiWriteRegister(0x34, 0x07);
		spiWriteRegister(0x35, 0x3A);
		spiWriteRegister(0x36, 0x3e);
		spiWriteRegister(0x37, 0x69);
	}

	spiWriteRegister(0x3E, 0x28);
	spiWriteRegister(0x6E, 0x01);
	spiWriteRegister(0x6F, 0x3B);
	spiWriteRegister(0x70, 0x22);
	spiWriteRegister(0x71, 0x26);
	spiWriteRegister(0x72, 0x18);
	spiWriteRegister(0x75, 0x75);
	spiWriteRegister(0x76, 0xCB);
	spiWriteRegister(0x77, 0xC0);
	spiWriteRegister(0x0b, 0x14);
	spiWriteRegister(0x0C, 0x19);
	spiWriteRegister(0x0D, 0x0F);
	spiEnableReciver();
}

void Xl4432::spiXl4432Fifo()
{
	uint8_t data;
	for (uint8_t x = 0; x < 255; x++) {
		data = spiReadRegister(0x7F);
	}
}

void Xl4432::spiReadPacketFromFifo(uint8_t length)
{
	for (uint8_t x = 0; x < length + 1; x++) {
		packet[x] = spiReadRegister(0x7F);
	}
}

void Xl4432::checkForNewPacket()
{
	uint8_t data = spiReadRegister(0x3);
	if (data & FIFO_FULL) {
		spiXl4432Fifo();
	} else if (data & VALID_PACKET) {
		readPacketFromFifo();
		binToHexString();
		meterMeasurment = extractMeterReading();
		packetReady = 1;
	}
}

uint8_t Xl4432::spiReadRegister(uint8_t addr) {
	uint8_t data;
	digitalWrite(SS, LOW);
	SPI.transfer(addr & 0x7F);
	data = SPI.transfer(0x00);
	digitalWrite(SS, HIGH);
	return data;
}

void Xl4432::spiWriteRegister(uint8_t addr, uint8_t data) {
	digitalWrite(SS, LOW);
	SPI.transfer(addr | 0x80);
	SPI.transfer(data);
	digitalWrite(SS, HIGH);
}

void Xl4432::binToHexString()
{
	// Mask last nibble (unreliable due to Manchester timing drift)
	packet[PACKET_SIZE - 1] &= 0xF0;

	char *ptr = &output[0];
	for (int i = 0; i < PACKET_SIZE; i++) {
		ptr += sprintf(ptr, "%02X", packet[i]);
	}
}
