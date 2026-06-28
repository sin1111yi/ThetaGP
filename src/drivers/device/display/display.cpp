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

#ifdef USE_DISPLAY
#include "drivers/device/display/drivers/nv3007.h"
#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/peripheralsmgr.h"

namespace ThetaGP::Drivers::Device {

using namespace Peripheral::GPIO;

Display::Display()
    : Device("display") {
}

void Display::init() {
#ifdef DISPLAY_CHIP_NV3007
  auto &spi = Peripheral::PeripheralsManager::getInstance().spiBus(DISPLAY_SPI);
  _driver = &Nv3007::getInstance(spi,
                                 PinDesc DISPLAY_DC_PIN,
                                 PinDesc DISPLAY_RES_PIN,
                                 PinDesc DISPLAY_BLK_PIN);
#endif
  if (!_driver)
    return;
  _driver->init();
  _initialized = true;
}

void Display::update() {
  if (!_driver)
    return;

  bool triggered = _pendingUpdate;
  for (uint8_t i = 0; i < _eventCount; i++) {
    if (_events[i]->isTriggered()) {
      triggered = true;
      _events[i]->clear();
    }
  }
  if (!triggered)
    return;
  _pendingUpdate = false;

  _driver->flush();
}

void Display::requestUpdate() {
  _pendingUpdate = true;
}

void Display::registerEvent(Event::Event *evt) {
  if (_eventCount >= MAX_EVENTS)
    return;
  _events[_eventCount++] = evt;
}

} // namespace ThetaGP::Drivers::Device
#endif // USE_DISPLAY
