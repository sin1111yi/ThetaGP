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

#include "drivers/event/CompositeEvent.h"

namespace ThetaGP::Drivers::Event {

CompositeEvent::CompositeEvent(Event **events, uint8_t count, CompositeEvent::Mode mode)
    : _events(events), _count(count), _mode(mode) {}

bool CompositeEvent::isTriggered() {
  if (_mode == OR) {
    for (uint8_t i = 0; i < _count; i++) {
      if (_events[i]->isTriggered())
        return true;
    }
    return false;
  }
  for (uint8_t i = 0; i < _count; i++) {
    if (!_events[i]->isTriggered())
      return false;
  }
  return true;
}

void CompositeEvent::clear() {
  for (uint8_t i = 0; i < _count; i++) {
    _events[i]->clear();
  }
}

} // namespace ThetaGP::Drivers::Event
