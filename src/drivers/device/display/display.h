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
#include "drivers/event/event.h"

namespace ThetaGP::Drivers::Device {

class Display : public Device {
public:
  static Display &getInstance() {
    static Display instance;
    return instance;
  }

  void init() override;
  void update();
  void requestUpdate();
  void registerEvent(Event::Event *evt);

  DisplayDriver *getDriver() { return _driver; }

private:
  Display();
  DisplayDriver *_driver = nullptr;

  static constexpr uint8_t MAX_EVENTS = 4;
  Event::Event *_events[MAX_EVENTS] = {};
  uint8_t _eventCount = 0;
  bool _pendingUpdate = false;
};

} // namespace ThetaGP::Drivers::Device
