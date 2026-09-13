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

#include "build_info.h"
#include "drivers/device/device.h"
#include "drivers/peripherals/peripheralsmgr.h"

#include <cstdint>

namespace ThetaGP::Drivers::Device {

class Logger : public Device {
public:
  static Logger &getInstance() {
    static Logger instance;
    return instance;
  }

  void init() override;
  static void LoggerTransmitBytes(uint8_t *data, uint16_t n);

private:
  Logger();
  Peripheral::BUS::UartBus &_uart;
};

} // namespace ThetaGP::Drivers::Device
