#include "gpio_controller.h"

#include <spdlog/spdlog.h>

namespace hub {

namespace {
constexpr const char* kConsumer = "home-automation-hub";
}

GpioController::GpioController(const std::string& chip_path,
                                const std::vector<DeviceConfig>& devices) {
  auto logger = spdlog::get("gpio");

  try {
    chip_ = std::make_unique<gpiod::chip>(chip_path);

    for (const auto& device : devices) {
      gpiod::line_settings settings;
      settings.set_direction(gpiod::line::direction::OUTPUT)
          .set_output_value(gpiod::line::value::INACTIVE);

      gpiod::line_request request = chip_->prepare_request()
                                         .set_consumer(kConsumer)
                                         .add_line_settings(device.pin, settings)
                                         .do_request();

      requests_.emplace(device.pin, std::move(request));

      if (logger) {
        logger->info("requested pin {} ({}) as output on {}", device.pin,
                      device.name, chip_path);
      }
    }

    available_ = true;
  } catch (const std::exception& e) {
    if (logger) {
      logger->error("failed to initialize GPIO chip {}: {} — GPIO control disabled",
                    chip_path, e.what());
    }
    available_ = false;
  }
}

std::optional<bool> GpioController::toggle(int pin) {
  auto logger = spdlog::get("gpio");

  if (!available_) {
    if (logger) logger->warn("toggle({}) requested but GPIO is unavailable", pin);
    return std::nullopt;
  }

  auto it = requests_.find(pin);
  if (it == requests_.end()) {
    if (logger) logger->warn("toggle({}) requested for unconfigured pin", pin);
    return std::nullopt;
  }

  gpiod::line::value current = it->second.get_value(pin);
  gpiod::line::value next = (current == gpiod::line::value::ACTIVE)
                                ? gpiod::line::value::INACTIVE
                                : gpiod::line::value::ACTIVE;
  it->second.set_value(pin, next);

  bool new_state = (next == gpiod::line::value::ACTIVE);
  if (logger) logger->info("pin {} toggled to {}", pin, new_state ? "ACTIVE" : "INACTIVE");
  return new_state;
}

std::optional<bool> GpioController::read(int pin) {
  if (!available_) return std::nullopt;

  auto it = requests_.find(pin);
  if (it == requests_.end()) return std::nullopt;

  return it->second.get_value(pin) == gpiod::line::value::ACTIVE;
}

}  // namespace hub
