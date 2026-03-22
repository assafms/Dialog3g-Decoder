#include "esphome/core/log.h"
#include "xl4432_spi_sensor.h"
#include "xl4432.h"

Xl4432::Xl4432(char id[3], bool use_id_as_sync)
{
	nIRQState = 0;
	meterMeasurment = -1;
	packetReady=0;
    lastMeterMeasurment = 0;
	METER_ID[0]=id[0];
	METER_ID[1]=id[1];
	METER_ID[2]=id[2];
	useIdAsSync = use_id_as_sync;
	packetSniff = false;
}



float Xl4432::extractMeterReading()
{
  if (useIdAsSync) {
    // When using ID as sync, no need to check meter ID (sync already matched it)
    // Meter reading is directly in bytes 1,2,3
    float result = float((packet[3]<<16) + (packet[2]<<8) + packet[1])/10;  
    return result;
  } else if (packetSniff) {
    // Sniff mode: extract reading from any packet, no ID check
    float result = float((packet[12]<<16) + (packet[11]<<8) + packet[10])/10;
    return result;
  } else {
    // Original behavior - meter ID at bytes 5,6,7
    if(packet[5] == METER_ID[0] &&
       packet[6] == METER_ID[1] &&
       packet[7] == METER_ID[2])
    {
      float result = float((packet[12]<<16) + (packet[11]<<8) + packet[10])/10;
      return result;
    }
    else
      return -1;
  }
  return -1;
}

void Xl4432::readPacketFromFifo()
{
    //Serial.println("Reading packet from fifo");
    spiWriteRegister(0x07, 0x01);  // disable the receiver chain write 0x01 to the Operating Function Control 1 register
    spiReadPacketFromFifo(PACKET_SIZE);
    // reset the RX FIFO
    spiWriteRegister(0x08, 0x02);  // write 0x02 to the Operating Function Control 2 register
    spiWriteRegister(0x08, 0x00);  // write 0x00 to the Operating Function Control 2 register
    // enable the receiver chain again
    spiWriteRegister(0x07, 0x05);  // write 0x05 to the Operating Function Control 1 register
}

void Xl4432::spiInitRadio()
{
    spiWriteRegister(0x07,0x80);  // write 0x80 to the Operating & Function Control1 register
    delay(100);
    spiReadRegister(0x03);        // read the Interrupt Status1 register and clear
    spiReadRegister(0x04);        // read the Interrupt Status2 register and clear
    delay(100);
}

void Xl4432::spiDisableReciver()
{
  spiWriteRegister(0x07, 0x01);  // disable the receiver chain write 0x01 to the Operating Function Control 1 register 
}


void Xl4432::spiEnableReciver()
{
    // enable receiver chain
    spiWriteRegister(0x07, 0x05);  //write 0x05 to the Operating Function Control 1 register
    // Enable two interrupts:
    // a) one which shows that a valid packet received: 'ipkval'
    // b) second shows if the packet received with incorrect CRC: 'icrcerror'
    spiWriteRegister(0x05, 0x03);  //write 0x03 to the Interrupt Enable 1 register
    spiWriteRegister(0x06, 0x00);  //write 0x00 to the Interrupt Enable 2 register
    // read interrupt status registers to release all pending interrupts
    spiReadRegister(0x03);  //read the Interrupt Status1 register
    spiReadRegister(0x04);  //read the Interrupt Status2 register
}



void Xl4432::initXl4432Registers()
{
  // This horible function initialize the registers to be a match to the Israeli 
  // WaterMeter format. this packet format was reversed by me using an HackRF
  // and might not be a perfect match for places outside of Israel
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
    // When using meter ID as sync:
    // - Disable preamble detection completely
    // - Use 4 sync words
    // Register 0x33: Header Control 2
    // Bit 3 = 1 (header length included), Bits [2:0] = 011 (3 = 4-1 sync bytes)
    spiWriteRegister(0x33, 0x0E);  // 0x0B = 0000 1011: 4 sync words
    spiWriteRegister(0x34, 0x00);  // Preamble Length: 0 nibbles (no preamble)
    // Register 0x35: For Manchester with no preamble, we want sync word detection only
    // Keep bit 5 set to skip preamble detection, other bits as per original
    spiWriteRegister(0x35, 0x22);  // 0x22: Skip preamble (bit 5=1), keep Manchester sync timing
    
    // Set sync words: 0x4B followed by meter ID (MSB first)
    // Sync words are transmitted/received in order: Sync3, Sync2, Sync1, Sync0
    spiWriteRegister(0x36, 0x4B);        // Sync Word 3 (transmitted first)
    spiWriteRegister(0x37, METER_ID[0]); // Sync Word 2 (MSB of meter ID)
    spiWriteRegister(0x38, METER_ID[1]); // Sync Word 1 (middle byte of meter ID)
    spiWriteRegister(0x39, METER_ID[2]); // Sync Word 0 (LSB of meter ID, transmitted last)
    
    // Log the sync configuration for debugging
    ESP_LOGD("custom", "TEST MODE: Sync words set to 0x4B 0x%02X 0x%02X 0x%02X", 
             METER_ID[0], METER_ID[1], METER_ID[2]);
  } else {
    // Original configuration with preamble and 2 sync words
    spiWriteRegister(0x33, 0x0A);  // Sync Word Length: 2 bytes
    spiWriteRegister(0x34, 0x07);  // Preamble length
    spiWriteRegister(0x35, 0x3A);  // Preamble Detection Control
    spiWriteRegister(0x36, 0x3e);  // first sync
    spiWriteRegister(0x37, 0x69);  // second sync
  }
  
  spiWriteRegister(0x3E, 0x28);  //length of packet
  spiWriteRegister(0x6E, 0x01);
  spiWriteRegister(0x6F, 0x3B);
  spiWriteRegister(0x70, 0x22);  //invert manchester?
  spiWriteRegister(0x71, 0x26);
  spiWriteRegister(0x72, 0x18);
  spiWriteRegister(0x75, 0x75);
  spiWriteRegister(0x76, 0xCB);
  spiWriteRegister(0x77, 0xC0);
  spiWriteRegister(0x0b, 0x14);  // GPIO0 RX Data
  spiWriteRegister(0x0C, 0x19);  // GPIO1 Valid preamble
  spiWriteRegister(0x0D, 0x0F);  // GPIO2
  spiEnableReciver();
}


void Xl4432::spiXl4432Fifo()
{
    uint8_t data;
    for(uint8_t x=0;x<255;x++)
    {
        data = spiReadRegister(0x7F);
    }
    
}


void Xl4432::spiReadPacketFromFifo(uint8_t length)
{
    uint8_t data;
    for(uint8_t x=0;x<length+1;x++)
    {
        packet[x] = spiReadRegister(0x7F);
    }
   
}
    
void Xl4432::checkForNewPacket()
{
    uint8_t data = spiReadRegister(0x3);
    //Serial.print(",0x");
    //Serial.println(data,HEX);
    if(data & FIFO_FULL)
    {
        spiXl4432Fifo();   
    }
    else if(data & VALID_PACKET)
    {
        readPacketFromFifo();
        meterMeasurment = extractMeterReading();
        binToHexString();
        packetReady=1;
        
        
    }
}
uint8_t Xl4432::spiReadRegister(uint8_t addr) {
  uint8_t data;
  digitalWrite(SS, LOW);
  SPI.transfer(addr & 0x7F);    //set address as read
  data = SPI.transfer(0x00);    //get data
  digitalWrite(SS, HIGH);
  return (data);
}

void Xl4432::spiWriteRegister(uint8_t addr, uint8_t data) {
  digitalWrite(SS, LOW);
  SPI.transfer(addr | 0x80);     //set address as write
  SPI.transfer(data);     //write data
  digitalWrite(SS, HIGH);
}

void Xl4432::binToHexString()
{
	char *ptr = &output[0];
	int i;

	for (i = 0; i < PACKET_SIZE; i++) {
		ptr += sprintf(ptr, "%02X", packet[i]);
	}
}