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

#include "drivers/event/event.h"

namespace ThetaGP::Drivers::Event {

class EventManager {
public:
  static EventManager &getInstance() {
    static EventManager instance;
    return instance;
  }

  void registerEvent(Event &evt) {
    if (_count >= MAX_EVENTS)
      return;
    _events[_count++] = &evt;
  }

  void trigger(Event &evt) {
    evt.trigger();
  }

  uint8_t eventCount() const { return _count; }
  Event *getEvent(uint8_t i) const { return _events[i]; }

private:
  EventManager() = default;
  static constexpr uint8_t MAX_EVENTS = 8;
  Event *_events[MAX_EVENTS] = {};
  uint8_t _count = 0;
};

} // namespace ThetaGP::Drivers::Event
