#include "esphome/core/log.h"
#include "xl4432_spi_sensor.h"
#include "xl4432.h"

static const uint64_t OFFSET = 0xDF750DC2C0ULL;

static const uint64_t CONS_BASIS[24] = {
    0x61B89FB6A0ULL, 0xE360308318ULL, 0xC6C12115C8ULL, 0xAD9382E0F8ULL,
    0x7B374A3C50ULL, 0xF66E9478A0ULL, 0xECDDE7D470ULL, 0xF9AB805540ULL,
    0xA045A72F80ULL, 0x408B817A30ULL, 0xA1060D1A38ULL, 0x420D5A2788ULL,
    0x841AB44F10ULL, 0x0835A7BB10ULL, 0x106AC040E8ULL, 0x20D40FB718ULL,
    0x51AAF3D980ULL, 0x2826118BE0ULL, 0xADEBE64938ULL, 0x2D02BFE790ULL,
    0x5A04F0F9E8ULL, 0xB4086EC518ULL, 0xC0E088FEF8ULL, 0xD022B40558ULL,
};

static const uint64_t X40_OFFSET = 0xAAF90B5990ULL;
static const uint64_t D3C_OFFSET = 0x6D2A310958ULL;

static const uint64_t ID_BASIS[24] = {
    0x456FF2CC60ULL, 0x8ADE6AAE08ULL, 0x0149F2DC28ULL, 0x7FAE4CBD30ULL,
    0x9694D8DA08ULL, 0x39DD1902E0ULL, 0x2E9694EEF8ULL, 0x0000000000ULL,
    0x49D8C178F8ULL, 0xB3A08D1FA8ULL, 0x475155C2F0ULL, 0x8EA2AB85E0ULL,
    0x3D5518F660ULL, 0x7AAA31ECC0ULL, 0xD544E30110ULL, 0xAA89092710ULL,
    0x7503D28548ULL, 0xEA062A3C58ULL, 0xD40D146B48ULL, 0xA81B68C568ULL,
    0x5037919928ULL, 0xA06EAC0498ULL, 0x40DD972C00ULL, 0xA1AA21B658ULL,
};

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

uint64_t Xl4432::expectedScramble()
{
	uint32_t meter_id = ((uint32_t)packet[5] << 16) | ((uint32_t)packet[6] << 8) | packet[7];
	uint32_t cons = packet[10] | (packet[11] << 8) | ((uint32_t)packet[12] << 16);
	uint64_t result = OFFSET;
	for (int i = 0; i < 24; i++) {
		if (meter_id & (1 << i))
			result ^= ID_BASIS[i];
	}
	for (int i = 0; i < 24; i++) {
		if (cons & (1 << i))
			result ^= CONS_BASIS[i];
	}
	return result;
}

uint64_t Xl4432::deriveConstant()
{
	uint32_t cons = packet[10] | (packet[11] << 8) | ((uint32_t)packet[12] << 16);
	uint64_t actual = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
	                  ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
	                  (uint64_t)packet[19];
	uint64_t result = actual;
	for (int i = 0; i < 24; i++) {
		if (cons & (1 << i))
			result ^= CONS_BASIS[i];
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

	// Standard meters: full GF(2) validation
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

	// x40/Sonata meters: same ID_BASIS, different OFFSET
	if (packet[9] == 0x40) {
		uint32_t meter_id = ((uint32_t)packet[5] << 16) | ((uint32_t)packet[6] << 8) | packet[7];
		uint32_t cons = packet[10] | (packet[11] << 8) | ((uint32_t)packet[12] << 16);
		uint64_t result = X40_OFFSET;
		for (int i = 0; i < 24; i++) {
			if (meter_id & (1 << i))
				result ^= ID_BASIS[i];
		}
		for (int i = 0; i < 24; i++) {
			if (cons & (1 << i))
				result ^= CONS_BASIS[i];
		}
		uint64_t actual = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
		                  ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
		                  (uint64_t)packet[19];
		if (result == actual)
			return PKT_VALID;
		else
			return PKT_INVALID;
	}

	// 3D0C meters: same ID_BASIS, different OFFSET
	if (packet[8] == 0x3D && packet[9] == 0x0C) {
		uint32_t meter_id = ((uint32_t)packet[5] << 16) | ((uint32_t)packet[6] << 8) | packet[7];
		uint32_t cons = packet[10] | (packet[11] << 8) | ((uint32_t)packet[12] << 16);
		uint64_t result = D3C_OFFSET;
		for (int i = 0; i < 24; i++) {
			if (meter_id & (1 << i))
				result ^= ID_BASIS[i];
		}
		for (int i = 0; i < 24; i++) {
			if (cons & (1 << i))
				result ^= CONS_BASIS[i];
		}
		uint64_t actual = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
		                  ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
		                  (uint64_t)packet[19];
		if (result == actual)
			return PKT_VALID;
		else
			return PKT_INVALID;
	}

	// Other unknown groups: two-packet validation fallback
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
