#pragma once

#include <Arduino.h>
#include <stdint.h>

// Thin wrapper around the board's QMI8658 IMU giving raw, unfiltered
// accelerometer readings (gravity-vector components in g). Shared by any
// consumer that needs tilt data directly (FocusTimer's device-orientation
// classifier, DigitalRain's fall-direction bias) without duplicating the
// per-board init/register sequence.
class Accelerometer {
 public:
  bool begin();
  bool available() const;
  bool read(float &x, float &y, float &z);

 private:
  bool updateRegister(uint8_t reg, uint8_t mask, uint8_t value);

  bool imuAvailable_ = false;
  uint8_t imuAddress_ = 0;
  float accelScale_ = 4.0f / 32768.0f;
};
