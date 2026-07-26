#include "sqlite_log_sink.h"

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>

namespace {

std::string temp_db_path(const char* tag) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string("events_test_") + tag + "_" +
               std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".db");
  std::filesystem::remove(path);
  return path.string();
}

}  // namespace

TEST(SqliteLogSink, CreatesSchemaAndEnablesWal) {
  auto path = temp_db_path("schema");
  { hub::SqliteLogSink sink(path); }

  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);

  sqlite3_stmt* stmt = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, nullptr), SQLITE_OK);
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  std::string mode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  EXPECT_EQ(mode, "wal");
  sqlite3_finalize(stmt);

  ASSERT_EQ(sqlite3_prepare_v2(
                db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='events';",
                -1, &stmt, nullptr),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  EXPECT_EQ(sqlite3_column_int(stmt, 0), 1);
  sqlite3_finalize(stmt);

  sqlite3_close(db);
  std::filesystem::remove(path);
}

TEST(SqliteLogSink, WritesLogRecordsAsRows) {
  auto path = temp_db_path("insert");
  auto sink = std::make_shared<hub::SqliteLogSink>(path);
  spdlog::logger logger("gpio_test", sink);
  logger.set_level(spdlog::level::info);

  logger.info("test message {}", 42);
  sink->flush();

  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);

  sqlite3_stmt* stmt = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(
                db, "SELECT level, component, message FROM events ORDER BY id DESC LIMIT 1;", -1,
                &stmt, nullptr),
            SQLITE_OK);
  ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
  EXPECT_STREQ(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), "info");
  EXPECT_STREQ(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)), "gpio_test");
  EXPECT_STREQ(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)), "test message 42");
  sqlite3_finalize(stmt);

  sqlite3_close(db);
  std::filesystem::remove(path);
}
