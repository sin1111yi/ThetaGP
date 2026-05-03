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

#include "device/usbd.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include "gamepad/gamepad.h"
#include "utils/log/log.h"
#include "utils/utils.h"

#include "taskmanager.h"

#include "ThetaGP.h"

#include <cstdint>

using namespace ThetaGP;
using namespace ThetaGP::Gamepad;

static void taskGamepadCore(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  Gamepad::Gamepad::getInstance().process();
  tud_task();
}

void ThetaGP::ThetaGamepad::registerTasks(void) {
  TaskManager::registerTask("GAMEPAD", "CORE", taskGamepadCore,
                            TASK_PERIOD_HZ(1000), TaskPriority::Realtime);
}