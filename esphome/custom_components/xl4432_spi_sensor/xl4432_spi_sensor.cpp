#include "esphome/core/log.h"
#include "xl4432_spi_sensor.h"
#include "xl4432.h"



namespace esphome {
namespace xl4432_spi_sensor {

static const char *TAG = "xl4432_spi_sensor.sensor";
#define nIRQ_PIN     5

# change this to your METER_ID  , in most cases it is on the meter (decimal) or in the bill and needs to be converted 
# Meter ID = 12345678  => 0xBC614E  => METER_ID[]= {0x4E,0x61,0xBC}
char  METER_ID[]= {0x4E,0x61,0xBC};
//char  METER_ID[]= {0x12,0x34,0x56};    //Use this if you want do debug any meter ID 

Xl4432 xl4432(METER_ID);



ICACHE_RAM_ATTR void nIRQ_ISR(){
  xl4432.spiDisableReciver();
  xl4432.checkForNewPacket();
  xl4432.spiEnableReciver();
}



void Xl4432SPISensor::setup() {
    pinMode(SS, OUTPUT);
    digitalWrite(SS, HIGH);
    pinMode(nIRQ_PIN, INPUT);
    attachInterrupt(nIRQ_PIN, nIRQ_ISR, FALLING);
    xl4432.initXl4432Registers();
    xl4432.lastMeterMeasurment = 0 ;
	
}

void Xl4432SPISensor::update() {

}

void Xl4432SPISensor::loop() {

if(xl4432.packetReady and xl4432.meterMeasurment>0)
    {
      xl4432.packetReady = 0;
      // We dont have any CRC so we wait for 2 measurments that are the same 
      // before we send the information to HA 
      if (xl4432.meterMeasurment == xl4432.lastMeterMeasurment){
          publish_state(xl4432.meterMeasurment);
          ESP_LOGD("custom","Raw Packet:%s",xl4432.output);
      }
      else{
        xl4432.lastMeterMeasurment = xl4432.meterMeasurment;
        ESP_LOGD("custom","Raw Packet:%s",xl4432.output);
        ESP_LOGD("custom","Value has changed , waiting for another");
           
      }
      
	  
    }

if(xl4432.packetReady and xl4432.meterMeasurment<0)
    {
      xl4432.packetReady = 0;
	    ESP_LOGD("custom","Unknown Packet:%s",xl4432.output);
    }


}

void Xl4432SPISensor::dump_config(){
    ESP_LOGCONFIG(TAG, "Xl4432 SPI sensor");
}

}  // namespace xl4432_spi_sensor
}  // namespace esphome