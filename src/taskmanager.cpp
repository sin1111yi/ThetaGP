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

#include "gamepad/scheduler/scheduler.h"
#include "utils/utils.h"

#include "taskmanager.h"

#include "ThetaGP.h"

#include "drivers/device/systimer.h"

#include "tusb.h"

#include "build_info.h"

namespace ThetaGP::Gamepad {

FAST_DATA_ZERO_INIT Scheduler *TaskManager::scheduler = nullptr;
FAST_DATA_ZERO_INIT Task TaskManager::taskSlots[MAX_TASKS]{};
TaskAttribute TaskManager::attrSlots[MAX_TASKS]{};
FAST_DATA_ZERO_INIT TaskManager::TaskRecord TaskManager::records[MAX_TASKS]{};
size_t TaskManager::taskCount = 0;
uint16_t TaskManager::averageSystemLoadPercent = 0;

void TaskManager::init() { scheduler = &Scheduler::getInstance(); }

void TaskManager::setupSysTasks() {
  TID loadTid = createTask("SYSTEM", "LOAD", taskSystemLoad, TASK_PERIOD_HZ(10),
                           TaskPriority::High);
  TID updateTid = createTask("SYSTEM", "UPDATE", taskMain, TASK_PERIOD_HZ(1000),
                             TaskPriority::High);

  if (isValidTID(loadTid))
    scheduler->queueAdd(records[loadTid].task);
  if (isValidTID(updateTid))
    scheduler->queueAdd(records[updateTid].task);
}

void TaskManager::setupScheduler() {
  scheduler->bindTimeBase(Drivers::Device::SystemTimer::getInstance());
  scheduler->init();
}

FAST_CODE void TaskManager::run() { scheduler->run(); }

TID TaskManager::createTask(const char *name, const char *subName,
                            TaskFunc func, uint32_t periodUs,
                            TaskPriority priority) {
  TID tid = INVALID_TID;
  for (uint32_t i = 0; i < MAX_TASKS; i++) {
    if (!records[i].inUse) {
      tid = static_cast<TID>(i);
      break;
    }
  }
  if (tid == INVALID_TID) {
    return INVALID_TID;
  }

  taskSlots[tid] = Task{};
  attrSlots[tid] = TaskAttribute{};

  TaskAttribute &attr = attrSlots[tid];
  attr.taskName = name;
  attr.subTaskName = subName;
  attr.checkFunc = nullptr;
  attr.taskFunc = func;
  attr.desiredPeriodUs = periodUs;
  attr.staticPriority = static_cast<int8_t>(priority);

  taskSlots[tid].attribute = &attr;

  records[tid].task = &taskSlots[tid];
  records[tid].attribute = &attr;
  records[tid].inUse = true;
  taskCount++;

  return tid;
}

void TaskManager::registerTask(const char *name, const char *subName,
                               TaskFunc func, uint32_t periodUs,
                               TaskPriority priority) {
  TID taskId = createTask(name, subName, func, periodUs, priority);
  if (isValidTID(taskId))
    scheduler->queueAdd(records[taskId].task);
}

void TaskManager::destroyTask(TID tid) {
  if (!isValidTID(tid)) {
    return;
  }

  TaskRecord &rec = records[tid];
  scheduler->queueRemove(rec.task);
  // The slot is released for reuse; its memory stays with the module.
  rec = TaskRecord{};
  taskCount--;
}

bool TaskManager::isValidTID(TID tid) {
  return tid >= 0 && static_cast<uint32_t>(tid) < MAX_TASKS &&
         records[tid].inUse;
}

const TaskInfo *TaskManager::getTaskInfo(TID tid) {
  if (!isValidTID(tid))
    return nullptr;
  Task *t = records[tid].task;
  TaskAttribute *a = records[tid].attribute;
  static TaskInfo info;
  info.taskName = a->taskName;
  info.subTaskName = a->subTaskName;
  info.isEnabled = (scheduler->queueContains(t));
  info.staticPriority = a->staticPriority;
  info.desiredPeriodUs = a->desiredPeriodUs;
  info.latestDeltaTimeUs = t->taskLatestDeltaTimeUs;
  info.maxExecutionTimeUs = t->maxExecutionTimeUs;
  info.totalExecutionTimeUs = t->totalExecutionTimeUs;
  info.averageExecutionTime10thUs = t->movingSumExecutionTime10thUs / TASK_STATS_MOVING_SUM_COUNT;
  info.averageDeltaTime10thUs = t->movingSumDeltaTime10thUs / TASK_STATS_MOVING_SUM_COUNT;
  info.movingAverageCycleTimeUs = t->movingAverageCycleTimeUs;
#ifdef USE_LATE_TASK_STATISTICS
  info.runCount = t->runCount;
  info.lateCount = t->lateCount;
#endif
  return &info;
}

FAST_CODE void TaskManager::taskSystemLoad(uint32_t currentTimeUs) {
  FAST_DATA_ZERO_INIT static uint32_t lastExecutedAtUs = 0;
  uint32_t deltaTime = currentTimeUs - lastExecutedAtUs;
  if (deltaTime) {
    uint32_t totalExecTime = scheduler->getAndResetTotalExecutionTime();
    averageSystemLoadPercent = 100 * totalExecTime / deltaTime;
    lastExecutedAtUs = currentTimeUs;
  } else {
    scheduler->ignoreTaskExecTime();
  }
}

FAST_CODE void TaskManager::taskMain(uint32_t currentTimeUs) { UNUSED(currentTimeUs); }

} // namespace ThetaGP::Gamepad
