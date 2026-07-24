#pragma once

#include <mutex>
#include <sqlite3.h>
#include <spdlog/sinks/base_sink.h>
#include <string>

namespace hub {

// spdlog sink that writes every log record into an `events` table in a
// SQLite database, in WAL mode, via a single prepared statement.
//
// Thread-safety comes from spdlog::sinks::base_sink<std::mutex>: the base
// class takes its mutex around every call to sink_it_()/flush_(), so this
// class itself does not need to do its own locking.
class SqliteLogSink : public spdlog::sinks::base_sink<std::mutex> {
 public:
  explicit SqliteLogSink(const std::string& db_path);
  ~SqliteLogSink() override;

  SqliteLogSink(const SqliteLogSink&) = delete;
  SqliteLogSink& operator=(const SqliteLogSink&) = delete;

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override;
  void flush_() override;

 private:
  sqlite3* db_ = nullptr;
  sqlite3_stmt* insert_stmt_ = nullptr;
};

}  // namespace hub
