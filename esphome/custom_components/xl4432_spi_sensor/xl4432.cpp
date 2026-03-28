#include "esphome/core/log.h"
#include "xl4432_spi_sensor.h"
#include "xl4432.h"

// LFSR seed and submask table — generates all basis vectors for bytes 5-11
static const uint64_t LFSR_SEED = 0xA045A72F80ULL;

static const uint64_t LFSR_SUBMASK[8] = {
    0x0000000000ULL, 0x00018F36C8ULL, 0x201080D890ULL, 0x20110FEE58ULL,
    0x00014013F8ULL, 0x0000CF2530ULL, 0x2011C0CB68ULL, 0x20104FFDA0ULL,
};

// STD group correction: XOR this into byte-7 vectors at bit positions 2,3,5,6,7
static const uint64_t BYTE7_CORRECTION = 0x34E4E74B50ULL;
static const uint8_t  STD_CORRECTION_MASK = 0xEC;  // bits 2,3,5,6,7

// General alarm flag (byte 4 bit 5) scramble contribution
static const uint64_t ALARM_VEC = 0xA8F1156730ULL;

// CONS_BASIS[16-23] — byte 12, not part of LFSR, stored separately
static const uint64_t CONS_HI[8] = {
    0x51AAF3D980ULL, 0x2826118BE0ULL, 0xADEBE64938ULL, 0x2D02BFE790ULL,
    0x5A04F0F9E8ULL, 0xB4086EC518ULL, 0xC0E088FEF8ULL, 0xD022B40558ULL,
};

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
	int key = ((v >> 39) & 1) * 4 + ((v >> 31) & 1) * 2 + ((v >> 23) & 1);
	return ((v << 1) & 0xFFFFFFFFFFULL) ^ LFSR_SUBMASK[key];
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
	// Sequence: byte11, byte10, byte9, byte8, byte7, byte6, byte5
	uint64_t v = LFSR_SEED;

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

	// Byte 12 (consumption bits 16-23 — separate, not LFSR)
	for (int i = 0; i < 8; i++) {
		if (packet[12] & (1 << i))
			result ^= CONS_HI[i];
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

	// Strip consumption contribution using LFSR
	uint64_t v = LFSR_SEED;
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
	// Byte 12 (separate)
	for (int i = 0; i < 8; i++) {
		if (packet[12] & (1 << i))
			result ^= CONS_HI[i];
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

	// Universal validation: works for STD, and any group whose byte-7
	// correction mask is known. Currently only STD (0x0000) is fully solved.
	if (packet[8] == 0x00 && packet[9] == 0x00) {
		uint64_t expected = expectedScramble();
		uint64_t actual = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
		                  ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
		                  (uint64_t)packet[19];
		if (expected == actual)
			return PKT_VALID;
		else
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
