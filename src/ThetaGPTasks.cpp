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

#include "BoardConfig.h"
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_USB_REPORT_RATE_HZ, the gamepad task rate this file registers

#include "gamepad/gamepad.h"
#include "taskmanager.h"

#include "ThetaGP.h"

#include "test/framelayer.h"

using namespace ThetaGP;
using namespace ThetaGP::Gamepad;

FAST_CODE static void taskGamepadCore(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  // tud_task first: consume pending USB events and free the HID IN
  // endpoint before submitting the next report. Calling it after
  // process() drops ~2/3 of a 1kHz report stream (single-buffer HID:
  // a report submitted while the endpoint is still busy is discarded).
  tud_task();
  Gamepad::Gamepad::getInstance().process();
  // Drains the response bytes sendResponse() laid down: those reach the
  // stack's TX FIFO here, on the report tick.
  ThetaGP::Test::FrameLayer::getInstance().flushTx();
}

FAST_CODE static void taskCmdProc(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  // The 20 Hz command tick: decode the frames the host sent and dispatch the
  // queued ones synchronously. Responses built through sendResponse() are
  // queued for the gamepad tick's flushTx(); a command that writes the CDC
  // FIFO itself (profile.get: header and raw payload) gets its bytes out
  // during this dispatch, while its trailer goes through sendResponse().
  ThetaGP::Test::FrameLayer::getInstance().processCommandQueue();
}

void ThetaGP::ThetaGamepad::registerTasks(void) {
  TaskManager::registerTask("GAMEPAD", "CORE", taskGamepadCore,
                            TASK_PERIOD_HZ(THETAGP_CFG_USB_REPORT_RATE_HZ),
                            TaskPriority::Realtime);
  TaskManager::registerTask("TEST", "CMD_PROC", taskCmdProc,
                            TASK_PERIOD_HZ(20), TaskPriority::Medium);
}