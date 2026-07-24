#include "device_manager.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace hub {

DeviceManager::DeviceManager(const std::string& config_path) {
  auto logger = spdlog::get("device_manager");

  std::ifstream in(config_path);
  if (!in) {
    throw std::runtime_error("could not open device config: " + config_path);
  }

  nlohmann::json j;
  in >> j;

  gpio_chip_path_ = j.at("gpio_chip").get<std::string>();

  for (const auto& entry : j.at("devices")) {
    DeviceConfig device{
        entry.at("pin").get<int>(),
        entry.at("name").get<std::string>(),
        entry.at("component").get<std::string>(),
    };
    devices_.push_back(std::move(device));
  }

  if (logger) {
    logger->info("loaded {} device(s) from {} (chip {})", devices_.size(),
                  config_path, gpio_chip_path_);
  }
}

std::optional<DeviceConfig> DeviceManager::find_by_pin(int pin) const {
  for (const auto& device : devices_) {
    if (device.pin == pin) {
      return device;
    }
  }
  return std::nullopt;
}

}  // namespace hub
