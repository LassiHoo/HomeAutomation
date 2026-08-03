#include "bme280_sensor.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <spdlog/spdlog.h>
#include <thread>

namespace hub {

namespace {
constexpr uint8_t kRegChipId = 0xD0;
constexpr uint8_t kChipIdBme280 = 0x60;
constexpr uint8_t kRegCalib00 = 0x88;   // 26 bytes: 0x88-0xA1
constexpr uint8_t kRegCalib26 = 0xE1;   // 7 bytes: 0xE1-0xE7
constexpr uint8_t kRegCtrlHum = 0xF2;
constexpr uint8_t kRegStatus = 0xF3;
constexpr uint8_t kRegCtrlMeas = 0xF4;
constexpr uint8_t kRegConfig = 0xF5;
constexpr uint8_t kRegPressMsb = 0xF7;  // 8 bytes: press(3) + temp(3) + hum(2)

constexpr uint8_t kCtrlHumOversampleX1 = 0x01;
constexpr uint8_t kCtrlMeasForcedOversampleX1 = 0x25;  // osrs_t=1, osrs_p=1, mode=forced
constexpr uint8_t kStatusMeasuringBit = 0x08;
}  // namespace

Bme280Sensor::Bme280Sensor(const std::string& i2c_bus_path, uint8_t address)
    : bus_path_(i2c_bus_path), address_(address) {
  auto logger = spdlog::get("bme280");

  fd_ = open(bus_path_.c_str(), O_RDWR);
  if (fd_ < 0) {
    if (logger) {
      logger->error("failed to open I2C bus {}: {} — BME280 disabled", bus_path_,
                    std::strerror(errno));
    }
    return;
  }

  uint8_t chip_id = 0;
  if (!read_registers(kRegChipId, &chip_id, 1) || chip_id != kChipIdBme280) {
    if (logger) {
      logger->error(
          "BME280 not detected on {} at address 0x{:02x} (chip_id=0x{:02x}) — disabled",
          bus_path_, address_, chip_id);
    }
    close(fd_);
    fd_ = -1;
    return;
  }

  if (!load_calibration()) {
    if (logger) logger->error("failed to read BME280 calibration data — disabled");
    close(fd_);
    fd_ = -1;
    return;
  }

  write_register(kRegCtrlHum, kCtrlHumOversampleX1);
  write_register(kRegConfig, 0x00);

  available_ = true;
  if (logger) logger->info("BME280 initialized on {} at address 0x{:02x}", bus_path_, address_);
}

Bme280Sensor::~Bme280Sensor() {
  if (fd_ >= 0) close(fd_);
}

bool Bme280Sensor::write_register(uint8_t reg, uint8_t value) {
  uint8_t buf[2] = {reg, value};
  i2c_msg msg{};
  msg.addr = address_;
  msg.flags = 0;
  msg.len = sizeof(buf);
  msg.buf = buf;

  i2c_rdwr_ioctl_data data{};
  data.msgs = &msg;
  data.nmsgs = 1;

  return ioctl(fd_, I2C_RDWR, &data) >= 0;
}

bool Bme280Sensor::read_registers(uint8_t reg, uint8_t* buf, size_t len) {
  i2c_msg msgs[2]{};
  msgs[0].addr = address_;
  msgs[0].flags = 0;
  msgs[0].len = 1;
  msgs[0].buf = &reg;

  msgs[1].addr = address_;
  msgs[1].flags = I2C_M_RD;
  msgs[1].len = static_cast<uint16_t>(len);
  msgs[1].buf = buf;

  i2c_rdwr_ioctl_data data{};
  data.msgs = msgs;
  data.nmsgs = 2;

  return ioctl(fd_, I2C_RDWR, &data) >= 0;
}

bool Bme280Sensor::load_calibration() {
  uint8_t buf[26];
  if (!read_registers(kRegCalib00, buf, sizeof(buf))) return false;

  calib_.dig_T1 = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
  calib_.dig_T2 = static_cast<int16_t>(buf[2] | (buf[3] << 8));
  calib_.dig_T3 = static_cast<int16_t>(buf[4] | (buf[5] << 8));

  calib_.dig_P1 = static_cast<uint16_t>(buf[6] | (buf[7] << 8));
  calib_.dig_P2 = static_cast<int16_t>(buf[8] | (buf[9] << 8));
  calib_.dig_P3 = static_cast<int16_t>(buf[10] | (buf[11] << 8));
  calib_.dig_P4 = static_cast<int16_t>(buf[12] | (buf[13] << 8));
  calib_.dig_P5 = static_cast<int16_t>(buf[14] | (buf[15] << 8));
  calib_.dig_P6 = static_cast<int16_t>(buf[16] | (buf[17] << 8));
  calib_.dig_P7 = static_cast<int16_t>(buf[18] | (buf[19] << 8));
  calib_.dig_P8 = static_cast<int16_t>(buf[20] | (buf[21] << 8));
  calib_.dig_P9 = static_cast<int16_t>(buf[22] | (buf[23] << 8));

  // buf[24] is register 0xA0, reserved.
  calib_.dig_H1 = buf[25];  // register 0xA1

  uint8_t buf2[7];
  if (!read_registers(kRegCalib26, buf2, sizeof(buf2))) return false;

  calib_.dig_H2 = static_cast<int16_t>(buf2[0] | (buf2[1] << 8));
  calib_.dig_H3 = buf2[2];
  calib_.dig_H4 = static_cast<int16_t>((static_cast<int16_t>(static_cast<int8_t>(buf2[3])) << 4) |
                                       (buf2[4] & 0x0F));
  calib_.dig_H5 = static_cast<int16_t>((static_cast<int16_t>(static_cast<int8_t>(buf2[5])) << 4) |
                                       (buf2[4] >> 4));
  calib_.dig_H6 = static_cast<int8_t>(buf2[6]);

  return true;
}

bool Bme280Sensor::wait_for_measurement() {
  for (int attempt = 0; attempt < 50; ++attempt) {
    uint8_t status = 0;
    if (!read_registers(kRegStatus, &status, 1)) return false;
    if ((status & kStatusMeasuringBit) == 0) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

std::optional<Bme280Reading> Bme280Sensor::read() {
  if (!available_) return std::nullopt;
  auto logger = spdlog::get("bme280");

  if (!write_register(kRegCtrlMeas, kCtrlMeasForcedOversampleX1)) {
    if (logger) logger->warn("BME280: failed to trigger measurement");
    return std::nullopt;
  }

  if (!wait_for_measurement()) {
    if (logger) logger->warn("BME280: measurement did not complete in time");
    return std::nullopt;
  }

  uint8_t buf[8];
  if (!read_registers(kRegPressMsb, buf, sizeof(buf))) {
    if (logger) logger->warn("BME280: failed to read measurement registers");
    return std::nullopt;
  }

  int32_t adc_p = (static_cast<int32_t>(buf[0]) << 12) | (static_cast<int32_t>(buf[1]) << 4) |
                  (static_cast<int32_t>(buf[2]) >> 4);
  int32_t adc_t = (static_cast<int32_t>(buf[3]) << 12) | (static_cast<int32_t>(buf[4]) << 4) |
                  (static_cast<int32_t>(buf[5]) >> 4);
  int32_t adc_h = (static_cast<int32_t>(buf[6]) << 8) | static_cast<int32_t>(buf[7]);

  return compensate(calib_, adc_t, adc_p, adc_h);
}

// Double-precision compensation formulas from the BME280 datasheet, section
// 4.2.3. t_fine is an intermediate shared between the temperature and the
// pressure/humidity formulas, computed fresh on every call.
Bme280Reading Bme280Sensor::compensate(const Bme280Calibration& c, int32_t adc_temperature,
                                        int32_t adc_pressure, int32_t adc_humidity) {
  double var1 = (static_cast<double>(adc_temperature) / 16384.0 -
                 static_cast<double>(c.dig_T1) / 1024.0) *
                static_cast<double>(c.dig_T2);
  double var2 = ((static_cast<double>(adc_temperature) / 131072.0 -
                  static_cast<double>(c.dig_T1) / 8192.0) *
                 (static_cast<double>(adc_temperature) / 131072.0 -
                  static_cast<double>(c.dig_T1) / 8192.0)) *
                static_cast<double>(c.dig_T3);
  double t_fine = var1 + var2;
  double temperature = t_fine / 5120.0;

  double pressure = 0.0;
  var1 = t_fine / 2.0 - 64000.0;
  var2 = var1 * var1 * static_cast<double>(c.dig_P6) / 32768.0;
  var2 = var2 + var1 * static_cast<double>(c.dig_P5) * 2.0;
  var2 = var2 / 4.0 + static_cast<double>(c.dig_P4) * 65536.0;
  var1 = (static_cast<double>(c.dig_P3) * var1 * var1 / 524288.0 +
          static_cast<double>(c.dig_P2) * var1) /
         524288.0;
  var1 = (1.0 + var1 / 32768.0) * static_cast<double>(c.dig_P1);
  if (var1 != 0.0) {
    pressure = 1048576.0 - static_cast<double>(adc_pressure);
    pressure = (pressure - var2 / 4096.0) * 6250.0 / var1;
    var1 = static_cast<double>(c.dig_P9) * pressure * pressure / 2147483648.0;
    var2 = pressure * static_cast<double>(c.dig_P8) / 32768.0;
    pressure = pressure + (var1 + var2 + static_cast<double>(c.dig_P7)) / 16.0;
  }

  double humidity = t_fine - 76800.0;
  humidity = (static_cast<double>(adc_humidity) -
              (static_cast<double>(c.dig_H4) * 64.0 +
               static_cast<double>(c.dig_H5) / 16384.0 * humidity)) *
             (static_cast<double>(c.dig_H2) / 65536.0 *
              (1.0 + static_cast<double>(c.dig_H6) / 67108864.0 * humidity *
                         (1.0 + static_cast<double>(c.dig_H3) / 67108864.0 * humidity)));
  humidity = humidity * (1.0 - static_cast<double>(c.dig_H1) * humidity / 524288.0);
  if (humidity > 100.0) {
    humidity = 100.0;
  } else if (humidity < 0.0) {
    humidity = 0.0;
  }

  return Bme280Reading{temperature, humidity, pressure / 100.0};  // Pa -> hPa
}

}  // namespace hub
