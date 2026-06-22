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

#include "gamepad/gamepadstate.h"

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

namespace {
// Four-way mode: press-order LIFO tracking (max 4 directions)
struct {
  Enums::DpadDirection order[4] = {};
  uint8_t count = 0;
} s_fourWayState;

// SOCD axis-last state
Enums::DpadDirection lastUD = Enums::DpadDirection::None;
Enums::DpadDirection lastLR = Enums::DpadDirection::None;
} // anonymous namespace

uint8_t updateDpad(uint8_t dpad, DpadDirection direction) {
  const auto mask = getMaskFromDirection(direction);
  auto &order = s_fourWayState.order;
  auto &count = s_fourWayState.count;

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

uint8_t filterToFourWayMode(uint8_t dpad) {
  updateDpad(dpad, DpadDirection::Up);
  updateDpad(dpad, DpadDirection::Down);
  updateDpad(dpad, DpadDirection::Left);
  return updateDpad(dpad, DpadDirection::Right);
}

uint8_t runSOCDCleaner(SOCDMode mode, uint8_t dpad) {
  if (mode == SOCDMode::Bypass) {
    return dpad;
  }


  uint8_t newDpad = 0;

  switch (dpad & (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN)) {
  case (GAMEPAD_MASK_UP | GAMEPAD_MASK_DOWN):
    if (mode == SOCDMode::UpPriority) {
      newDpad |= GAMEPAD_MASK_UP;
      lastUD = DpadDirection::Up;
    } else if (mode == SOCDMode::SecondInputPriority &&
               lastUD != DpadDirection::None)
      newDpad |=
          (lastUD == DpadDirection::Up) ? GAMEPAD_MASK_DOWN : GAMEPAD_MASK_UP;
    else if (mode == SOCDMode::FirstInputPriority &&
             lastUD != DpadDirection::None)
      newDpad |=
          (lastUD == DpadDirection::Up) ? GAMEPAD_MASK_UP : GAMEPAD_MASK_DOWN;
    else
      lastUD = DpadDirection::None;
    break;

  case GAMEPAD_MASK_UP:
    newDpad |= GAMEPAD_MASK_UP;
    lastUD = DpadDirection::Up;
    break;

  case GAMEPAD_MASK_DOWN:
    newDpad |= GAMEPAD_MASK_DOWN;
    lastUD = DpadDirection::Down;
    break;

  default:
    lastUD = DpadDirection::None;
    break;
  }

  switch (dpad & (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT)) {
  case (GAMEPAD_MASK_LEFT | GAMEPAD_MASK_RIGHT):
    if (mode == SOCDMode::SecondInputPriority && lastLR != DpadDirection::None)
      newDpad |= (lastLR == DpadDirection::Left) ? GAMEPAD_MASK_RIGHT
                                                 : GAMEPAD_MASK_LEFT;
    else if (mode == SOCDMode::FirstInputPriority &&
             lastLR != DpadDirection::None)
      newDpad |= (lastLR == DpadDirection::Left) ? GAMEPAD_MASK_LEFT
                                                 : GAMEPAD_MASK_RIGHT;
    else
      lastLR = DpadDirection::None;
    break;

  case GAMEPAD_MASK_LEFT:
    newDpad |= GAMEPAD_MASK_LEFT;
    lastLR = DpadDirection::Left;
    break;

  case GAMEPAD_MASK_RIGHT:
    newDpad |= GAMEPAD_MASK_RIGHT;
    lastLR = DpadDirection::Right;
    break;

  default:
    lastLR = DpadDirection::None;
    break;
  }

  return newDpad;
}

}  // namespace ThetaGP::Gamepad
