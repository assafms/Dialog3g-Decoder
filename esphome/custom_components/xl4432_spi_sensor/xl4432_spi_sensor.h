#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"

#ifdef USE_ARDUINO
#include <WiFiServer.h>
#include <WiFiClient.h>
#endif

namespace esphome {
namespace xl4432_spi_sensor {

#define SNIFF_TCP_PORT 4321

ICACHE_RAM_ATTR void nIRQ_ISR();

class Xl4432SPISensor : public sensor::Sensor,
                       public PollingComponent,
                       public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                            spi::DATA_RATE_1KHZ> {
 public:
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  void set_meter_id(const std::string &meter_id);
  void set_packet_sniff(bool packet_sniff);
  void send_to_clients(const char *line);

 private:
#ifdef USE_ARDUINO
  WiFiServer tcp_server_{SNIFF_TCP_PORT};
  WiFiClient tcp_clients_[3];
#endif
};

}  // namespace xl4432_spi_sensor
}  // namespace esphome