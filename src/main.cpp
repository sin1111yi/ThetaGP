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

#include "BoardConfig.h"

#include "gamepad/gamepad.h"
#include "gamepad/scheduler/scheduler.h"

#include "drivers/device/keypad.h"

#include "tusb.h"

using namespace ThetaGP;

int main(void) {
  Gamepad::Scheduler &scheduler = Gamepad::Scheduler::getInstance();
  scheduler.setupSystem();

  auto &keypad = Drivers::Device::Keypad::getInstance();
  keypad.init();
  while (!keypad.isInitialized()) {
  }

  // tusb_init(1);

  // auto &gamepad = Gamepad::Gamepad::getInstance();
  // gamepad.setup();
  // gamepad.registerKeypadDevice(keypad);
  // gamepad.setDefaultMappings();

  // scheduler.tasksInit();

  while (1) {
    ;
  }
}
