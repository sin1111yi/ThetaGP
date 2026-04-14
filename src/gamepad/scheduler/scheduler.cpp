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

#include "scheduler.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>

using namespace ThetaGP::Drivers::Device;

namespace ThetaGP::Gamepad::Scheduler {

Task::Task()
    : attribute(nullptr), dynamicPriority(0), taskAgePeriods(0),
      taskLatestDeltaTimeUs(0), lastExecutedAtUs(0), lastSignaledAtUs(0),
      lastDesiredAtUs(0), movingAverageCycleTimeUs(0),
      anticipatedExecutionTime(0), movingSumDeltaTime10thUs(0),
      movingSumExecutionTime10thUs(0), maxExecutionTimeUs(0),
      totalExecutionTimeUs(0), lastStatsAtUs(0), runCount(0), lateCount(0),
      execTime(0) {}

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

Scheduler::Scheduler()
    : timer(SystemTimer::getInstance()), taskPool(), taskCount(0),
      currentTask(nullptr), ignoreCurrentTaskExecRate(false),
      ignoreCurrentTaskExecTime(false), taskQueuePos(0), taskQueueSize(0),
      desiredPeriodCycles(0), lastTargetCycles(0), schedLoopStartCycles(0),
      schedLoopStartMinCycles(0), schedLoopStartMaxCycles(0),
      schedLoopStartDeltaDownCycles(0), schedLoopStartDeltaUpCycles(0),
      taskGuardCycles(0), taskGuardMinCycles(0), taskGuardMaxCycles(0),
      taskGuardDeltaDownCycles(0), taskGuardDeltaUpCycles(0),
      taskTotalExecutionTimeUs(0), averageSystemLoadPercent(0),
      checkFuncMaxExecutionTimeUs(0), checkFuncTotalExecutionTimeUs(0),
      checkFuncMovingSumExecutionTimeUs(0), checkFuncMovingSumDeltaTimeUs(0),
      lateTaskCount(0), lateTaskTotal(0), nextTimingCycles(0),
      taskNextStateTimeUs(0), scheduleCount(0) {
  std::memset(taskPoolMemory, 0, sizeof(taskPoolMemory));
  std::memset(taskQueueArray, 0, sizeof(taskQueueArray));
}

int32_t Scheduler::cmpTimeCycles(uint32_t t1, uint32_t t2) {
  return static_cast<int32_t>(t1 - t2);
}

uint32_t Scheduler::cmpTimeUs(uint32_t t1, uint32_t t2) { return t1 - t2; }

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

void Scheduler::taskSystemLoad(uint32_t currentTimeUs) {
  static uint32_t lastExecutedAtUs = 0;
  uint32_t deltaTime = cmpTimeUs(currentTimeUs, lastExecutedAtUs);

  if (deltaTime) {
    averageSystemLoadPercent = 100 * taskTotalExecutionTimeUs / deltaTime;
    taskTotalExecutionTimeUs = 0;
    lastExecutedAtUs = currentTimeUs;
  } else {
    ignoreTaskExecTime();
  }
}

void Scheduler::getCheckFuncInfo(CheckFuncInfo *checkFuncInfo) {
  checkFuncInfo->maxExecutionTimeUs = checkFuncMaxExecutionTimeUs;
  checkFuncInfo->totalExecutionTimeUs = checkFuncTotalExecutionTimeUs;
  checkFuncInfo->averageExecutionTimeUs =
      checkFuncMovingSumExecutionTimeUs / TASK_STATS_MOVING_SUM_COUNT;
  checkFuncInfo->averageDeltaTimeUs =
      checkFuncMovingSumDeltaTimeUs / TASK_STATS_MOVING_SUM_COUNT;
}

void Scheduler::getTaskInfo(TaskId taskId, TaskInfo *taskInfo) {
  Task *task = getTask(taskId);
  taskInfo->isEnabled = queueContains(task);
  taskInfo->desiredPeriodUs = task->attribute->desiredPeriodUs;
  taskInfo->staticPriority = task->attribute->staticPriority;
  taskInfo->taskName = task->attribute->taskName;
  taskInfo->subTaskName = task->attribute->subTaskName;
  taskInfo->maxExecutionTimeUs = task->maxExecutionTimeUs;
  taskInfo->totalExecutionTimeUs = task->totalExecutionTimeUs;
  taskInfo->averageExecutionTime10thUs =
      task->movingSumExecutionTime10thUs / TASK_STATS_MOVING_SUM_COUNT;
  taskInfo->averageDeltaTime10thUs =
      task->movingSumDeltaTime10thUs / TASK_STATS_MOVING_SUM_COUNT;
  taskInfo->latestDeltaTimeUs = task->taskLatestDeltaTimeUs;
  taskInfo->movingAverageCycleTimeUs = task->movingAverageCycleTimeUs;
#ifdef USE_LATE_TASK_STATISTICS
  taskInfo->runCount = task->runCount;
  taskInfo->lateCount = task->lateCount;
  taskInfo->execTime = task->execTime;
#endif
}

void Scheduler::rescheduleTask(TaskId taskId, uint32_t newPeriodUs) {
  Task *task = nullptr;

  if (taskId == TaskId::Self) {
    task = currentTask;
  } else if (taskId < TaskId::Count) {
    task = getTask(taskId);
  } else {
    return;
  }

  task->attribute->desiredPeriodUs =
      std::max(static_cast<uint32_t>(SCHEDULER_DELAY_LIMIT), newPeriodUs);

  if (taskId == TaskId::Gamepad) {
    desiredPeriodCycles =
        timer.microsToCycles(task->attribute->desiredPeriodUs);
  }
}

void Scheduler::setTaskEnabled(TaskId taskId, bool enabled) {
  Task *task = nullptr;
  if (taskId == TaskId::Self) {
    task = currentTask;
  } else if (taskId < TaskId::Count) {
    task = getTask(taskId);
  } else {
    return;
  }

  if (enabled && task->attribute->taskFunc) {
    queueAdd(task);
  } else {
    queueRemove(task);
  }
}

uint32_t Scheduler::getTaskDeltaTimeUs(TaskId taskId) {
  if (taskId == TaskId::Self) {
    return currentTask->taskLatestDeltaTimeUs;
  } else if (taskId < TaskId::Count) {
    return getTask(taskId)->taskLatestDeltaTimeUs;
  }
  return 0;
}

void Scheduler::ignoreTaskStateTime() {
  ignoreCurrentTaskExecRate = true;
  ignoreCurrentTaskExecTime = true;
}

void Scheduler::ignoreTaskExecRate() { ignoreCurrentTaskExecRate = true; }

void Scheduler::ignoreTaskExecTime() { ignoreCurrentTaskExecTime = true; }

bool Scheduler::getIgnoreTaskExecTime() { return ignoreCurrentTaskExecTime; }

void Scheduler::resetTaskStatistics(TaskId taskId) {
  Task *task = nullptr;
  if (taskId == TaskId::Self) {
    task = currentTask;
  } else if (taskId < TaskId::Count) {
    task = getTask(taskId);
  } else {
    return;
  }

  if (task) {
    task->resetStatistics();
  }
}

void Scheduler::resetTaskMaxExecutionTime(TaskId taskId) {
  Task *task = nullptr;
  if (taskId == TaskId::Self) {
    task = currentTask;
  } else if (taskId < TaskId::Count) {
    task = getTask(taskId);
  } else {
    return;
  }

  if (task) {
    task->resetMaxExecutionTime();
  }
}

void Scheduler::resetCheckFunctionMaxExecutionTime() {
  checkFuncMaxExecutionTimeUs = 0;
}

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
}

void Scheduler::setNextStateTime(uint32_t nextStateTimeUs) {
  taskNextStateTimeUs = nextStateTimeUs;
}

uint32_t Scheduler::getNextStateTime() {
  return currentTask
             ? (currentTask->anticipatedExecutionTime >> TASK_EXEC_TIME_SHIFT)
             : 0;
}

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

    const uint32_t currentTimeBeforeTaskCallUs = timer.getMicros();
    selectedTask->attribute->taskFunc(currentTimeBeforeTaskCallUs);
    taskExecutionTimeUs = timer.getMicros() - currentTimeBeforeTaskCallUs;
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

void Scheduler::run() {
  static uint32_t checkCycles = 0;
  static uint32_t scheduleCount = 0;
  uint32_t currentTimeUs = 0;
  uint32_t nowCycles = 0;
  Task *selectedTask = nullptr;
  uint16_t selectedTaskDynamicPriority = 0;
  uint32_t nextTargetCycles = 0;
  int32_t schedLoopRemainingCycles = 0;

  Task *keypadTask = getTask(TaskId::Gamepad);
  nowCycles = timer.getCycleCounter();

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
      static_cast<int32_t>(timer.microsToCycles(CHECK_GUARD_MARGIN_US))) {
    currentTimeUs = timer.getMicros();

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

uint16_t Scheduler::getAverageSystemLoadPercent() {
  return averageSystemLoadPercent;
}

float Scheduler::getCycleTimeMultiplier() {
  return static_cast<float>(timer.microsToCycles(
             getTask(TaskId::Gamepad)->attribute->desiredPeriodUs)) /
         desiredPeriodCycles;
}

Task *Scheduler::getCurrentTask() { return currentTask; }

void Scheduler::taskMain(uint32_t currentTimeUs) { (void)currentTimeUs; }

void Scheduler::taskGamepadCore(uint32_t currentTimeUs) { (void)currentTimeUs; }

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

Task *Scheduler::getStaticTask(size_t index) {
  if (index < MAX_STATIC_TASKS) {
    return &staticTasks[index];
  }
  return nullptr;
}

TaskSlot *Scheduler::getTaskSlotByTask(Task *task) {
  for (size_t i = 0; i < taskCount; i++) {
    if (allSlots[i].task == task) {
      return &allSlots[i];
    }
  }
  return nullptr;
}

bool Scheduler::isValidTask(Task *task) const {
  if (!task) {
    return false;
  }
  for (size_t i = 0; i < taskCount; i++) {
    if (allSlots[i].task == task && allSlots[i].enabled) {
      return true;
    }
  }
  return false;
}

Task *Scheduler::createTask(const char *name, const char *subName,
                            TaskFunc func, uint32_t periodUs,
                            TaskPriority priority) {
  if (taskCount >= MAX_TASK_COUNT) {
    return nullptr;
  }

  Task *task = static_cast<Task *>(taskPool.alloc(sizeof(Task)));
  if (!task) {
    return nullptr;
  }

  TaskAttribute *attr =
      static_cast<TaskAttribute *>(taskPool.alloc(sizeof(TaskAttribute)));
  if (!attr) {
    taskPool.free(task);
    return nullptr;
  }

  attr->taskName = name;
  attr->subTaskName = subName;
  attr->checkFunc = nullptr;
  attr->taskFunc = func;
  attr->desiredPeriodUs = periodUs;
  attr->staticPriority = static_cast<int8_t>(priority);

  task->attribute = attr;

  allSlots[taskCount].task = task;
  allSlots[taskCount].enabled = true;
  allSlots[taskCount].isStatic = false;
  taskCount++;

  return task;
}

Mempool::PoolError Scheduler::destroyTask(Task *task) {
  TaskSlot *slot = getTaskSlotByTask(task);
  if (!slot || slot->isStatic) {
    return Mempool::PoolError::NotAllocated;
  }

  queueRemove(task);

  taskPool.free(task->attribute);
  taskPool.free(task);

  // Clear current slot
  slot->task = nullptr;
  slot->enabled = false;
  slot->isStatic = false;

  // Move remaining slots
  for (size_t i = 0; i < taskCount - 1; i++) {
    allSlots[i] = allSlots[i + 1];
  }

  // Clear last slot
  allSlots[taskCount - 1].task = nullptr;
  allSlots[taskCount - 1].enabled = false;
  allSlots[taskCount - 1].isStatic = false;

  taskCount--;

  return Mempool::PoolError::OK;
}

void Scheduler::suspendTask(Task *task) { setTaskEnabled(task, false); }

void Scheduler::resumeTask(Task *task) { setTaskEnabled(task, true); }

bool Scheduler::isTaskEnabled(Task *task) const {
  const TaskSlot *slot = nullptr;
  for (size_t i = 0; i < taskCount; i++) {
    if (allSlots[i].task == task) {
      slot = &allSlots[i];
      break;
    }
  }
  return slot ? slot->enabled : false;
}

void Scheduler::setTaskEnabled(Task *task, bool enabled) {
  TaskSlot *slot = getTaskSlotByTask(task);
  if (!slot) {
    return;
  }

  if (enabled && task->attribute->taskFunc) {
    queueAdd(task);
    slot->enabled = true;
  } else {
    queueRemove(task);
    slot->enabled = false;
  }
}

Task *Scheduler::getTask(TaskId taskId) {
  if (taskId < TaskId::Count && static_cast<size_t>(taskId) < taskCount) {
    return allSlots[static_cast<size_t>(taskId)].task;
  }
  return nullptr;
}

} // namespace ThetaGP::Gamepad::Scheduler
