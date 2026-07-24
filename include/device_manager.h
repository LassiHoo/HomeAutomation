#pragma once

#include <optional>
#include <string>
#include <vector>

namespace hub {

struct DeviceConfig {
  int pin;
  std::string name;
  std::string component;
};

// Loads and holds the static device -> GPIO pin mapping from config/devices.json.
class DeviceManager {
 public:
  explicit DeviceManager(const std::string& config_path);

  const std::string& gpio_chip_path() const { return gpio_chip_path_; }
  const std::vector<DeviceConfig>& devices() const { return devices_; }
  std::optional<DeviceConfig> find_by_pin(int pin) const;

 private:
  std::string gpio_chip_path_;
  std::vector<DeviceConfig> devices_;
};

}  // namespace hub
