#pragma once

#include <gpiod.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "device_manager.h"

namespace hub {

// Wraps libgpiod v2's C++ bindings to request and drive a fixed set of
// output lines. If the chip device can't be opened (e.g. running off the
// target hardware), the controller stays in a disabled state: the rest of
// the service still starts, but toggle()/read() report unavailable.
class GpioController {
 public:
  GpioController(const std::string& chip_path,
                 const std::vector<DeviceConfig>& devices);

  bool available() const { return available_; }

  // Flips the line for `pin`. Returns the new logical state on success.
  std::optional<bool> toggle(int pin);

  // Reads the last known logical state of `pin` without changing it.
  std::optional<bool> read(int pin);

 private:
  bool available_ = false;
  std::unique_ptr<gpiod::chip> chip_;
  std::map<int, gpiod::line_request> requests_;
};

}  // namespace hub
