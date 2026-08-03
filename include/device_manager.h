#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hub {

struct DeviceConfig {
  int pin;
  std::string name;
  std::string component;
};

// Loads and holds the static device -> GPIO pin mapping and I2C sensor
// config from config/devices.json.
class DeviceManager {
 public:
  explicit DeviceManager(const std::string& config_path);

  const std::string& gpio_chip_path() const { return gpio_chip_path_; }
  const std::vector<DeviceConfig>& devices() const { return devices_; }
  std::optional<DeviceConfig> find_by_pin(int pin) const;

  const std::string& bme280_i2c_bus() const { return bme280_i2c_bus_; }
  uint8_t bme280_address() const { return bme280_address_; }

 private:
  std::string gpio_chip_path_;
  std::vector<DeviceConfig> devices_;

  std::string bme280_i2c_bus_ = "/dev/i2c-1";
  uint8_t bme280_address_ = 0x76;
};

}  // namespace hub
