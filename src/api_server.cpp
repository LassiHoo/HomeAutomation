#include "api_server.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>

namespace hub {

using json = nlohmann::json;

namespace {
constexpr int kDefaultEventLimit = 100;
constexpr int kMaxEventLimit = 1000;
}  // namespace

ApiServer::ApiServer(DeviceManager& device_manager, GpioController& gpio, Bme280Sensor& bme280,
                      const std::string& events_db_path)
    : device_manager_(device_manager), gpio_(gpio), bme280_(bme280) {
  if (sqlite3_open_v2(events_db_path.c_str(), &events_db_,
                       SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                       nullptr) != SQLITE_OK) {
    std::string msg = events_db_ ? sqlite3_errmsg(events_db_) : "unknown error";
    if (events_db_) sqlite3_close(events_db_);
    throw std::runtime_error("api_server: could not open events db " + events_db_path +
                              ": " + msg);
  }

  server_.Get("/status", [this](const httplib::Request& req, httplib::Response& res) {
    handle_status(req, res);
  });
  server_.Post(R"(/toggle/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
    handle_toggle(req, res);
  });
  server_.Get("/events", [this](const httplib::Request& req, httplib::Response& res) {
    handle_events(req, res);
  });
  server_.Get("/sensors/bme280", [this](const httplib::Request& req, httplib::Response& res) {
    handle_bme280(req, res);
  });
}

ApiServer::~ApiServer() {
  if (events_db_) sqlite3_close(events_db_);
}

void ApiServer::listen(const std::string& host, int port) {
  auto logger = spdlog::get("api_server");
  if (logger) logger->info("listening on {}:{}", host, port);
  server_.listen(host, port);
}

void ApiServer::stop() { server_.stop(); }

void ApiServer::handle_status(const httplib::Request&, httplib::Response& res) {
  json body;
  body["status"] = "ok";
  body["gpio_available"] = gpio_.available();

  json devices = json::array();
  for (const auto& device : device_manager_.devices()) {
    json d;
    d["pin"] = device.pin;
    d["name"] = device.name;
    d["component"] = device.component;
    auto state = gpio_.read(device.pin);
    d["state"] = state.has_value() ? json(*state) : json(nullptr);
    devices.push_back(std::move(d));
  }
  body["devices"] = std::move(devices);

  res.set_content(body.dump(), "application/json");
}

void ApiServer::handle_toggle(const httplib::Request& req, httplib::Response& res) {
  auto logger = spdlog::get("api_server");
  int pin = std::stoi(req.matches[1].str());

  auto device = device_manager_.find_by_pin(pin);
  if (!device.has_value()) {
    res.status = 404;
    res.set_content(json{{"error", "no device configured on pin " + std::to_string(pin)}}.dump(),
                     "application/json");
    return;
  }

  auto new_state = gpio_.toggle(pin);
  if (!new_state.has_value()) {
    res.status = 503;
    res.set_content(json{{"error", "gpio unavailable"}}.dump(), "application/json");
    return;
  }

  if (logger) {
    logger->info("toggled '{}' (pin {}) -> {}", device->name, pin,
                 *new_state ? "on" : "off");
  }

  json body;
  body["pin"] = pin;
  body["name"] = device->name;
  body["state"] = *new_state;
  res.set_content(body.dump(), "application/json");
}

void ApiServer::handle_events(const httplib::Request& req, httplib::Response& res) {
  auto logger = spdlog::get("api_server");

  std::string level = req.get_param_value("level");
  std::string component = req.get_param_value("component");

  int limit = kDefaultEventLimit;
  if (req.has_param("limit")) {
    try {
      limit = std::stoi(req.get_param_value("limit"));
    } catch (const std::exception&) {
      // keep default on malformed input
    }
  }
  if (limit <= 0) limit = kDefaultEventLimit;
  if (limit > kMaxEventLimit) limit = kMaxEventLimit;

  static constexpr char kQuery[] =
      "SELECT id, timestamp, level, component, message FROM events "
      "WHERE (?1 = '' OR level = ?1) AND (?2 = '' OR component = ?2) "
      "ORDER BY id DESC LIMIT ?3;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(events_db_, kQuery, -1, &stmt, nullptr) != SQLITE_OK) {
    if (logger) logger->error("failed to prepare /events query: {}", sqlite3_errmsg(events_db_));
    res.status = 500;
    res.set_content(json{{"error", "internal error"}}.dump(), "application/json");
    return;
  }

  sqlite3_bind_text(stmt, 1, level.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, component.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 3, limit);

  json events = json::array();
  int rc;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    json e;
    e["id"] = sqlite3_column_int64(stmt, 0);
    e["timestamp"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    e["level"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    e["component"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    e["message"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    events.push_back(std::move(e));
  }
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    if (logger) logger->error("error while reading /events results: {}", sqlite3_errmsg(events_db_));
  }

  json body;
  body["count"] = events.size();
  body["events"] = std::move(events);
  res.set_content(body.dump(), "application/json");
}

void ApiServer::handle_bme280(const httplib::Request&, httplib::Response& res) {
  json body;
  body["available"] = bme280_.available();

  auto reading = bme280_.read();
  if (!reading.has_value()) {
    body["temperature_c"] = nullptr;
    body["humidity_percent"] = nullptr;
    body["pressure_hpa"] = nullptr;
    if (!bme280_.available()) {
      res.status = 503;
    }
    res.set_content(body.dump(), "application/json");
    return;
  }

  body["temperature_c"] = reading->temperature_c;
  body["humidity_percent"] = reading->humidity_percent;
  body["pressure_hpa"] = reading->pressure_hpa;
  res.set_content(body.dump(), "application/json");
}

}  // namespace hub
