#include "sqlite_log_sink.h"

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace hub {

namespace {

std::string format_timestamp(spdlog::log_clock::time_point tp) {
  using namespace std::chrono;
  std::time_t tt = spdlog::log_clock::to_time_t(tp);
  auto ms = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;

  std::tm tm_buf{};
  localtime_r(&tt, &tm_buf);

  std::ostringstream oss;
  oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
      << std::setw(3) << ms.count();
  return oss.str();
}

void exec_or_throw(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "unknown sqlite error";
    sqlite3_free(err);
    throw std::runtime_error("sqlite_log_sink: " + msg);
  }
}

}  // namespace

SqliteLogSink::SqliteLogSink(const std::string& db_path) {
  if (sqlite3_open_v2(db_path.c_str(), &db_,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                       nullptr) != SQLITE_OK) {
    std::string msg = db_ ? sqlite3_errmsg(db_) : "failed to open database";
    if (db_) sqlite3_close(db_);
    throw std::runtime_error("sqlite_log_sink: could not open " + db_path + ": " + msg);
  }

  exec_or_throw(db_, "PRAGMA journal_mode=WAL;");
  exec_or_throw(db_, "PRAGMA synchronous=NORMAL;");
  exec_or_throw(db_,
                "CREATE TABLE IF NOT EXISTS events ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  timestamp TEXT NOT NULL,"
                "  level TEXT NOT NULL,"
                "  component TEXT NOT NULL,"
                "  message TEXT NOT NULL"
                ");");
  exec_or_throw(db_, "CREATE INDEX IF NOT EXISTS idx_events_level ON events(level);");
  exec_or_throw(db_, "CREATE INDEX IF NOT EXISTS idx_events_component ON events(component);");

  static constexpr char kInsertSql[] =
      "INSERT INTO events (timestamp, level, component, message) VALUES (?, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, kInsertSql, -1, &insert_stmt_, nullptr) != SQLITE_OK) {
    std::string msg = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    throw std::runtime_error("sqlite_log_sink: failed to prepare insert statement: " + msg);
  }
}

SqliteLogSink::~SqliteLogSink() {
  if (insert_stmt_) sqlite3_finalize(insert_stmt_);
  if (db_) sqlite3_close(db_);
}

void SqliteLogSink::sink_it_(const spdlog::details::log_msg& msg) {
  const std::string timestamp = format_timestamp(msg.time);
  const auto level = spdlog::level::to_string_view(msg.level);
  const std::string component = msg.logger_name.size() == 0
                                     ? std::string("hub")
                                     : std::string(msg.logger_name.begin(), msg.logger_name.end());
  const std::string message(msg.payload.begin(), msg.payload.end());

  sqlite3_reset(insert_stmt_);
  sqlite3_clear_bindings(insert_stmt_);

  sqlite3_bind_text(insert_stmt_, 1, timestamp.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_stmt_, 2, level.data(), static_cast<int>(level.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_stmt_, 3, component.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insert_stmt_, 4, message.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(insert_stmt_) != SQLITE_DONE) {
    // Avoid throwing out of a logging call; the console/file sinks still
    // capture the message even if this insert failed.
    fprintf(stderr, "sqlite_log_sink: insert failed: %s\n", sqlite3_errmsg(db_));
  }
}

void SqliteLogSink::flush_() {
  // WAL + synchronous=NORMAL already commits each statement; nothing to do.
}

}  // namespace hub
