#include "esphome/core/log.h"
#include "xl4432_spi_sensor.h"
#include "xl4432.h"

// Consumption basis vectors (bits 0-14, universal across all meter types)
static const uint64_t CONS_BASIS[15] = {
    0x61B89FB6A0ULL, 0xE360308318ULL, 0xC6C12115C8ULL, 0xAD9382E0F8ULL,
    0x7B374A3C50ULL, 0xF66E9478A0ULL, 0xECDDE7D470ULL, 0xF9AB805540ULL,
    0xA045A72F80ULL, 0x408B817A30ULL, 0xA1060D1A38ULL, 0x420D5A2788ULL,
    0x841AB44F10ULL, 0x0835A7BB10ULL, 0x106AC040E8ULL,
};

Xl4432::Xl4432(char id[3], bool use_id_as_sync)
{
	nIRQState = 0;
	meterMeasurment = -1;
	packetReady = 0;
	lastMeterMeasurment = 0;
	METER_ID[0] = id[0];
	METER_ID[1] = id[1];
	METER_ID[2] = id[2];
	useIdAsSync = use_id_as_sync;
	packetSniff = false;
	constantLearned = false;
	learnedConstant = 0;
}

uint64_t Xl4432::deriveConstant()
{
	uint32_t cons = packet[10] | (packet[11] << 8) | ((uint32_t)packet[12] << 16);
	uint64_t scram = ((uint64_t)packet[15] << 32) | ((uint64_t)packet[16] << 24) |
	                 ((uint64_t)packet[17] << 16) | ((uint64_t)packet[18] << 8) |
	                 (uint64_t)packet[19];
	uint64_t result = scram;
	for (int i = 0; i < 15; i++) {
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

	uint64_t thisConstant = deriveConstant();

	if (!constantLearned) {
		learnedConstant = thisConstant;
		constantLearned = true;
		return PKT_LEARNING;
	}

	if (thisConstant == learnedConstant)
		return PKT_VALID;
	else
		return PKT_INVALID;
}

float Xl4432::extractMeterReading()
{
	if (useIdAsSync) {
		return float((packet[3] << 16) + (packet[2] << 8) + packet[1]) / 10;
	} else {
		return float((packet[12] << 16) + (packet[11] << 8) + packet[10]) / 10;
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
