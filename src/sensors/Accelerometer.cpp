#include "sensors/Accelerometer.h"

#include "board/BoardImu.h"

namespace {

constexpr uint8_t kImuWhoAmIReg = 0x00;
constexpr uint8_t kImuCtrl1Reg = 0x02;
constexpr uint8_t kImuCtrl2Reg = 0x03;
constexpr uint8_t kImuCtrl5Reg = 0x06;
constexpr uint8_t kImuCtrl7Reg = 0x08;
constexpr uint8_t kImuCtrl8Reg = 0x09;
constexpr uint8_t kImuAccelStartReg = 0x35;
constexpr uint8_t kImuResetReg = 0x60;
constexpr uint8_t kImuResetValue = 0xB0;
constexpr uint8_t kImuResetResultReg = 0x4D;
constexpr uint8_t kImuResetResultValue = 0x80;
constexpr uint8_t kImuWhoAmIValue = 0x05;

}  // namespace

bool Accelerometer::begin() {
  if (!Board::Imu::available()) {
    imuAvailable_ = false;
    Serial.println("[accel] IMU unavailable for this board profile");
    return false;
  }

  if (imuAvailable_) {
    return true;
  }

  const uint8_t candidateAddresses[] = {
      Board::Imu::address(),
      0x6B,
      0x6A,
  };
  bool sawRespondingAddress = false;

  for (uint8_t i = 0; i < sizeof(candidateAddresses); ++i) {
    const uint8_t candidateAddress = candidateAddresses[i];
    bool alreadyTried = false;
    for (uint8_t j = 0; j < i; ++j) {
      if (candidateAddresses[j] == candidateAddress) {
        alreadyTried = true;
        break;
      }
    }
    if (alreadyTried) {
      continue;
    }

    if (!Board::Imu::probeAddress(candidateAddress)) {
      continue;
    }
    sawRespondingAddress = true;
    imuAddress_ = candidateAddress;

    uint8_t whoAmI = 0;
    if (!Board::Imu::readRegister(imuAddress_, kImuWhoAmIReg, whoAmI) ||
        whoAmI != kImuWhoAmIValue) {
      Serial.printf("[accel] QMI8658 WHOAMI mismatch addr=0x%02X got=0x%02X expected=0x%02X\n",
                    candidateAddress, whoAmI, kImuWhoAmIValue);
      continue;
    }

    if (!Board::Imu::writeRegister(imuAddress_, kImuResetReg, kImuResetValue)) {
      Serial.printf("[accel] QMI8658 reset command failed addr=0x%02X\n", candidateAddress);
      continue;
    }

    const uint32_t waitStartedMs = millis();
    uint8_t resetResult = 0;
    bool resetReady = false;
    while (millis() - waitStartedMs < 500) {
      if (Board::Imu::readRegister(imuAddress_, kImuResetResultReg, resetResult) &&
          resetResult == kImuResetResultValue) {
        resetReady = true;
        break;
      }
      delay(10);
    }

    if (!resetReady) {
      Serial.printf("[accel] QMI8658 reset timeout addr=0x%02X last=0x%02X\n",
                    candidateAddress, resetResult);
      continue;
    }

    whoAmI = 0;
    if (!Board::Imu::readRegister(imuAddress_, kImuWhoAmIReg, whoAmI) ||
        whoAmI != kImuWhoAmIValue) {
      Serial.printf("[accel] QMI8658 WHOAMI mismatch after reset addr=0x%02X got=0x%02X "
                    "expected=0x%02X\n",
                    candidateAddress, whoAmI, kImuWhoAmIValue);
      continue;
    }

    if (!updateRegister(kImuCtrl1Reg, 0x40, 0x40) ||
        !Board::Imu::writeRegister(imuAddress_, kImuCtrl8Reg, 0x80) ||
        !Board::Imu::writeRegister(imuAddress_, kImuCtrl2Reg, 0x16) ||
        !updateRegister(kImuCtrl5Reg, 0x07, 0x07) ||
        !updateRegister(kImuCtrl7Reg, 0x01, 0x01)) {
      Serial.printf("[accel] QMI8658 configuration failed addr=0x%02X\n", candidateAddress);
      continue;
    }

    accelScale_ = 4.0f / 32768.0f;
    imuAvailable_ = true;
    Serial.printf("[accel] QMI8658 initialized addr=0x%02X bus=%s\n", imuAddress_,
                  Board::Imu::wireName());
    return true;
  }

  imuAvailable_ = false;
  if (sawRespondingAddress) {
    Serial.printf("[accel] QMI8658 init failed bus=%s configured=0x%02X\n",
                  Board::Imu::wireName(), Board::Imu::address());
  } else {
    Serial.printf("[accel] QMI8658 not responding bus=%s configured=0x%02X fallback=0x6A\n",
                  Board::Imu::wireName(), Board::Imu::address());
  }
  return false;
}

bool Accelerometer::available() const { return imuAvailable_; }

bool Accelerometer::updateRegister(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current = 0;
  if (!Board::Imu::readRegister(imuAddress_, reg, current)) {
    return false;
  }

  current = static_cast<uint8_t>((current & static_cast<uint8_t>(~mask)) | (value & mask));
  return Board::Imu::writeRegister(imuAddress_, reg, current);
}

bool Accelerometer::read(float &x, float &y, float &z) {
  if (!imuAvailable_) {
    return false;
  }

  uint8_t buffer[6] = {0};
  if (!Board::Imu::readRegisters(imuAddress_, kImuAccelStartReg, buffer, sizeof(buffer))) {
    return false;
  }

  const int16_t rawX = static_cast<int16_t>((buffer[1] << 8) | buffer[0]);
  const int16_t rawY = static_cast<int16_t>((buffer[3] << 8) | buffer[2]);
  const int16_t rawZ = static_cast<int16_t>((buffer[5] << 8) | buffer[4]);

  x = rawX * accelScale_;
  y = rawY * accelScale_;
  z = rawZ * accelScale_;
  return true;
}
