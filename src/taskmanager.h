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

#include <cstddef>
#include <cstdint>

namespace ThetaGP::Gamepad {

using TID = int;

class TaskManager {
private:
  static constexpr uint32_t MAX_TASKS = 16;
  static constexpr TID INVALID_TID = -1;

  struct TaskRecord {
    Task *task = nullptr;
    TaskAttribute *attribute = nullptr;
    bool inUse = false;
  };

  static Scheduler *scheduler;

  // ── Task slots ──
  //   One Task/TaskAttribute pair per TID, held for the firmware lifetime.
  //   Invariant: records[i].task == &taskSlots[i] and records[i].attribute
  //   == &attrSlots[i] for every i in [0, MAX_TASKS). A TID is a slot index
  //   and is valid exactly while its record is in use.
  static Task taskSlots[MAX_TASKS];          // 1,024 B (16 x 64)
  static TaskAttribute attrSlots[MAX_TASKS]; //   384 B (16 x 24)

  static TaskRecord records[MAX_TASKS];
  static size_t taskCount;
  static uint16_t averageSystemLoadPercent;

  TaskManager() = delete;

  static void taskSystemLoad(uint32_t currentTimeUs);
  static void taskMain(uint32_t currentTimeUs);

public:
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
  static const TaskInfo *getTaskInfo(TID tid);
  static size_t getTaskCount() { return taskCount; }
  static uint16_t getAverageSystemLoadPercent() {
    return averageSystemLoadPercent;
  }
};

} // namespace ThetaGP::Gamepad
