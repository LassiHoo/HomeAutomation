#include "device_manager.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

std::string write_temp_config(const std::string& contents) {
  auto path = std::filesystem::temp_directory_path() /
              ("devices_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
               "_" + std::to_string(rand()) + ".json");
  std::ofstream out(path);
  out << contents;
  out.close();
  return path.string();
}

}  // namespace

TEST(DeviceManager, LoadsChipPathAndDevices) {
  auto path = write_temp_config(R"({
    "gpio_chip": "/dev/gpiochip0",
    "devices": [
      { "pin": 17, "name": "living_room_light", "component": "lighting" },
      { "pin": 22, "name": "garage_relay", "component": "relay" }
    ]
  })");

  hub::DeviceManager manager(path);

  EXPECT_EQ(manager.gpio_chip_path(), "/dev/gpiochip0");
  ASSERT_EQ(manager.devices().size(), 2u);
  EXPECT_EQ(manager.devices()[0].pin, 17);
  EXPECT_EQ(manager.devices()[0].name, "living_room_light");
  EXPECT_EQ(manager.devices()[0].component, "lighting");

  std::filesystem::remove(path);
}

TEST(DeviceManager, FindByPinReturnsMatch) {
  auto path = write_temp_config(R"({
    "gpio_chip": "/dev/gpiochip0",
    "devices": [
      { "pin": 27, "name": "porch_light", "component": "lighting" }
    ]
  })");

  hub::DeviceManager manager(path);

  auto found = manager.find_by_pin(27);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->name, "porch_light");

  std::filesystem::remove(path);
}

TEST(DeviceManager, FindByPinReturnsNulloptForUnknownPin) {
  auto path = write_temp_config(R"({
    "gpio_chip": "/dev/gpiochip0",
    "devices": []
  })");

  hub::DeviceManager manager(path);

  EXPECT_FALSE(manager.find_by_pin(99).has_value());

  std::filesystem::remove(path);
}

TEST(DeviceManager, ThrowsOnMissingFile) {
  EXPECT_THROW(hub::DeviceManager("/nonexistent/path/devices.json"), std::runtime_error);
}
