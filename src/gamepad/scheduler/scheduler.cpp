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

/**
 * @file scheduler.cpp
 * @brief Task scheduler implementation
 *
 * @note Based on Betaflight 4.3-maintenance scheduler
 * @see https://github.com/betaflight/betaflight/tree/4.3-maintenance
 */

#include "gamepad/scheduler/scheduler.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>

using namespace ThetaGP::Drivers::Device;

namespace ThetaGP::Gamepad {

// ============================================================================
// Scheduler Constructor
// ============================================================================

/* clang-format off */
Scheduler::Scheduler()
    : timer(SystemTimer::getInstance()),
      taskPool(),
      taskCount(0),
      currentTask(nullptr),
      ignoreCurrentTaskExecRate(false),
      ignoreCurrentTaskExecTime(false),
      taskQueuePos(0), taskQueueSize(0),
      desiredPeriodCycles(0),
      lastTargetCycles(0),
      schedLoopStartCycles(0),
      schedLoopStartMinCycles(0),
      schedLoopStartMaxCycles(0),
      schedLoopStartDeltaDownCycles(0),
      schedLoopStartDeltaUpCycles(0),
      taskGuardCycles(0),
      taskGuardMinCycles(0),
      taskGuardMaxCycles(0),
      taskGuardDeltaDownCycles(0),
      taskGuardDeltaUpCycles(0),
      taskTotalExecutionTimeUs(0),
      averageSystemLoadPercent(0),
      checkFuncMaxExecutionTimeUs(0),
      checkFuncTotalExecutionTimeUs(0),
      checkFuncMovingSumExecutionTimeUs(0),
      checkFuncMovingSumDeltaTimeUs(0),
      lateTaskCount(0),
      lateTaskTotal(0),
      nextTimingCycles(0),
      taskNextStateTimeUs(0),
      scheduleCount(0)
/* clang-format on */
{
  std::memset(taskPoolMemory, 0, sizeof(taskPoolMemory));
  std::memset(taskQueueArray, 0, sizeof(taskQueueArray));
}

// ============================================================================
// Scheduler Statistics
// ============================================================================

uint16_t Scheduler::getAverageSystemLoadPercent() {
  return averageSystemLoadPercent;
}

float Scheduler::getCycleTimeMultiplier() {
  return static_cast<float>(timer.microsToCycles(
             getTask(TaskId::Gamepad)->attribute->desiredPeriodUs)) /
         desiredPeriodCycles;
}

// ============================================================================
// Scheduler Initialization
// ============================================================================

void Scheduler::init() {
  queueClear();
  queueAdd(getTask(TaskId::System));

  schedLoopStartMinCycles = timer.microsToCycles(SCHED_START_LOOP_MIN_US);
  schedLoopStartMaxCycles = timer.microsToCycles(SCHED_START_LOOP_MAX_US);
  schedLoopStartCycles = schedLoopStartMinCycles;
  schedLoopStartDeltaDownCycles =
      static_cast<int32_t>(timer.microsToCycles(1)) /
      SCHED_START_LOOP_DOWN_STEP;
  schedLoopStartDeltaUpCycles =
      static_cast<int32_t>(timer.microsToCycles(1)) / SCHED_START_LOOP_UP_STEP;

  taskGuardMinCycles = timer.microsToCycles(TASK_GUARD_MARGIN_MIN_US);
  taskGuardMaxCycles = timer.microsToCycles(TASK_GUARD_MARGIN_MAX_US);
  taskGuardCycles = taskGuardMinCycles;
  taskGuardDeltaDownCycles = static_cast<int32_t>(timer.microsToCycles(1)) /
                             TASK_GUARD_MARGIN_DOWN_STEP;
  taskGuardDeltaUpCycles =
      static_cast<int32_t>(timer.microsToCycles(1)) / TASK_GUARD_MARGIN_UP_STEP;

  desiredPeriodCycles = timer.microsToCycles(
      getTask(TaskId::Gamepad)->attribute->desiredPeriodUs);
  lastTargetCycles = timer.getCycleCounter();

  for (uint8_t i = 0; i < static_cast<uint8_t>(TaskId::Count); i++) {
    resetTaskStatistics(static_cast<TaskId>(i));
  }

  timer.init();
}

void Scheduler::tasksInit() {
  taskPool.init(taskPoolMemory, TASK_POOL_SIZE);
  taskCount = 0;

  static TaskAttribute staticAttributes[] = {
      {"SYSTEM", "LOAD", nullptr,
       [](uint32_t t) { Scheduler::getInstance().taskSystemLoad(t); },
       TASK_PERIOD_HZ(10), static_cast<int8_t>(TaskPriority::High)},
      {"SYSTEM", "UPDATE", nullptr,
       [](uint32_t t) { Scheduler::getInstance().taskMain(t); },
       TASK_PERIOD_HZ(1000), static_cast<int8_t>(TaskPriority::High)},
      {"GAMEPAD", "CORE", nullptr,
       [](uint32_t t) { Scheduler::getInstance().taskGamepadCore(t); },
       TASK_PERIOD_HZ(1000), static_cast<int8_t>(TaskPriority::Realtime)},
  };

  for (size_t i = 0; i < 3 && i < MAX_STATIC_TASKS; i++) {
    staticTasks[i].attribute = &staticAttributes[i];
    allSlots[i].task = &staticTasks[i];
    allSlots[i].enabled = true;
    allSlots[i].isStatic = true;
    taskCount++;
  }

  init();

  setTaskEnabled(getStaticTask(1), true);
  setTaskEnabled(getStaticTask(2), true);

  rescheduleTask(TaskId::Gamepad, TASK_PERIOD_HZ(1000));
}

// ============================================================================
// Scheduler Run Loop
// ============================================================================

void Scheduler::run() {
  static uint32_t checkCycles = 0;
  static uint32_t scheduleCount = 0;
  uint32_t currentTimeUs = 0;
  uint32_t nowCycles = 0;
  Task* selectedTask = nullptr;
  uint16_t selectedTaskDynamicPriority = 0;
  uint32_t nextTargetCycles = 0;
  int32_t schedLoopRemainingCycles = 0;

  Task* keypadTask = getTask(TaskId::Gamepad);
  nowCycles = timer.getCycleCounter();

  nextTargetCycles = lastTargetCycles + desiredPeriodCycles;
  schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);

  if (schedLoopRemainingCycles <
      -static_cast<int32_t>(desiredPeriodCycles)) {
    nextTargetCycles +=
        desiredPeriodCycles *
        (1u + static_cast<uint32_t>(
                  schedLoopRemainingCycles /
                  -static_cast<int32_t>(desiredPeriodCycles)));
    schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);
  }

  if ((schedLoopRemainingCycles < schedLoopStartMinCycles) &&
      (schedLoopStartCycles < schedLoopStartMaxCycles)) {
    schedLoopStartCycles += schedLoopStartDeltaUpCycles;
  }

  if (schedLoopRemainingCycles < schedLoopStartCycles) {
    if (schedLoopStartCycles > schedLoopStartMinCycles) {
      schedLoopStartCycles -= schedLoopStartDeltaDownCycles;
    }
  }

  if (schedLoopRemainingCycles < schedLoopStartCycles) {
    if (schedLoopStartCycles > schedLoopStartMinCycles) {
      schedLoopStartCycles -= schedLoopStartDeltaDownCycles;
    }

    while (schedLoopRemainingCycles > 0) {
      nowCycles = timer.getCycleCounter();
      schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);
    }

    currentTimeUs = timer.getMicros();
    executeTask(keypadTask, currentTimeUs);

    if (cmpTimeCycles(nextTimingCycles, nowCycles) < 0) {
      nextTimingCycles += timer.microsToCycles(1000000);
      lateTaskCount = 0;
      lateTaskTotal = 0;
      taskCount = 0;
    }

    lastTargetCycles = nextTargetCycles;
  }

  nowCycles = timer.getCycleCounter();
  schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);

  if (schedLoopRemainingCycles >
      static_cast<int32_t>(
          timer.microsToCycles(CHECK_GUARD_MARGIN_US))) {
    currentTimeUs = timer.getMicros();

    for (Task* task = queueFirst(); task != nullptr; task = queueNext()) {
      if (task->attribute->staticPriority !=
          static_cast<int8_t>(TaskPriority::Realtime)) {
        if (task->attribute->checkFunc) {
          if (task->dynamicPriority > 0) {
            task->taskAgePeriods =
                1 + (cmpTimeUs(currentTimeUs, task->lastSignaledAtUs) /
                     task->attribute->desiredPeriodUs);
            task->dynamicPriority =
                1 + task->attribute->staticPriority * task->taskAgePeriods;
          } else if (task->attribute->checkFunc(
                         currentTimeUs,
                         cmpTimeUs(currentTimeUs, task->lastExecutedAtUs))) {
            const uint32_t checkFuncExecutionTimeUs =
                cmpTimeUs(timer.getMicros(), currentTimeUs);
            checkFuncMovingSumExecutionTimeUs +=
                checkFuncExecutionTimeUs -
                checkFuncMovingSumExecutionTimeUs / TASK_STATS_MOVING_SUM_COUNT;
            checkFuncMovingSumDeltaTimeUs +=
                task->taskLatestDeltaTimeUs -
                checkFuncMovingSumDeltaTimeUs / TASK_STATS_MOVING_SUM_COUNT;
            checkFuncTotalExecutionTimeUs += checkFuncExecutionTimeUs;
            checkFuncMaxExecutionTimeUs =
                std::max(checkFuncMaxExecutionTimeUs, checkFuncExecutionTimeUs);
            task->lastSignaledAtUs = currentTimeUs;
            task->taskAgePeriods = 1;
            task->dynamicPriority = 1 + task->attribute->staticPriority;
          } else {
            task->taskAgePeriods = 0;
          }
        } else {
          task->taskAgePeriods =
              cmpTimeUs(currentTimeUs, task->lastExecutedAtUs) /
              task->attribute->desiredPeriodUs;
          if (task->taskAgePeriods > 0) {
            task->dynamicPriority =
                1 + task->attribute->staticPriority * task->taskAgePeriods;
          }
        }

        if (task->dynamicPriority > selectedTaskDynamicPriority) {
          const uint32_t taskRequiredTimeUs =
              task->anticipatedExecutionTime >> TASK_EXEC_TIME_SHIFT;
          int32_t taskRequiredTimeCycles =
              static_cast<int32_t>(timer.microsToCycles(taskRequiredTimeUs));
          taskRequiredTimeCycles +=
              static_cast<int32_t>(checkCycles + taskGuardCycles);

          if ((taskRequiredTimeCycles < schedLoopRemainingCycles) ||
              ((scheduleCount & SCHED_TASK_DEFER_MASK) == 0)) {
            selectedTaskDynamicPriority = task->dynamicPriority;
            selectedTask = task;
          }
        }
      }
    }

    checkCycles = cmpTimeCycles(timer.getCycleCounter(), nowCycles);

    if (selectedTask) {
      const uint32_t taskRequiredTimeUs =
          selectedTask->anticipatedExecutionTime >> TASK_EXEC_TIME_SHIFT;
      selectedTask->execTime = taskRequiredTimeUs;
      int32_t taskRequiredTimeCycles =
          static_cast<int32_t>(timer.microsToCycles(taskRequiredTimeUs));

      nowCycles = timer.getCycleCounter();
      schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);

      taskRequiredTimeCycles += taskGuardCycles;

      if (taskRequiredTimeCycles < schedLoopRemainingCycles) {
        uint32_t anticipatedEndCycles = nowCycles + taskRequiredTimeCycles;
        executeTask(selectedTask, currentTimeUs);
        nowCycles = timer.getCycleCounter();
        int32_t cyclesOverdue = cmpTimeCycles(nowCycles, anticipatedEndCycles);

        if (cyclesOverdue > 0) {
          selectedTask->lateCount++;
          lateTaskCount++;
          lateTaskTotal += cyclesOverdue;
        }

        if ((cyclesOverdue > 0) || (-cyclesOverdue < taskGuardMinCycles)) {
          if (taskGuardCycles < taskGuardMaxCycles) {
            taskGuardCycles += taskGuardDeltaUpCycles;
          }
        } else if (taskGuardCycles > taskGuardMinCycles) {
          taskGuardCycles -= taskGuardDeltaDownCycles;
        }

        taskCount++;
      } else if ((selectedTask->taskAgePeriods > TASK_AGE_EXPEDITE_COUNT)) {
        selectedTask->anticipatedExecutionTime = static_cast<uint32_t>(
            selectedTask->anticipatedExecutionTime * TASK_AGE_EXPEDITE_SCALE);
      }
    }
  }

  scheduleCount++;
}

}  // namespace ThetaGP::Gamepad
