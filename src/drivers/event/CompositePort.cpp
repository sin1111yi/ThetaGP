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

#include "drivers/event/CompositePort.h"

namespace ThetaGP::Drivers::Event {

CompositePort::CompositePort(IEventPort **ports, uint8_t count, Mode mode)
    : _ports(ports), _count(count), _mode(mode) {}

bool CompositePort::anyTriggered() {
  if (_mode == OR) {
    for (uint8_t i = 0; i < _count; i++)
      if (_ports[i]->anyTriggered()) return true;
    return false;
  }
  for (uint8_t i = 0; i < _count; i++)
    if (!_ports[i]->anyTriggered()) return false;
  return true;
}

void CompositePort::clearAll() {
  for (uint8_t i = 0; i < _count; i++)
    _ports[i]->clearAll();
}

} // namespace ThetaGP::Drivers::Event
