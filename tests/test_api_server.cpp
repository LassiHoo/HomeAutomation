#include "api_server.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "bme280_sensor.h"
#include "device_manager.h"
#include "gpio_controller.h"
#include "sqlite_log_sink.h"

namespace {

constexpr int kTestPort = 18080;

std::string write_temp_config(const std::string& contents, const char* tag) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string("devices_") + tag + "_" +
               std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".json");
  std::ofstream out(path);
  out << contents;
  return path.string();
}

std::string temp_path(const char* tag, const char* ext) {
  return (std::filesystem::temp_directory_path() /
          (std::string(tag) + "_" +
           std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ext))
      .string();
}

}  // namespace

class ApiServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    config_path_ = write_temp_config(R"({
      "gpio_chip": "/dev/gpiochip0",
      "devices": [
        { "pin": 17, "name": "living_room_light", "component": "lighting" },
        { "pin": 22, "name": "garage_relay", "component": "relay" }
      ]
    })",
                                      "api_server_test");
    db_path_ = temp_path("api_server_events", ".db");

    device_manager_ = std::make_unique<hub::DeviceManager>(config_path_);
    // Nonexistent chip path: GpioController degrades gracefully instead of
    // requiring real hardware for this test.
    gpio_ = std::make_unique<hub::GpioController>("/dev/nonexistent_chip_for_unit_tests",
                                                   device_manager_->devices());
    // Same nonexistent-path pattern for the sensor: degrades gracefully.
    bme280_ = std::make_unique<hub::Bme280Sensor>("/dev/nonexistent_i2c_bus_for_unit_tests",
                                                   0x76);

    // Seed one event row so /events has something to return.
    {
      auto sink = std::make_shared<hub::SqliteLogSink>(db_path_);
      spdlog::logger logger("api_server", sink);
      logger.info("seed event for api server test");
      sink->flush();
    }

    server_ = std::make_unique<hub::ApiServer>(*device_manager_, *gpio_, *bme280_, db_path_);
    server_thread_ = std::thread([this]() { server_->listen("127.0.0.1", kTestPort); });

    client_ = std::make_unique<httplib::Client>("127.0.0.1", kTestPort);
    client_->set_connection_timeout(1, 0);

    httplib::Result res;
    for (int attempt = 0; attempt < 50 && !res; ++attempt) {
      res = client_->Get("/status");
      if (!res) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_TRUE(res) << "server did not come up in time";
  }

  void TearDown() override {
    server_->stop();
    server_thread_.join();
    std::filesystem::remove(config_path_);
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_ + "-wal");
    std::filesystem::remove(db_path_ + "-shm");
  }

  std::string config_path_;
  std::string db_path_;
  std::unique_ptr<hub::DeviceManager> device_manager_;
  std::unique_ptr<hub::GpioController> gpio_;
  std::unique_ptr<hub::Bme280Sensor> bme280_;
  std::unique_ptr<hub::ApiServer> server_;
  std::thread server_thread_;
  std::unique_ptr<httplib::Client> client_;
};

TEST_F(ApiServerTest, StatusReportsConfiguredDevices) {
  auto res = client_->Get("/status");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["status"], "ok");
  EXPECT_EQ(body["gpio_available"], false);
  ASSERT_EQ(body["devices"].size(), 2u);
  EXPECT_EQ(body["devices"][0]["state"], nullptr);
}

TEST_F(ApiServerTest, ToggleUnconfiguredPinReturns404) {
  auto res = client_->Post("/toggle/999");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 404);
}

TEST_F(ApiServerTest, ToggleConfiguredPinReturns503WhenGpioUnavailable) {
  auto res = client_->Post("/toggle/17");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 503);
}

TEST_F(ApiServerTest, EventsReturnsSeededRow) {
  auto res = client_->Get("/events");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 200);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_GE(body["count"].get<int>(), 1);
}

TEST_F(ApiServerTest, EventsComponentFilterExcludesNonMatches) {
  auto res = client_->Get("/events?component=nonexistent_component");
  ASSERT_TRUE(res);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["count"].get<int>(), 0);
}

TEST_F(ApiServerTest, Bme280ReturnsServiceUnavailableWhenSensorMissing) {
  auto res = client_->Get("/sensors/bme280");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->status, 503);

  auto body = nlohmann::json::parse(res->body);
  EXPECT_EQ(body["available"], false);
  EXPECT_TRUE(body["temperature_c"].is_null());
}
