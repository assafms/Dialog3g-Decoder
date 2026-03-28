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

char  METER_ID[]= {0x12,0x34,0x56};

bool PACKET_SNIFF = false;
Xl4432 xl4432(METER_ID);

void Xl4432SPISensor::set_meter_id(const std::string &meter_id) {
  std::string id_str = meter_id;
  id_str.erase(std::remove(id_str.begin(), id_str.end(), ' '), id_str.end());
  std::transform(id_str.begin(), id_str.end(), id_str.begin(), ::toupper);
  if (id_str.substr(0, 2) == "0X") {
    id_str = id_str.substr(2);
  }
  if (id_str.length() == 6) {
    bool valid_hex = true;
    for (char c : id_str) {
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
        valid_hex = false;
        break;
      }
    }
    if (valid_hex) {
      uint32_t full_id = std::stoul(id_str, nullptr, 16);
      METER_ID[0] = (char)((full_id >> 16) & 0xFF);
      METER_ID[1] = (char)((full_id >> 8) & 0xFF);
      METER_ID[2] = (char)(full_id & 0xFF);
      xl4432 = Xl4432(METER_ID);
    } else {
      ESP_LOGW(TAG, "Invalid hex characters in meter_id, using default");
    }
  } else {
    ESP_LOGW(TAG, "Invalid meter_id format, using default");
  }
}

void Xl4432SPISensor::set_packet_sniff(bool packet_sniff) {
  PACKET_SNIFF = packet_sniff;
  xl4432.packetSniff = packet_sniff;
}

void Xl4432SPISensor::set_tcp_server(bool enabled) {
  tcp_enabled_ = enabled;
}

IRAM_ATTR void nIRQ_ISR(){
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
    xl4432.lastMeterMeasurment = 0;
#ifdef USE_ARDUINO
    if (tcp_enabled_) {
        tcp_server_.begin();
        ESP_LOGI(TAG, "TCP sniff server on port %d", SNIFF_TCP_PORT);
    }
#endif
}

void Xl4432SPISensor::send_to_clients(const char *line) {
#ifdef USE_ARDUINO
    if (!tcp_enabled_) return;
    // Send to all connected clients
    for (int i = 0; i < 3; i++) {
        if (tcp_clients_[i] && tcp_clients_[i].connected()) {
            tcp_clients_[i].print(line);
            tcp_clients_[i].print("\n");
        }
    }
#endif
}

void Xl4432SPISensor::update() {
}

void Xl4432SPISensor::loop() {

#ifdef USE_ARDUINO
if (tcp_enabled_) {
    WiFiClient newClient = tcp_server_.available();
    if (newClient) {
        for (int i = 0; i < 3; i++) {
            if (!tcp_clients_[i] || !tcp_clients_[i].connected()) {
                tcp_clients_[i] = newClient;
                ESP_LOGI(TAG, "TCP client %d connected", i);
                break;
            }
        }
    }
}
#endif

if (!xl4432.packetReady)
    return;

xl4432.packetReady = 0;

// Always send to TCP clients (sniff format)
{
    char tcp_line[64];
    snprintf(tcp_line, sizeof(tcp_line), "[sniff:916300000]: %s", xl4432.output);
    send_to_clients(tcp_line);
}

if (PACKET_SNIFF) {
    ESP_LOGI("sniff", "%s", xl4432.output);
    return;
}

PacketStatus status = xl4432.validatePacket();
stat_total_++;

switch (status) {
    case PKT_VALID:
        stat_valid_++;
        publish_state(xl4432.meterMeasurment);
        ESP_LOGI("gf2", "Valid: reading=%.1f pkt=%s",
                 xl4432.meterMeasurment, xl4432.output);
        break;

    case PKT_CORRECTED_1:
    case PKT_CORRECTED_2:
    case PKT_CORRECTED_3: {
        int nbits = status - PKT_CORRECTED_1 + 1;
        stat_corrected_[nbits - 1]++;
        xl4432.meterMeasurment = xl4432.extractMeterReading();
        publish_state(xl4432.meterMeasurment);
        ESP_LOGI("gf2", "Corrected (%dbit): reading=%.1f pkt=%s",
                 nbits, xl4432.meterMeasurment, xl4432.output);
        break;
    }

    case PKT_INVALID:
        stat_invalid_++;
        ESP_LOGW("gf2", "RF error: reading=%.1f pkt=%s",
                 xl4432.meterMeasurment, xl4432.output);
        break;

    case PKT_ID_MISMATCH:
        ESP_LOGD("gf2", "Other meter: %s", xl4432.output);
        break;

    case PKT_VALID_TWO_PKT:
        stat_valid_++;
        publish_state(xl4432.meterMeasurment);
        ESP_LOGI("gf2", "Valid (2pkt): reading=%.1f pkt=%s",
                 xl4432.meterMeasurment, xl4432.output);
        break;

    case PKT_NON_STANDARD:
        ESP_LOGI("gf2", "Non-std first pkt: reading=%.1f pkt=%s",
                 xl4432.meterMeasurment, xl4432.output);
        break;

    default:
        break;
}

// Log stats every 10 packets
if (stat_total_ > 0 && stat_total_ % 10 == 0) {
    uint32_t corrected = stat_corrected_[0] + stat_corrected_[1] + stat_corrected_[2];
    ESP_LOGI("stats", "Pkts=%lu Valid=%lu Fix1=%lu Fix2=%lu Fix3=%lu Bad=%lu (%.0f%% ok)",
             stat_total_, stat_valid_,
             stat_corrected_[0], stat_corrected_[1], stat_corrected_[2],
             stat_invalid_,
             100.0 * (stat_valid_ + corrected) / stat_total_);
}

}

void Xl4432SPISensor::dump_config(){
    ESP_LOGCONFIG(TAG, "Xl4432 SPI sensor");
}

}  // namespace xl4432_spi_sensor
}  // namespace esphome
