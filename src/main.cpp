#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

#include "api_server.h"
#include "device_manager.h"
#include "gpio_controller.h"
#include "sqlite_log_sink.h"

namespace {

hub::ApiServer* g_server = nullptr;

void handle_signal(int) {
  if (g_server) g_server->stop();
}

std::vector<spdlog::sink_ptr> build_sinks(const std::string& log_dir,
                                           const std::string& events_db_path) {
  std::vector<spdlog::sink_ptr> sinks;

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  sinks.push_back(console_sink);

  auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      log_dir + "/hub.log", 5 * 1024 * 1024, 3);
  sinks.push_back(file_sink);

  auto sqlite_sink = std::make_shared<hub::SqliteLogSink>(events_db_path);
  sinks.push_back(sqlite_sink);

  return sinks;
}

std::shared_ptr<spdlog::logger> make_component_logger(
    const std::string& name, const std::vector<spdlog::sink_ptr>& sinks) {
  auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
  logger->set_level(spdlog::level::info);
  logger->flush_on(spdlog::level::warn);
  spdlog::register_logger(logger);
  return logger;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string config_path = argc > 1 ? argv[1] : "config/devices.json";
  const std::string data_dir = std::getenv("HUB_DATA_DIR") ? std::getenv("HUB_DATA_DIR") : "data";
  const std::string log_dir = std::getenv("HUB_LOG_DIR") ? std::getenv("HUB_LOG_DIR") : "logs";
  const std::string host = std::getenv("HUB_HOST") ? std::getenv("HUB_HOST") : "0.0.0.0";
  const int port = std::getenv("HUB_PORT") ? std::atoi(std::getenv("HUB_PORT")) : 8080;

  std::filesystem::create_directories(data_dir);
  std::filesystem::create_directories(log_dir);

  const std::string events_db_path = data_dir + "/events.db";

  auto sinks = build_sinks(log_dir, events_db_path);
  make_component_logger("device_manager", sinks);
  make_component_logger("gpio", sinks);
  auto api_logger = make_component_logger("api_server", sinks);
  spdlog::set_default_logger(api_logger);

  try {
    hub::DeviceManager device_manager(config_path);
    hub::GpioController gpio(device_manager.gpio_chip_path(), device_manager.devices());
    hub::ApiServer server(device_manager, gpio, events_db_path);

    g_server = &server;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    api_logger->info("home automation hub starting up (gpio_available={})", gpio.available());
    server.listen(host, port);
    api_logger->info("home automation hub shut down cleanly");
  } catch (const std::exception& e) {
    spdlog::error("fatal startup error: {}", e.what());
    spdlog::shutdown();
    return 1;
  }

  spdlog::shutdown();
  return 0;
}
