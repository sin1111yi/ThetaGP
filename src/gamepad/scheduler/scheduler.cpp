#include "gamepad/scheduler/scheduler.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>

namespace ThetaGP::Gamepad {

// ============================================================================
// Scheduler Constructor
// ============================================================================

Scheduler::Scheduler() = default;

// ============================================================================
// Scheduler Initialization
// ============================================================================

void Scheduler::init() {
  schedLoopStartMinCycles = timer->microsToCycles(SCHED_START_LOOP_MIN_US);
  schedLoopStartMaxCycles = timer->microsToCycles(SCHED_START_LOOP_MAX_US);
  schedLoopStartCycles = schedLoopStartMinCycles;
  schedLoopStartDeltaDownCycles =
      static_cast<int32_t>(timer->microsToCycles(1)) /
      SCHED_START_LOOP_DOWN_STEP;
  schedLoopStartDeltaUpCycles =
      static_cast<int32_t>(timer->microsToCycles(1)) / SCHED_START_LOOP_UP_STEP;

  taskGuardMinCycles = timer->microsToCycles(TASK_GUARD_MARGIN_MIN_US);
  taskGuardMaxCycles = timer->microsToCycles(TASK_GUARD_MARGIN_MAX_US);
  taskGuardCycles = taskGuardMinCycles;
  taskGuardDeltaDownCycles = static_cast<int32_t>(timer->microsToCycles(1)) /
                             TASK_GUARD_MARGIN_DOWN_STEP;
  taskGuardDeltaUpCycles = static_cast<int32_t>(timer->microsToCycles(1)) /
                           TASK_GUARD_MARGIN_UP_STEP;

  if (realtimeTask && realtimeTask->attribute) {
    desiredPeriodCycles =
        timer->microsToCycles(realtimeTask->attribute->desiredPeriodUs);
  } else {
    desiredPeriodCycles = timer->microsToCycles(TASK_PERIOD_HZ(1000));
  }
  lastTargetCycles = timer->getCycleCounter();
}

// ============================================================================
// Scheduler Run Loop
// ============================================================================

void Scheduler::run() {
  uint32_t currentTimeUs = 0;
  uint32_t nowCycles = 0;
  Task *selectedTask = nullptr;
  uint16_t selectedTaskDynamicPriority = 0;
  uint32_t nextTargetCycles = 0;
  int32_t schedLoopRemainingCycles = 0;

  nowCycles = timer->getCycleCounter();

  nextTargetCycles = lastTargetCycles + desiredPeriodCycles;
  schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);

  if (schedLoopRemainingCycles < -static_cast<int32_t>(desiredPeriodCycles)) {
    nextTargetCycles += desiredPeriodCycles *
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
      nowCycles = timer->getCycleCounter();
      schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);
    }

    currentTimeUs = timer->getMicros();
    if (realtimeTask) {
      executeTask(realtimeTask, currentTimeUs);
    }

    if (cmpTimeCycles(nextTimingCycles, nowCycles) < 0) {
      nextTimingCycles += timer->microsToCycles(1000000);
      lateTaskCount = 0;
      lateTaskTotal = 0;
      tasksExecutedThisCycle = 0;
    }

    lastTargetCycles = nextTargetCycles;
  }

  nowCycles = timer->getCycleCounter();
  schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);

  if (schedLoopRemainingCycles >
      static_cast<int32_t>(timer->microsToCycles(CHECK_GUARD_MARGIN_US))) {
    currentTimeUs = timer->getMicros();

    for (Task *task = queueFirst(); task != nullptr; task = queueNext()) {
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
                cmpTimeUs(timer->getMicros(), currentTimeUs);
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
              static_cast<int32_t>(timer->microsToCycles(taskRequiredTimeUs));
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

    checkCycles = cmpTimeCycles(timer->getCycleCounter(), nowCycles);

    if (selectedTask) {
      const uint32_t taskRequiredTimeUs =
          selectedTask->anticipatedExecutionTime >> TASK_EXEC_TIME_SHIFT;
      selectedTask->execTime = taskRequiredTimeUs;
      int32_t taskRequiredTimeCycles =
          static_cast<int32_t>(timer->microsToCycles(taskRequiredTimeUs));

      nowCycles = timer->getCycleCounter();
      schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);

      taskRequiredTimeCycles += taskGuardCycles;

      if (taskRequiredTimeCycles < schedLoopRemainingCycles) {
        uint32_t anticipatedEndCycles = nowCycles + taskRequiredTimeCycles;
        executeTask(selectedTask, currentTimeUs);
        nowCycles = timer->getCycleCounter();
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

        tasksExecutedThisCycle++;
      } else if ((selectedTask->taskAgePeriods > TASK_AGE_EXPEDITE_COUNT)) {
        selectedTask->anticipatedExecutionTime = static_cast<uint32_t>(
            selectedTask->anticipatedExecutionTime * TASK_AGE_EXPEDITE_SCALE);
      }
    }
  }

  scheduleCount++;
}

uint32_t Scheduler::getAndResetTotalExecutionTime() {
  uint32_t temp = taskTotalExecutionTimeUs;
  taskTotalExecutionTimeUs = 0;
  return temp;
}

// ============================================================================
// Time Comparison
// ============================================================================

int32_t Scheduler::cmpTimeCycles(uint32_t t1, uint32_t t2) {
  return static_cast<int32_t>(t1 - t2);
}

uint32_t Scheduler::cmpTimeUs(uint32_t t1, uint32_t t2) { return t1 - t2; }

// ============================================================================
// Task Queue Management
// ============================================================================

void Scheduler::queueClear() {
  std::memset(taskQueueArray, 0, sizeof(taskQueueArray));
  taskQueuePos = 0;
  taskQueueSize = 0;
}

bool Scheduler::queueContains(Task *task) {
  for (size_t ii = 0; ii < taskQueueSize; ++ii) {
    if (taskQueueArray[ii] == task) {
      return true;
    }
  }
  return false;
}

bool Scheduler::queueAdd(Task *task) {
  if ((taskQueueSize >= MAX_TASK_COUNT) || queueContains(task)) {
    return false;
  }

  if (task->attribute && task->attribute->staticPriority ==
                             static_cast<int8_t>(TaskPriority::Realtime)) {
    if (realtimeTask) {
      return false;
    }
    realtimeTask = task;
  }

  for (size_t ii = 0; ii <= static_cast<size_t>(taskQueueSize); ++ii) {
    if (taskQueueArray[ii] == nullptr ||
        taskQueueArray[ii]->attribute->staticPriority <
            task->attribute->staticPriority) {
      std::memmove(&taskQueueArray[ii + 1], &taskQueueArray[ii],
                   sizeof(Task *) * (taskQueueSize - ii));
      taskQueueArray[ii] = task;
      ++taskQueueSize;
      return true;
    }
  }
  return false;
}

bool Scheduler::queueRemove(Task *task) {
  if (task == realtimeTask) {
    realtimeTask = nullptr;
  }

  for (size_t ii = 0; ii < taskQueueSize; ++ii) {
    if (taskQueueArray[ii] == task) {
      std::memmove(&taskQueueArray[ii], &taskQueueArray[ii + 1],
                   sizeof(Task *) * (taskQueueSize - ii));
      --taskQueueSize;
      return true;
    }
  }
  return false;
}

Task *Scheduler::queueFirst() {
  taskQueuePos = 0;
  return taskQueueArray[0];
}

Task *Scheduler::queueNext() { return taskQueueArray[++taskQueuePos]; }

// ============================================================================
// Task State Management
// ============================================================================

void Scheduler::ignoreTaskStateTime() {
  ignoreCurrentTaskExecRate = true;
  ignoreCurrentTaskExecTime = true;
}

void Scheduler::ignoreTaskExecRate() { ignoreCurrentTaskExecRate = true; }

void Scheduler::ignoreTaskExecTime() { ignoreCurrentTaskExecTime = true; }

bool Scheduler::getIgnoreTaskExecTime() { return ignoreCurrentTaskExecTime; }

void Scheduler::setNextStateTime(uint32_t nextStateTimeUs) {
  taskNextStateTimeUs = nextStateTimeUs;
}

uint32_t Scheduler::getNextStateTime() {
  return currentTask
             ? (currentTask->anticipatedExecutionTime >> TASK_EXEC_TIME_SHIFT)
             : 0;
}

// ============================================================================
// Task Execution
// ============================================================================

uint32_t Scheduler::executeTask(Task *selectedTask, uint32_t currentTimeUs) {
  uint32_t taskExecutionTimeUs = 0;

  if (selectedTask) {
    currentTask = selectedTask;
    ignoreCurrentTaskExecRate = false;
    ignoreCurrentTaskExecTime = false;
    taskNextStateTimeUs = UINT32_MAX;

    const float period =
        static_cast<float>(currentTimeUs - selectedTask->lastExecutedAtUs);
    selectedTask->lastExecutedAtUs = currentTimeUs;
    selectedTask->lastDesiredAtUs += selectedTask->attribute->desiredPeriodUs;
    selectedTask->dynamicPriority = 0;

    const uint32_t currentTimeBeforeTaskCallUs = timer->getMicros();
    selectedTask->attribute->taskFunc(currentTimeBeforeTaskCallUs);
    taskExecutionTimeUs = timer->getMicros() - currentTimeBeforeTaskCallUs;
    taskTotalExecutionTimeUs += taskExecutionTimeUs;

    selectedTask->movingSumExecutionTime10thUs +=
        (taskExecutionTimeUs * 10) -
        selectedTask->movingSumExecutionTime10thUs /
            TASK_STATS_MOVING_SUM_COUNT;

    if (!ignoreCurrentTaskExecRate) {
      selectedTask->taskLatestDeltaTimeUs =
          cmpTimeUs(currentTimeUs, selectedTask->lastStatsAtUs);
      selectedTask->movingSumDeltaTime10thUs +=
          (selectedTask->taskLatestDeltaTimeUs * 10) -
          selectedTask->movingSumDeltaTime10thUs / TASK_STATS_MOVING_SUM_COUNT;
      selectedTask->lastStatsAtUs = currentTimeUs;
    }

    if (taskNextStateTimeUs != UINT32_MAX) {
      selectedTask->anticipatedExecutionTime = taskNextStateTimeUs
                                               << TASK_EXEC_TIME_SHIFT;
    } else if (!ignoreCurrentTaskExecTime) {
      if (taskExecutionTimeUs >
          (selectedTask->anticipatedExecutionTime >> TASK_EXEC_TIME_SHIFT)) {
        selectedTask->anticipatedExecutionTime = taskExecutionTimeUs
                                                 << TASK_EXEC_TIME_SHIFT;
      } else if (selectedTask->anticipatedExecutionTime > 1) {
        selectedTask->anticipatedExecutionTime--;
      }
    }

    if (!ignoreCurrentTaskExecTime) {
      selectedTask->maxExecutionTimeUs =
          std::max(selectedTask->maxExecutionTimeUs, taskExecutionTimeUs);
    }

    selectedTask->totalExecutionTimeUs += taskExecutionTimeUs;
    selectedTask->movingAverageCycleTimeUs +=
        0.05f * (period - selectedTask->movingAverageCycleTimeUs);
    selectedTask->runCount++;
  }

  return taskExecutionTimeUs;
}

Task *Scheduler::getCurrentTask() { return currentTask; }

void Scheduler::bindTimeBase(Drivers::Device::SystemTimer &t) {
  if (t.isInitialized())
    timer = &t;
}

// ============================================================================
// Check Function Statistics
// ============================================================================

void Scheduler::getCheckFuncInfo(CheckFuncInfo *checkFuncInfo) {
  checkFuncInfo->maxExecutionTimeUs = checkFuncMaxExecutionTimeUs;
  checkFuncInfo->totalExecutionTimeUs = checkFuncTotalExecutionTimeUs;
  checkFuncInfo->averageExecutionTimeUs =
      checkFuncMovingSumExecutionTimeUs / TASK_STATS_MOVING_SUM_COUNT;
  checkFuncInfo->averageDeltaTimeUs =
      checkFuncMovingSumDeltaTimeUs / TASK_STATS_MOVING_SUM_COUNT;
}

void Scheduler::resetCheckFunctionMaxExecutionTime() {
  checkFuncMaxExecutionTimeUs = 0;
}

// ============================================================================
// Task Methods
// ============================================================================

void Task::resetStatistics() {
  anticipatedExecutionTime = 0;
  movingSumDeltaTime10thUs = 0;
  totalExecutionTimeUs = 0;
  maxExecutionTimeUs = 0;
}

void Task::resetMaxExecutionTime() {
  maxExecutionTimeUs = 0;
  lateCount = 0;
  runCount = 0;
}

} // namespace ThetaGP::Gamepad
