#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace hub {

struct Bme280Reading {
  double temperature_c;
  double humidity_percent;
  double pressure_hpa;
};

// Factory-programmed calibration coefficients read from the sensor at
// startup (BME280 datasheet section 4.2.2). Exposed publicly so the
// compensation math can be unit-tested against contrived coefficients
// without needing real I2C hardware.
struct Bme280Calibration {
  uint16_t dig_T1 = 0;
  int16_t dig_T2 = 0, dig_T3 = 0;

  uint16_t dig_P1 = 0;
  int16_t dig_P2 = 0, dig_P3 = 0, dig_P4 = 0, dig_P5 = 0, dig_P6 = 0, dig_P7 = 0, dig_P8 = 0,
          dig_P9 = 0;

  uint8_t dig_H1 = 0, dig_H3 = 0;
  int16_t dig_H2 = 0, dig_H4 = 0, dig_H5 = 0;
  int8_t dig_H6 = 0;
};

// Talks to a BME280 temperature/humidity/pressure sensor over I2C using raw
// Linux i2c-dev ioctls (no libi2c/smbus dependency). Degrades gracefully —
// same philosophy as GpioController — if the bus or sensor isn't present.
class Bme280Sensor {
 public:
  Bme280Sensor(const std::string& i2c_bus_path, uint8_t address);
  ~Bme280Sensor();

  Bme280Sensor(const Bme280Sensor&) = delete;
  Bme280Sensor& operator=(const Bme280Sensor&) = delete;

  bool available() const { return available_; }

  // Triggers a forced-mode measurement and reads it back. Returns nullopt on
  // I2C failure (transient) or if the sensor wasn't available at startup.
  std::optional<Bme280Reading> read();

  // Pure compensation math from the BME280 datasheet (section 4.2.3),
  // separated from hardware I/O so it can be unit-tested directly.
  static Bme280Reading compensate(const Bme280Calibration& calib, int32_t adc_temperature,
                                   int32_t adc_pressure, int32_t adc_humidity);

 private:
  bool write_register(uint8_t reg, uint8_t value);
  bool read_registers(uint8_t reg, uint8_t* buf, size_t len);
  bool load_calibration();
  bool wait_for_measurement();

  std::string bus_path_;
  uint8_t address_;
  int fd_ = -1;
  bool available_ = false;
  Bme280Calibration calib_;
};

}  // namespace hub
