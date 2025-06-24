#include "esphome/core/log.h"
#include "xl4432_spi_sensor.h"
#include "xl4432.h"
#include <string>
#include <vector>
#include <algorithm>

namespace esphome {
namespace xl4432_spi_sensor {

static const char *TAG = "xl4432_spi_sensor.sensor";
#define nIRQ_PIN     5

// Default meter ID (fallback)
char  METER_ID[]= {0x12,0x34,0x56};

// Global xl4432 instance
Xl4432 xl4432(METER_ID);

void Xl4432SPISensor::set_meter_id(const std::string &meter_id) {
  // Parse the meter_id string (expected format: "0x4E61BC" or "4E61BC")
  std::string id_str = meter_id;
  
  // Remove spaces and convert to uppercase
  id_str.erase(std::remove(id_str.begin(), id_str.end(), ' '), id_str.end());
  std::transform(id_str.begin(), id_str.end(), id_str.begin(), ::toupper);
  
  // Remove 0x prefix if present
  if (id_str.substr(0, 2) == "0X") {
    id_str = id_str.substr(2);
  }
  
  // Check if it's a 6-character hex string (3 bytes)
  if (id_str.length() == 6) {
    // Check if all characters are valid hex digits
    bool valid_hex = true;
    for (char c : id_str) {
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
        valid_hex = false;
        break;
      }
    }
    
    if (valid_hex) {
      // Parse the hex string and extract 3 bytes
      uint32_t full_id = std::stoul(id_str, nullptr, 16);
      METER_ID[0] = (char)((full_id >> 16) & 0xFF);  // Most significant byte
      METER_ID[1] = (char)((full_id >> 8) & 0xFF);   // Middle byte
      METER_ID[2] = (char)(full_id & 0xFF);          // Least significant byte
      
      // Update the xl4432 instance with new meter ID
      xl4432 = Xl4432(METER_ID);
    } else {
      ESP_LOGW(TAG, "Invalid hex characters in meter_id, using default");
    }
  } else {
    ESP_LOGW(TAG, "Invalid meter_id format, using default");
  }
}

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