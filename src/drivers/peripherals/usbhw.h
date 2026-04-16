/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "utils/types.h"

#include "drivers/peripherals/gpio.h"

#include "build_info.h"

#include <cstdint>

namespace ThetaGP::Drivers::Peripheral {

using namespace GPIO;

namespace USB {

enum class USBSpeed : uint8_t {
  UsbFullSpeed,
  UsbHighSpeedInternalPHY,
  UsbHighSpeedExternalPHY,
};

enum class USBPeripheral : uint8_t {
  DifferenceLine,
  ULPI,
};

enum class ULPI : uint8_t {
  CLK, // ULPI clock
  STP, // ULPI strobe
  DIR, // ULPI direction
  NXT, // ULPI next
  D0,  // ULPI data bit 0
  D1,  // ULPI data bit 1
  D2,  // ULPI data bit 2
  D3,  // ULPI data bit 3
  D4,  // ULPI data bit 4
  D5,  // ULPI data bit 5
  D6,  // ULPI data bit 6
  D7,  // ULPI data bit 7
};

class HardwareUSB {
private:
  bool _initialized;
  USBSpeed _speed;
  USBPeripheral _peripheral;

  void enableClock() const;
  RetVal initPCD();
  void initULPIPins();
  void initHighSpeedPins();
  void initFullSpeedPins();

public:
  HardwareUSB(USBSpeed speed, USBPeripheral peripheral);

  RetVal init();
  bool isInitialized() const { return _initialized; }
};

} // namespace USB
} // namespace ThetaGP::Drivers::Peripheral
