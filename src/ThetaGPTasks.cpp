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
#include "drivers/device/flash/flash_w25qxx.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include "gamepad/gamepad.h"
#include "utils/log/log.h"
#include "utils/utils.h"

#include "taskmanager.h"

#include "ThetaGP.h"

#include <ArduinoJson.h>
#include <cstdint>

using namespace ThetaGP;
using namespace ThetaGP::Gamepad;

static void taskGamepadCore(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  Gamepad::Gamepad::getInstance().process();
  tud_task();
}

static void taskFlashTest(uint32_t currentTimeUs) {
  UNUSED(currentTimeUs);

  auto &flash = Drivers::Device::W25qxxFlash::getInstance();
  if (!flash.isInitialized()) {
    LOG_WARN("FLASH: not initialized");
    return;
  }

  uint32_t jedecId = flash.readId();
  const auto &info = flash.getInfo();

  LOG_INFO("FLASH: JEDEC ID = 0x%06lX, size = %lu bytes",
           (unsigned long)jedecId, (unsigned long)info.sizeBytes);
}

void ThetaGP::ThetaGamepad::registerTasks(void) {
  TaskManager::registerTask("GAMEPAD", "CORE", taskGamepadCore,
                            TASK_PERIOD_HZ(1000), TaskPriority::Realtime);
  TaskManager::registerTask("FLASH", "TEST", taskFlashTest,
                            TASK_PERIOD_HZ(1), TaskPriority::Low);
}