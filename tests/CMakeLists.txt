include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(hub_tests
  test_device_manager.cpp
  test_gpio_controller.cpp
  test_bme280_sensor.cpp
  test_sqlite_log_sink.cpp
  test_api_server.cpp
)

target_link_libraries(hub_tests PRIVATE hub_core GTest::gtest_main)

gtest_discover_tests(hub_tests)
