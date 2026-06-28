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

#include "drivers/event/IEventPort.h"

#include <cstdint>

namespace ThetaGP::Drivers::Event {

template<typename E, uint8_t N>
class EventPort : public IEventPort {
  volatile uint8_t _flags[N] = {};

public:
  void trigger(E e) { _flags[static_cast<uint8_t>(e)] = 1; }
  bool isTriggered(E e) const { return _flags[static_cast<uint8_t>(e)] != 0; }
  void clear(E e) { _flags[static_cast<uint8_t>(e)] = 0; }

  template<E... events>
  bool anyOf() const { return (isTriggered(events) || ...); }

  template<E... events>
  bool allOf() const { return (isTriggered(events) && ...); }

  template<E... events>
  void clearOf() { (clear(events), ...); }

  bool anyTriggered() override {
    for (uint8_t i = 0; i < N; i++)
      if (_flags[i]) return true;
    return false;
  }

  void clearAll() override {
    for (uint8_t i = 0; i < N; i++)
      _flags[i] = 0;
  }

  uint8_t count() const { return N; }
};

} // namespace ThetaGP::Drivers::Event
