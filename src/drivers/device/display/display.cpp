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

#include "drivers/device/display/display.h"

#include <new>

#if defined(DISPLAY_SPI)
#include "drivers/device/display/drivers/nv3007.h"
#include "drivers/peripherals/peripheralsmgr.h"

namespace ThetaGP::Drivers::Device {

using namespace Peripheral::GPIO;

Display::Display()
    : Device("display") {
}

void Display::init() {
  // Get SPI bus from PeripheralsManager
  _spiBus = &Peripheral::PeripheralsManager::getInstance().spiBus(DISPLAY_SPI);

  // Create Nv3007 driver
  _driver = new DisplayDrv::Nv3007(
      *_spiBus,
      PinDesc DISPLAY_DC_PIN,
      PinDesc DISPLAY_RES_PIN,
      PinDesc DISPLAY_BLK_PIN
  );

  _driver->init();
  _initialized = true;
}

void Display::update() {
  if (!_driver)
    return;
}

} // namespace ThetaGP::Drivers::Device
#endif // DISPLAY_SPI
