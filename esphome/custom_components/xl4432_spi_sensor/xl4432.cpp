#include "esphome/core/log.h"
#include "xl4432_spi_sensor.h"
#include "xl4432.h"

// 40-bit LFSR with 3 feedback taps — generates all 64 basis vectors for bytes 5-12
// Seed generates 64 vectors in sequence: byte12, byte11, byte10, byte9, byte8, byte7, byte6, byte5
static const uint64_t LFSR_SEED  = 0x51AAF3D980ULL;  // byte 12 bit 0
static const uint64_t LFSR_TAP_A = 0x00014013F8ULL;  // feedback when bit 39 = 1
static const uint64_t LFSR_TAP_B = 0x201080D890ULL;  // feedback when bit 31 = 1
static const uint64_t LFSR_TAP_C = 0x00018F36C8ULL;  // feedback when bit 23 = 1

// STD group correction: XOR this into byte-7 vectors at bit positions 2,3,5,6,7
static const uint64_t BYTE7_CORRECTION = 0x34E4E74B50ULL;
static const uint8_t  STD_CORRECTION_MASK = 0xEC;  // bits 2,3,5,6,7

// General alarm flag (byte 4 bit 5) scramble contribution
static const uint64_t ALARM_VEC = 0xA8F1156730ULL;

// Byte 12 is now part of the LFSR — no separate table needed

// Global offset (for group 0x0000 with STD byte-7 correction applied)
static const uint64_t OFFSET = 0xDF750DC2C0ULL;

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

	// Apply byte-7 correction for non-STD groups:
	// STD (0x0000) correction is baked into OFFSET and LFSR vectors.
	// Other groups need their STD correction removed and group-specific applied.
	// For now: STD uses stored correction, others use raw LFSR (no correction).
	uint8_t corr_mask = STD_CORRECTION_MASK;
	if (packet[8] != 0x00 || packet[9] != 0x00) {
		// Non-STD group: remove STD correction from offset
		// and apply group bytes contribution via LFSR vectors
		corr_mask = 0x00;  // use raw LFSR byte-7 vectors
	}

	// Generate basis vectors via LFSR and apply to input bytes
	// Sequence: byte12, byte11, byte10, byte9, byte8, byte7, byte6, byte5
	uint64_t v = LFSR_SEED;

	// Byte 12 (consumption bits 16-23)
	for (int i = 0; i < 8; i++) {
		if (packet[12] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}

	// Byte 11 (consumption bits 8-15)
	for (int i = 0; i < 8; i++) {
		if (packet[11] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}

	// Byte 10 (consumption bits 0-7)
	for (int i = 0; i < 8; i++) {
		if (packet[10] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}

	// Byte 9 (group low)
	for (int i = 0; i < 8; i++) {
		if (packet[9] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}

	// Byte 8 (group high)
	for (int i = 0; i < 8; i++) {
		if (packet[8] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}

	// Byte 7 (ID LSB — group-specific correction)
	for (int i = 0; i < 8; i++) {
		uint64_t basis = v;
		if (corr_mask & (1 << i))
			basis ^= BYTE7_CORRECTION;
		if (packet[7] & (1 << i))
			result ^= basis;
		v = lfsr_next(v);
	}

	// Byte 6 (ID mid)
	for (int i = 0; i < 8; i++) {
		if (packet[6] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}

	// Byte 5 (ID MSB)
	for (int i = 0; i < 8; i++) {
		if (packet[5] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}

	// Byte 4 bit 5: general alarm flag
	if (packet[4] & 0x20)
		result ^= ALARM_VEC;

	return result;
}

uint64_t Xl4432::deriveConstant()
{
	uint32_t cons = packet[10] | (packet[11] << 8) | ((uint32_t)packet[12] << 16);
	uint64_t actual = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
	                  ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
	                  (uint64_t)packet[19];
	uint64_t result = actual;

	// Strip consumption contribution using LFSR (byte12, byte11, byte10)
	uint64_t v = LFSR_SEED;
	// Byte 12
	for (int i = 0; i < 8; i++) {
		if (packet[12] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}
	// Byte 11
	for (int i = 0; i < 8; i++) {
		if (packet[11] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}
	// Byte 10
	for (int i = 0; i < 8; i++) {
		if (packet[10] & (1 << i))
			result ^= v;
		v = lfsr_next(v);
	}

	return result;
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

	// Universal validation with up to 3-bit error correction.
	// Currently only STD (0x0000) is fully solved.
	if (packet[8] == 0x00 && packet[9] == 0x00) {
		uint64_t expected = expectedScramble();
		uint64_t actual = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
		                  ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
		                  (uint64_t)packet[19];
		uint64_t syndrome = expected ^ actual;

		if (syndrome == 0)
			return PKT_VALID;

		// Build syndrome table: 65 input bits + 40 scramble bits = 105
		static const int SYN_COUNT = 105;
		uint64_t syn[SYN_COUNT];
		uint64_t v = LFSR_SEED;
		// Bytes 12,11,10,9,8
		for (int i = 0; i < 40; i++) { syn[i] = v; v = lfsr_next(v); }
		// Byte 7 with correction
		for (int i = 0; i < 8; i++) {
			syn[40+i] = (STD_CORRECTION_MASK & (1<<i)) ? (v ^ BYTE7_CORRECTION) : v;
			v = lfsr_next(v);
		}
		// Bytes 6, 5
		for (int i = 0; i < 16; i++) { syn[48+i] = v; v = lfsr_next(v); }
		// Alarm flag
		syn[64] = ALARM_VEC;
		// Scramble bits
		for (int i = 0; i < 40; i++) syn[65+i] = 1ULL << i;

		// Bit index -> packet byte/bit
		static const uint8_t idx_byte[] = {
			12,12,12,12,12,12,12,12, 11,11,11,11,11,11,11,11,
			10,10,10,10,10,10,10,10,  9, 9, 9, 9, 9, 9, 9, 9,
			 8, 8, 8, 8, 8, 8, 8, 8,  7, 7, 7, 7, 7, 7, 7, 7,
			 6, 6, 6, 6, 6, 6, 6, 6,  5, 5, 5, 5, 5, 5, 5, 5,
			 4
		};

		auto flip = [&](int idx) {
			if (idx < 65) {
				uint8_t bp = idx_byte[idx];
				uint8_t bi = (idx == 64) ? 5 : (idx % 8);
				packet[bp] ^= (1 << bi);
			} else {
				int sb = idx - 65;
				packet[19 - sb/8] ^= (1 << (sb % 8));
			}
		};

		// 1-bit correction
		for (int i = 0; i < SYN_COUNT; i++) {
			if (syn[i] == syndrome) {
				flip(i);
				return PKT_CORRECTED_1;
			}
		}

		// 2-bit correction
		for (int i = 0; i < SYN_COUNT; i++) {
			uint64_t r1 = syndrome ^ syn[i];
			for (int j = i+1; j < SYN_COUNT; j++) {
				if (syn[j] == r1) {
					flip(i); flip(j);
					return PKT_CORRECTED_2;
				}
			}
		}

		// 3-bit correction (~193K iterations, ~4ms on ESP32)
		for (int i = 0; i < SYN_COUNT; i++) {
			uint64_t r1 = syndrome ^ syn[i];
			for (int j = i+1; j < SYN_COUNT; j++) {
				uint64_t r2 = r1 ^ syn[j];
				for (int k = j+1; k < SYN_COUNT; k++) {
					if (syn[k] == r2) {
						flip(i); flip(j); flip(k);
						return PKT_CORRECTED_3;
					}
				}
			}
		}

		return PKT_INVALID;
	}

	// Non-STD groups: two-packet validation fallback
	// (x40 byte-7 correction not yet fully solved)
	uint64_t constant = deriveConstant();
	if (!hasStoredConstant) {
		storedConstant = constant;
		hasStoredConstant = true;
		return PKT_NON_STANDARD;
	}
	if (constant == storedConstant)
		return PKT_VALID_TWO_PKT;
	else
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
