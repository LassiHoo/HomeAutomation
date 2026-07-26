#include "gpio_controller.h"

#include <gtest/gtest.h>

TEST(GpioController, DegradesGracefullyWhenChipMissing) {
  std::vector<hub::DeviceConfig> devices{
      {17, "living_room_light", "lighting"},
      {22, "garage_relay", "relay"},
  };

  hub::GpioController gpio("/dev/nonexistent_chip_for_unit_tests", devices);

  EXPECT_FALSE(gpio.available());
  EXPECT_FALSE(gpio.toggle(17).has_value());
  EXPECT_FALSE(gpio.read(17).has_value());
}

TEST(GpioController, ToggleAndReadReturnNulloptForUnconfiguredPin) {
  std::vector<hub::DeviceConfig> devices{{17, "living_room_light", "lighting"}};

  hub::GpioController gpio("/dev/nonexistent_chip_for_unit_tests", devices);

  EXPECT_FALSE(gpio.toggle(999).has_value());
  EXPECT_FALSE(gpio.read(999).has_value());
}
