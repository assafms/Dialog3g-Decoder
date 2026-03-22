#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"

#define FIFO_FULL    0x40
#define VALID_PACKET 0x02
#define PACKET_SIZE  0x15

enum PacketStatus {
  PKT_LEARNING,
  PKT_VALID,
  PKT_INVALID,
  PKT_ID_MISMATCH,
  PKT_SNIFF,
};

class Xl4432 {
  public:
	char  METER_ID[3];
	volatile int nIRQState;
	uint8_t packet[PACKET_SIZE];
	char  output[(PACKET_SIZE * 2) + 1];
	float meterMeasurment;
	float lastMeterMeasurment;
	bool  packetReady;
	bool  useIdAsSync;
	bool  packetSniff;
	bool  constantLearned;
	uint64_t learnedConstant;
	void  spiDisableReciver();
	void  spiEnableReciver();
	void  checkForNewPacket();
	void  initXl4432Registers();
	Xl4432(char id[3], bool use_id_as_sync = false);
	uint64_t deriveConstant();
	PacketStatus validatePacket();

  private:
	float extractMeterReading();
	void  readPacketFromFifo();
	void  spiInitRadio();
	void  spiXl4432Fifo();
	void  spiReadPacketFromFifo(uint8_t length);
	uint8_t spiReadRegister(uint8_t addr);
	void  spiWriteRegister(uint8_t addr, uint8_t data);
	void  binToHexString();
};
