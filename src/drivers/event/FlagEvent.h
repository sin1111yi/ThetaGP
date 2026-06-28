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

class FlagEvent : public Event {
public:
  void trigger() override { _triggered = true; }
  bool isTriggered() override { return _triggered; }
  void clear() override { _triggered = false; }

private:
  volatile bool _triggered = false;
};

} // namespace ThetaGP::Drivers::Event
