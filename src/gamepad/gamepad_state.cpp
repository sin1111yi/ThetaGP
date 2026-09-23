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

#include <cstddef>

#include "gamepad/gamepad_state.h"

namespace ThetaGP::Gamepad {

using namespace Enums;

uint16_t dpadToAnalogX(uint8_t dpad) {
  switch (dpad & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT)) {
  case GAMEPAD_MASK_LEFT:
    return GAMEPAD_JOYSTICK_MIN;

  case GAMEPAD_MASK_RIGHT:
    return GAMEPAD_JOYSTICK_MAX;

  default:
    return GAMEPAD_JOYSTICK_MID;
  }
}

uint16_t dpadToAnalogY(uint8_t dpad) {
  switch (dpad & (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN)) {
  case GAMEPAD_MASK_UP:
    return GAMEPAD_JOYSTICK_MIN;

  case GAMEPAD_MASK_DOWN:
    return GAMEPAD_JOYSTICK_MAX;

  default:
    return GAMEPAD_JOYSTICK_MID;
  }
}

uint8_t getMaskFromDirection(DpadDirection direction) {
  return dpadMasks[static_cast<size_t>(direction) - 1];
}

uint8_t updateDpad(DpadState &s, uint8_t dpad, DpadDirection direction) {
  const auto mask = getMaskFromDirection(direction);
  auto &order = s.order;
  auto &count = s.count;

  if (dpad & mask) {
    // Pressed — append to order if not already present
    bool found = false;
    for (uint8_t i = 0; i < count; i++) {
      if (order[i] == direction) {
        found = true;
        break;
      }
    }
    if (!found && count < 4) {
      order[count++] = direction;
    }
  } else {
    // Released — remove from order
    for (uint8_t i = 0; i < count; i++) {
      if (order[i] == direction) {
        for (uint8_t j = i; j < count - 1; j++) {
          order[j] = order[j + 1];
        }
        order[--count] = DpadDirection::None;
        break;
      }
    }
  }

  return (count > 0) ? getMaskFromDirection(order[count - 1]) : 0;
}

uint8_t filterToFourWayMode(DpadState &s, uint8_t dpad) {
  updateDpad(s, dpad, DpadDirection::Up);
  updateDpad(s, dpad, DpadDirection::Down);
  updateDpad(s, dpad, DpadDirection::Left);
  return updateDpad(s, dpad, DpadDirection::Right);
}

uint8_t runSOCDCleaner(DpadState &s, SOCDMode mode, uint8_t dpad) {
  if (mode == SOCDMode::Bypass) {
    return dpad;
  }

  uint8_t newDpad = 0;

  switch (dpad & (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN)) {
  case (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN):
    if (mode == SOCDMode::UpPriority) {
      newDpad |= GAMEPAD_MASK_UP;
      s.lastUD = DpadDirection::Up;
    } else if (mode == SOCDMode::SecondInputPriority &&
               s.lastUD != DpadDirection::None)
      newDpad |=
          (s.lastUD == DpadDirection::Up) ? GAMEPAD_MASK_DOWN : GAMEPAD_MASK_UP;
    else if (mode == SOCDMode::FirstInputPriority &&
             s.lastUD != DpadDirection::None)
      newDpad |=
          (s.lastUD == DpadDirection::Up) ? GAMEPAD_MASK_UP : GAMEPAD_MASK_DOWN;
    else
      s.lastUD = DpadDirection::None;
    break;

  case GAMEPAD_MASK_UP:
    newDpad |= GAMEPAD_MASK_UP;
    s.lastUD = DpadDirection::Up;
    break;

  case GAMEPAD_MASK_DOWN:
    newDpad |= GAMEPAD_MASK_DOWN;
    s.lastUD = DpadDirection::Down;
    break;

  default:
    s.lastUD = DpadDirection::None;
    break;
  }

  switch (dpad & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT)) {
  case (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT):
    if (mode == SOCDMode::SecondInputPriority &&
        s.lastLR != DpadDirection::None)
      newDpad |= (s.lastLR == DpadDirection::Left) ? GAMEPAD_MASK_RIGHT
                                                   : GAMEPAD_MASK_LEFT;
    else if (mode == SOCDMode::FirstInputPriority &&
             s.lastLR != DpadDirection::None)
      newDpad |= (s.lastLR == DpadDirection::Left) ? GAMEPAD_MASK_LEFT
                                                   : GAMEPAD_MASK_RIGHT;
    else
      s.lastLR = DpadDirection::None;
    break;

  case GAMEPAD_MASK_LEFT:
    newDpad |= GAMEPAD_MASK_LEFT;
    s.lastLR = DpadDirection::Left;
    break;

  case GAMEPAD_MASK_RIGHT:
    newDpad |= GAMEPAD_MASK_RIGHT;
    s.lastLR = DpadDirection::Right;
    break;

  default:
    s.lastLR = DpadDirection::None;
    break;
  }

  return newDpad;
}

} // namespace ThetaGP::Gamepad
