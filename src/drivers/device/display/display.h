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

#include "BoardConfig.h"
#include "drivers/device/device.h"
#include "drivers/device/display/driver.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/gpio.h"

namespace ThetaGP::Drivers::Device {

class Display : public Device {
public:
  static Display &getInstance() {
    static Display instance;
    return instance;
  }

  void init() override;
  void update();
  DisplayDrv::DisplayDriver *getDriver() { return _driver; }

private:
  Display();
  DisplayDrv::DisplayDriver *_driver = nullptr;
  Peripheral::BUS::SpiBus *_spiBus = nullptr;
};

} // namespace ThetaGP::Drivers::Device
