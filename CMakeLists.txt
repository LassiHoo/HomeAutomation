cmake_minimum_required(VERSION 3.16)
project(home_automation_hub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# --- Header-only / vendorable deps: pinned via FetchContent so the build is
# reproducible regardless of what the host distro happens to package. ---

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.14.1
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE INTERNAL "")

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(spdlog nlohmann_json httplib)

# --- System deps: these talk to the kernel / filesystem directly, so we use
# whatever the OS provides rather than vendoring. ---

find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiod libgpiodcxx)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

add_executable(hub
  src/main.cpp
  src/device_manager.cpp
  src/gpio_controller.cpp
  src/api_server.cpp
  src/sqlite_log_sink.cpp
)

target_include_directories(hub PRIVATE include)

target_link_libraries(hub PRIVATE
  spdlog::spdlog
  nlohmann_json::nlohmann_json
  httplib::httplib
  PkgConfig::GPIOD
  SQLite::SQLite3
  Threads::Threads
)

install(TARGETS hub RUNTIME DESTINATION bin)
