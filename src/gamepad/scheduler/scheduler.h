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

#include "drivers/device/systimer.h"

#include <cstddef>
#include <cstdint>

#define TASK_PERIOD_HZ(hz)          (1000000 / (hz))
#define TASK_PERIOD_MS(ms)          ((ms) * 1000)
#define TASK_PERIOD_US(us)          (us)

#define TASK_STATS_MOVING_SUM_COUNT 8
#define LOAD_PERCENTAGE_ONE         100

#define SCHED_TASK_DEFER_MASK       0x07

#define SCHED_START_LOOP_MIN_US     1
#define SCHED_START_LOOP_MAX_US     12
#define SCHED_START_LOOP_DOWN_STEP  50
#define SCHED_START_LOOP_UP_STEP    1

#define TASK_GUARD_MARGIN_MIN_US    3
#define TASK_GUARD_MARGIN_MAX_US    6
#define TASK_GUARD_MARGIN_DOWN_STEP 50
#define TASK_GUARD_MARGIN_UP_STEP   1

#define CHECK_GUARD_MARGIN_US       2

#define TASK_EXEC_TIME_SHIFT        7

#define TASK_AGE_EXPEDITE_COUNT     1
#define TASK_AGE_EXPEDITE_SCALE     0.9f

namespace ThetaGP::Gamepad {

enum class TaskPriority : int8_t {
  Realtime = -1,

  VeryLow = 1,
  Low = 2,
  Medium = 3,
  High = 4,
  VeryHigh = 5,
};

using CheckFunc = bool (*)(uint32_t currentTimeUs, uint32_t currentDeltaTimeUs);
using TaskFunc = void (*)(uint32_t currentTimeUs);

struct TaskAttribute {
  const char *taskName;
  const char *subTaskName;
  CheckFunc checkFunc;
  TaskFunc taskFunc;
  uint32_t desiredPeriodUs;
  int8_t staticPriority;
};

struct TaskInfo {
  const char *taskName;
  const char *subTaskName;
  bool isEnabled;
  int8_t staticPriority;
  uint32_t desiredPeriodUs;
  uint32_t latestDeltaTimeUs;
  uint32_t maxExecutionTimeUs;
  uint32_t totalExecutionTimeUs;
  uint32_t averageExecutionTime10thUs;
  uint32_t averageDeltaTime10thUs;
  float movingAverageCycleTimeUs;
#ifdef USE_LATE_TASK_STATISTICS
  uint32_t runCount;
  uint32_t lateCount;
  uint32_t execTime;
#endif
};

struct CheckFuncInfo {
  uint32_t maxExecutionTimeUs;
  uint32_t totalExecutionTimeUs;
  uint32_t averageExecutionTimeUs;
  uint32_t averageDeltaTimeUs;
};

struct Task {
  TaskAttribute *attribute = nullptr;
  uint16_t dynamicPriority = 0;
  uint16_t taskAgePeriods = 0;
  uint32_t taskLatestDeltaTimeUs = 0;
  uint32_t lastExecutedAtUs = 0;
  uint32_t lastSignaledAtUs = 0;
  uint32_t lastDesiredAtUs = 0;
  float movingAverageCycleTimeUs = 0.0f;
  uint32_t anticipatedExecutionTime = 0;
  uint32_t movingSumDeltaTime10thUs = 0;
  uint32_t movingSumExecutionTime10thUs = 0;
  uint32_t maxExecutionTimeUs = 0;
  uint32_t totalExecutionTimeUs = 0;
  uint32_t lastStatsAtUs = 0;
  uint32_t runCount = 0;
  uint32_t lateCount = 0;
  uint32_t execTime = 0;

  void resetStatistics();
  void resetMaxExecutionTime();
};

class Scheduler {
public:
  static constexpr size_t MAX_TASK_COUNT = 8;

private:
  Drivers::Device::SystemTimer *timer = nullptr;

  Task *realtimeTask = nullptr;

  Task *taskQueueArray[MAX_TASK_COUNT + 1]{};
  Task *currentTask = nullptr;
  bool ignoreCurrentTaskExecRate = false;
  bool ignoreCurrentTaskExecTime = false;

  size_t taskQueuePos = 0;
  size_t taskQueueSize = 0;

  uint32_t desiredPeriodCycles = 0;
  uint32_t lastTargetCycles = 0;

  int32_t schedLoopStartCycles = 0;
  int32_t schedLoopStartMinCycles = 0;
  int32_t schedLoopStartMaxCycles = 0;
  uint32_t schedLoopStartDeltaDownCycles = 0;
  uint32_t schedLoopStartDeltaUpCycles = 0;

  int32_t taskGuardCycles = 0;
  int32_t taskGuardMinCycles = 0;
  int32_t taskGuardMaxCycles = 0;
  uint32_t taskGuardDeltaDownCycles = 0;
  uint32_t taskGuardDeltaUpCycles = 0;

  uint32_t taskTotalExecutionTimeUs = 0;

  uint32_t checkFuncMaxExecutionTimeUs = 0;
  uint32_t checkFuncTotalExecutionTimeUs = 0;
  uint32_t checkFuncMovingSumExecutionTimeUs = 0;
  uint32_t checkFuncMovingSumDeltaTimeUs = 0;

  int16_t lateTaskCount = 0;
  uint32_t lateTaskTotal = 0;
  uint32_t nextTimingCycles = 0;

  uint32_t taskNextStateTimeUs = 0;
  uint32_t scheduleCount = 0;
  uint32_t tasksExecutedThisCycle = 0;
  uint32_t checkCycles = 0;

  int32_t cmpTimeCycles(uint32_t t1, uint32_t t2);
  uint32_t cmpTimeUs(uint32_t t1, uint32_t t2);

public:
  Scheduler();

  static Scheduler &getInstance() {
    static Scheduler instance;
    return instance;
  }

  void bindTimeBase(Drivers::Device::SystemTimer &t);
  void init();
  void run();

  uint32_t getAndResetTotalExecutionTime();

  void queueClear();
  bool queueContains(Task *task);
  bool queueAdd(Task *task);
  bool queueRemove(Task *task);
  Task *queueFirst();
  Task *queueNext();

  void ignoreTaskStateTime();
  void ignoreTaskExecRate();
  void ignoreTaskExecTime();
  bool getIgnoreTaskExecTime();

  void setNextStateTime(uint32_t nextStateTimeUs);
  uint32_t getNextStateTime();

  uint32_t executeTask(Task *selectedTask, uint32_t currentTimeUs);

  Task *getCurrentTask();

  void getCheckFuncInfo(CheckFuncInfo *checkFuncInfo);
  void resetCheckFunctionMaxExecutionTime();
};

} // namespace ThetaGP::Gamepad
