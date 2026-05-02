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

#include <cstring>

namespace ThetaGP::Gamepad {

Scheduler *TaskManager::scheduler = nullptr;
Mempool::PoolID TaskManager::taskPoolId = Mempool::INVALID_POOL_ID;
uint8_t TaskManager::taskPoolMemory[TASK_POOL_SIZE]{};
TaskManager::TaskRecord TaskManager::records[MAX_TASKS]{};
size_t TaskManager::taskCount = 0;
uint16_t TaskManager::averageSystemLoadPercent = 0;

void TaskManager::init() {
  scheduler = &Scheduler::getInstance();

  std::memset(taskPoolMemory, 0, sizeof(taskPoolMemory));
  taskPoolId = Mempool::MempoolManager::createPool(taskPoolMemory,
                                                   TASK_POOL_SIZE, "task");
}

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

void TaskManager::run() { scheduler->run(); }

TID TaskManager::createTask(const char *name, const char *subName,
                            TaskFunc func, uint32_t periodUs,
                            TaskPriority priority) {
  if (taskCount >= MAX_TASKS) {
    return -1;
  }

  Task *task = static_cast<Task *>(
      Mempool::MempoolManager::alloc(taskPoolId, sizeof(Task)));
  if (!task) {
    return -1;
  }

  TaskAttribute *attr = static_cast<TaskAttribute *>(
      Mempool::MempoolManager::alloc(taskPoolId, sizeof(TaskAttribute)));
  if (!attr) {
    Mempool::MempoolManager::free(taskPoolId, task);
    return -1;
  }

  *task = Task{};
  *attr = TaskAttribute{};

  attr->taskName = name;
  attr->subTaskName = subName;
  attr->checkFunc = nullptr;
  attr->taskFunc = func;
  attr->desiredPeriodUs = periodUs;
  attr->staticPriority = static_cast<int8_t>(priority);

  task->attribute = attr;

  TID tid = static_cast<TID>(taskCount);
  records[tid].task = task;
  records[tid].attribute = attr;
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
  Mempool::MempoolManager::free(taskPoolId, rec.attribute);
  Mempool::MempoolManager::free(taskPoolId, rec.task);

  rec.task = nullptr;
  rec.attribute = nullptr;
  rec.inUse = false;

  for (size_t i = static_cast<size_t>(tid); i < taskCount - 1; i++) {
    records[i] = records[i + 1];
  }
  records[taskCount - 1] = TaskRecord{};
  taskCount--;
}

bool TaskManager::isValidTID(TID tid) {
  return tid >= 0 && static_cast<size_t>(tid) < taskCount && records[tid].inUse;
}

void TaskManager::taskSystemLoad(uint32_t currentTimeUs) {
  static uint32_t lastExecutedAtUs = 0;
  uint32_t deltaTime = currentTimeUs - lastExecutedAtUs;
  if (deltaTime) {
    uint32_t totalExecTime = scheduler->getAndResetTotalExecutionTime();
    averageSystemLoadPercent = 100 * totalExecTime / deltaTime;
    lastExecutedAtUs = currentTimeUs;
  } else {
    scheduler->ignoreTaskExecTime();
  }
}

void TaskManager::taskMain(uint32_t currentTimeUs) { UNUSED(currentTimeUs); }

} // namespace ThetaGP::Gamepad
