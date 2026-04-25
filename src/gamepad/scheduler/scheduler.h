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
 * @file scheduler.h
 * @brief Cooperative task scheduler with real-time priority support
 *
 * @note This scheduler design is based on Betaflight 4.3-maintenance branch
 * @see https://github.com/betaflight/betaflight/tree/4.3-maintenance
 */

#pragma once

#include "drivers/device/system_timer.h"

#include "utils/mempool/mempool.h"

#include <cstddef>
#include <cstdint>

// Time conversion
#define TASK_PERIOD_HZ(hz)          (1000000 / (hz))
#define TASK_PERIOD_MS(ms)          ((ms) * 1000)
#define TASK_PERIOD_US(us)          (us)

// Betaflight: 8-sample moving average
#define TASK_STATS_MOVING_SUM_COUNT 8

#define LOAD_PERCENTAGE_ONE         100

// Betaflight: Long-running tasks every 8 cycles
#define SCHED_TASK_DEFER_MASK       0x07

// Adaptive loop timing (microseconds)
#define SCHED_START_LOOP_MIN_US     1
#define SCHED_START_LOOP_MAX_US     12
#define SCHED_START_LOOP_DOWN_STEP  50
#define SCHED_START_LOOP_UP_STEP    1

// Adaptive task guard margin (microseconds)
#define TASK_GUARD_MARGIN_MIN_US    3
#define TASK_GUARD_MARGIN_MAX_US    6
#define TASK_GUARD_MARGIN_DOWN_STEP 50
#define TASK_GUARD_MARGIN_UP_STEP   1

#define CHECK_GUARD_MARGIN_US       2

// Betaflight: 7-bit fixed-point (128x precision)
#define TASK_EXEC_TIME_SHIFT        7

#define TASK_AGE_EXPEDITE_COUNT     1
#define TASK_AGE_EXPEDITE_SCALE     0.9f

#define SCHEDULER_DELAY_LIMIT       10

namespace ThetaGP::Gamepad {

enum class TaskPriority : int8_t {
  Realtime = -1,

  VeryLow = 1,
  Low = 2,
  Medium = 3,
  High = 4,
  VeryHigh = 5,
};

enum class TaskId {
  System,
  Main,
  GPCore,

  Count,

  None = Count,
  Self
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
  TaskAttribute *attribute;

  uint16_t dynamicPriority;
  uint16_t taskAgePeriods;
  uint32_t taskLatestDeltaTimeUs;
  uint32_t lastExecutedAtUs;
  uint32_t lastSignaledAtUs;
  uint32_t lastDesiredAtUs;

  float movingAverageCycleTimeUs;
  uint32_t anticipatedExecutionTime;
  uint32_t movingSumDeltaTime10thUs;
  uint32_t movingSumExecutionTime10thUs;
  uint32_t maxExecutionTimeUs;
  uint32_t totalExecutionTimeUs;
  uint32_t lastStatsAtUs;
  uint32_t runCount;
  uint32_t lateCount;
  uint32_t execTime;

  Task();
  void resetStatistics();
  void resetMaxExecutionTime();
};

// Task slot for managing task state and lifecycle
struct TaskSlot {
  Task *task = nullptr;
  bool enabled = false;
  bool isStatic = true;
};

class Scheduler {
private:
  static constexpr size_t MAX_STATIC_TASKS = 8;
  static constexpr size_t MAX_DYNAMIC_TASKS = 8;
  static constexpr size_t MAX_TASK_COUNT = MAX_STATIC_TASKS + MAX_DYNAMIC_TASKS;
  static constexpr size_t TASK_POOL_SIZE = 2048;

  Drivers::Device::SystemTimer &timer;

  uint8_t taskPoolMemory[TASK_POOL_SIZE];
  Mempool::Mempool taskPool;

  Task staticTasks[MAX_STATIC_TASKS];
  TaskSlot allSlots[MAX_TASK_COUNT];
  size_t taskCount;

  Task *taskQueueArray[MAX_TASK_COUNT + 1];

  Task *currentTask;
  bool ignoreCurrentTaskExecRate;
  bool ignoreCurrentTaskExecTime;

  size_t taskQueuePos;
  size_t taskQueueSize;

  uint32_t desiredPeriodCycles;
  uint32_t lastTargetCycles;

  int32_t schedLoopStartCycles;
  int32_t schedLoopStartMinCycles;
  int32_t schedLoopStartMaxCycles;
  uint32_t schedLoopStartDeltaDownCycles;
  uint32_t schedLoopStartDeltaUpCycles;

  int32_t taskGuardCycles;
  int32_t taskGuardMinCycles;
  int32_t taskGuardMaxCycles;
  uint32_t taskGuardDeltaDownCycles;
  uint32_t taskGuardDeltaUpCycles;

  uint32_t taskTotalExecutionTimeUs;
  uint16_t averageSystemLoadPercent;

  uint32_t checkFuncMaxExecutionTimeUs;
  uint32_t checkFuncTotalExecutionTimeUs;
  uint32_t checkFuncMovingSumExecutionTimeUs;
  uint32_t checkFuncMovingSumDeltaTimeUs;

  int16_t lateTaskCount;
  uint32_t lateTaskTotal;
  uint32_t nextTimingCycles;

  uint32_t taskNextStateTimeUs;
  uint32_t scheduleCount;

  void queueClear();
  bool queueContains(Task *task);
  bool queueAdd(Task *task);
  bool queueRemove(Task *task);
  Task *queueFirst();
  Task *queueNext();

  int32_t cmpTimeCycles(uint32_t t1, uint32_t t2);
  uint32_t cmpTimeUs(uint32_t t1, uint32_t t2);

public:
  Scheduler();

  static Scheduler &getInstance() {
    static Scheduler instance;
    return instance;
  }

  void init();
  void run();

  void setTaskEnabled(TaskId taskId, bool enabled);
  void rescheduleTask(TaskId taskId, uint32_t newPeriodUs);
  uint32_t getTaskDeltaTimeUs(TaskId taskId);

  void getTaskInfo(TaskId taskId, TaskInfo *taskInfo);
  void getCheckFuncInfo(CheckFuncInfo *checkFuncInfo);

  void resetTaskStatistics(TaskId taskId);
  void resetTaskMaxExecutionTime(TaskId taskId);
  void resetCheckFunctionMaxExecutionTime();
  uint16_t getAverageSystemLoadPercent();
  float getCycleTimeMultiplier();

  void ignoreTaskStateTime();
  void ignoreTaskExecRate();
  void ignoreTaskExecTime();
  bool getIgnoreTaskExecTime();

  void setNextStateTime(uint32_t nextStateTimeUs);
  uint32_t getNextStateTime();

  uint32_t executeTask(Task *selectedTask, uint32_t currentTimeUs);

  Task *getCurrentTask();

  void taskSystemLoad(uint32_t currentTimeUs);
  void taskMain(uint32_t currentTimeUs);
  void taskGamepadCore(uint32_t currentTimeUs);

  void tasksInit();
  Task *getTask(TaskId taskId);

  Task *createTask(const char *name, const char *subName, TaskFunc func,
                   uint32_t periodUs, TaskPriority priority);
  Mempool::PoolError destroyTask(Task *task);
  void suspendTask(Task *task);
  void resumeTask(Task *task);
  bool isTaskEnabled(Task *task) const;
  void setTaskEnabled(Task *task, bool enabled);

  Task *getStaticTask(size_t index);
  size_t getTaskCount() const { return taskCount; }
  TaskSlot *getTaskSlotByTask(Task *task);
  bool isValidTask(Task *task) const;
};

} // namespace ThetaGP::Gamepad
