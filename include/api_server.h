#pragma once

#include <httplib.h>
#include <sqlite3.h>

#include <memory>
#include <string>

#include "bme280_sensor.h"
#include "device_manager.h"
#include "gpio_controller.h"

namespace hub {

// REST API surface for the hub: /status, /toggle/{pin}, /events, /sensors/bme280.
class ApiServer {
 public:
  ApiServer(DeviceManager& device_manager, GpioController& gpio, Bme280Sensor& bme280,
            const std::string& events_db_path);
  ~ApiServer();

  ApiServer(const ApiServer&) = delete;
  ApiServer& operator=(const ApiServer&) = delete;

  // Blocks serving requests until stop() is called from another thread/signal.
  void listen(const std::string& host, int port);
  void stop();

 private:
  void handle_status(const httplib::Request& req, httplib::Response& res);
  void handle_toggle(const httplib::Request& req, httplib::Response& res);
  void handle_events(const httplib::Request& req, httplib::Response& res);
  void handle_bme280(const httplib::Request& req, httplib::Response& res);

  DeviceManager& device_manager_;
  GpioController& gpio_;
  Bme280Sensor& bme280_;
  sqlite3* events_db_ = nullptr;
  httplib::Server server_;
};

}  // namespace hub
