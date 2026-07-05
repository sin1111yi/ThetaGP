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

#include "utils/log/log.h"
#include "utils/utils.h"
#include "build_info.h"

#include "gamepad/gamepad.h"
#include "taskmanager.h"

#include "ThetaGP.h"

#include "test/framelayer.h"

using namespace ThetaGP;
using namespace ThetaGP::Gamepad;

FAST_CODE static void taskGamepadCore(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  Gamepad::Gamepad::getInstance().process();
  ThetaGP::Test::FrameLayer::getInstance().flushTx();
  tud_task();
}

FAST_CODE static void taskCmdProc(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);
  ThetaGP::Test::FrameLayer::getInstance().processCommandQueue();
}

void ThetaGP::ThetaGamepad::registerTasks(void) {
  TaskManager::registerTask("GAMEPAD", "CORE", taskGamepadCore,
                            TASK_PERIOD_HZ(1000), TaskPriority::Realtime);
  TaskManager::registerTask("TEST", "CMD_PROC", taskCmdProc,
                            TASK_PERIOD_HZ(20), TaskPriority::Medium);
}