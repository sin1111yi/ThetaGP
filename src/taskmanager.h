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

#include "gamepad/scheduler/scheduler.h"

#include "utils/mempool/mempoolmanager.h"

#include <cstddef>
#include <cstdint>

namespace ThetaGP::Gamepad {

using TID = int;

class TaskManager {
private:
  static constexpr uint32_t MAX_TASKS = 16;
  static constexpr uint32_t TASK_POOL_SIZE = 4096;

  struct TaskRecord {
    Task *task = nullptr;
    TaskAttribute *attribute = nullptr;
    bool inUse = false;
  };

  static Scheduler *scheduler;
  static Mempool::PoolID taskPoolId;

  static uint8_t taskPoolMemory[TASK_POOL_SIZE];

  static TaskRecord records[MAX_TASKS];
  static size_t taskCount;
  static uint16_t averageSystemLoadPercent;

  TaskManager() = delete;

  static void taskSystemLoad(uint32_t currentTimeUs);
  static void taskMain(uint32_t currentTimeUs);

public:
  static Mempool::PoolID getTaskPoolId() { return taskPoolId; }

  static void init();
  static void setupSysTasks();
  static void setupScheduler();
  static void run();

  static TID createTask(const char *name, const char *subName, TaskFunc func,
                        uint32_t periodUs, TaskPriority priority);
  static void registerTask(const char *name, const char *subName, TaskFunc func,
                           uint32_t periodUs, TaskPriority priority);
  static void destroyTask(TID tid);

  static bool isValidTID(TID tid);
};

} // namespace ThetaGP::Gamepad
