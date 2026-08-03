#include "bme280_sensor.h"

#include <gtest/gtest.h>

TEST(Bme280Sensor, DegradesGracefullyWhenBusMissing) {
  hub::Bme280Sensor sensor("/dev/nonexistent_i2c_bus_for_unit_tests", 0x76);

  EXPECT_FALSE(sensor.available());
  EXPECT_FALSE(sensor.read().has_value());
}

// The remaining tests exercise Bme280Sensor::compensate() directly — the
// pure Bosch datasheet math, independent of any I2C hardware — using
// contrived calibration coefficients chosen to isolate specific behaviors
// rather than trying to reproduce an exact real-sensor reference reading.

TEST(Bme280Compensate, TemperatureIncreasesMonotonicallyWithRawAdcValue) {
  hub::Bme280Calibration calib;
  calib.dig_T1 = 27504;
  calib.dig_T2 = 26435;
  calib.dig_T3 = 0;  // isolate the monotonic linear term
  calib.dig_P1 = 36477;

  auto cool = hub::Bme280Sensor::compensate(calib, /*adc_temperature=*/400000, 400000, 0);
  auto warm = hub::Bme280Sensor::compensate(calib, /*adc_temperature=*/520000, 400000, 0);

  EXPECT_LT(cool.temperature_c, warm.temperature_c);
}

TEST(Bme280Compensate, HumidityClampsToUpperBound) {
  hub::Bme280Calibration calib;
  calib.dig_T1 = 27504;
  calib.dig_T2 = 26435;
  calib.dig_P1 = 36477;
  // Zeroing H1/H3/H4/H5/H6 collapses the formula to
  // humidity = adc_H * dig_H2 / 65536, which we can push arbitrarily high.
  calib.dig_H2 = 32767;

  auto reading = hub::Bme280Sensor::compensate(calib, 500000, 400000, /*adc_humidity=*/65535);

  EXPECT_DOUBLE_EQ(reading.humidity_percent, 100.0);
}

TEST(Bme280Compensate, HumidityClampsToLowerBound) {
  hub::Bme280Calibration calib;
  calib.dig_T1 = 27504;
  calib.dig_T2 = 26435;
  calib.dig_P1 = 36477;
  calib.dig_H2 = -32768;  // same collapsed formula, driven negative instead

  auto reading = hub::Bme280Sensor::compensate(calib, 500000, 400000, /*adc_humidity=*/65535);

  EXPECT_DOUBLE_EQ(reading.humidity_percent, 0.0);
}

TEST(Bme280Compensate, IsDeterministic) {
  hub::Bme280Calibration calib;
  calib.dig_T1 = 27504;
  calib.dig_T2 = 26435;
  calib.dig_T3 = -1000;
  calib.dig_P1 = 36477;
  calib.dig_P2 = -10685;
  calib.dig_P3 = 3024;
  calib.dig_H1 = 75;
  calib.dig_H2 = 382;

  auto first = hub::Bme280Sensor::compensate(calib, 519888, 415148, 32398);
  auto second = hub::Bme280Sensor::compensate(calib, 519888, 415148, 32398);

  EXPECT_DOUBLE_EQ(first.temperature_c, second.temperature_c);
  EXPECT_DOUBLE_EQ(first.humidity_percent, second.humidity_percent);
  EXPECT_DOUBLE_EQ(first.pressure_hpa, second.pressure_hpa);
}

TEST(Bme280Compensate, PressureStaysWithinSensorOperatingRange) {
  // Typical-magnitude coefficients; the BME280 datasheet specifies a
  // 300-1100 hPa operating range, which any sane compensation result for a
  // mid-scale raw reading should fall within.
  hub::Bme280Calibration calib;
  calib.dig_T1 = 27504;
  calib.dig_T2 = 26435;
  calib.dig_T3 = -1000;
  calib.dig_P1 = 36477;
  calib.dig_P2 = -10685;
  calib.dig_P3 = 3024;
  calib.dig_P4 = 2855;
  calib.dig_P5 = 140;
  calib.dig_P6 = -7;
  calib.dig_P7 = 15500;
  calib.dig_P8 = -14600;
  calib.dig_P9 = 6000;

  auto reading = hub::Bme280Sensor::compensate(calib, 519888, 415148, 32398);

  EXPECT_GE(reading.pressure_hpa, 300.0);
  EXPECT_LE(reading.pressure_hpa, 1100.0);
}

TEST(Bme280Compensate, ZeroPressureCalibrationYieldsZeroPressure) {
  // dig_P1 == 0 hits the explicit divide-by-zero guard in the datasheet
  // formula rather than actually dividing by zero.
  hub::Bme280Calibration calib;
  calib.dig_T1 = 27504;
  calib.dig_T2 = 26435;
  calib.dig_P1 = 0;

  auto reading = hub::Bme280Sensor::compensate(calib, 519888, 415148, 0);

  EXPECT_DOUBLE_EQ(reading.pressure_hpa, 0.0);
}
