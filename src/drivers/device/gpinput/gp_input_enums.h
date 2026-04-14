#pragma once

#include <cstdint>

namespace ThetaGP::Drivers::Device {

enum class InputMode : uint8_t {
  Config = 0,
  HID,
  Count
};

}  // namespace ThetaGP::Drivers::Device
