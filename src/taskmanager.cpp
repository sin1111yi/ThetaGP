#include "taskmanager.h"

#include "ThetaGP.h"

#include "drivers/device/systimer.h"
#include "tusb.h"

#include <cstring>

namespace ThetaGP::Gamepad {

TaskManager::TaskManager() {
  scheduler = &Scheduler::getInstance();
  mempoolManager = &Mempool::MempoolManager::getInstance();
}

void TaskManager::init() {
  std::memset(taskPoolMemory, 0, sizeof(taskPoolMemory));
  mempoolManager->addPool(taskPoolMemory, TASK_POOL_SIZE, "task");

  TID loadTid = createTask("SYSTEM", "LOAD", taskSystemLoadStatic,
                           TASK_PERIOD_HZ(10), TaskPriority::High);
  TID updateTid = createTask("SYSTEM", "UPDATE", taskMainStatic,
                             TASK_PERIOD_HZ(1000), TaskPriority::High);
  TID coreTid = createTask("GAMEPAD", "CORE", taskGamepadCoreStatic,
                           TASK_PERIOD_HZ(1000), TaskPriority::Realtime);

  if (isValidTID(loadTid))
    scheduler->queueAdd(records[loadTid].task);
  if (isValidTID(updateTid))
    scheduler->queueAdd(records[updateTid].task);
  if (isValidTID(coreTid))
    scheduler->queueAdd(records[coreTid].task);

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

  Task *task = static_cast<Task *>(mempoolManager->alloc("task", sizeof(Task)));
  if (!task) {
    return -1;
  }

  TaskAttribute *attr =
      static_cast<TaskAttribute *>(mempoolManager->alloc("task", sizeof(TaskAttribute)));
  if (!attr) {
    mempoolManager->free("task", task);
    return -1;
  }

  std::memset(task, 0, sizeof(Task));
  std::memset(attr, 0, sizeof(TaskAttribute));

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

void TaskManager::destroyTask(TID tid) {
  if (!isValidTID(tid)) {
    return;
  }

  TaskRecord &rec = records[tid];
  scheduler->queueRemove(rec.task);
  mempoolManager->free("task", rec.attribute);
  mempoolManager->free("task", rec.task);

  rec.task = nullptr;
  rec.attribute = nullptr;
  rec.inUse = false;

  for (size_t i = static_cast<size_t>(tid); i < taskCount - 1; i++) {
    records[i] = records[i + 1];
  }
  records[taskCount - 1] = TaskRecord{};
  taskCount--;
}

bool TaskManager::isValidTID(TID tid) const {
  return tid >= 0 && static_cast<size_t>(tid) < taskCount && records[tid].inUse;
}

void TaskManager::taskSystemLoadStatic(uint32_t currentTimeUs) {
  static uint32_t lastExecutedAtUs = 0;
  TaskManager &self = getInstance();
  uint32_t deltaTime = currentTimeUs - lastExecutedAtUs;
  if (deltaTime) {
    uint32_t totalExecTime = self.scheduler->getAndResetTotalExecutionTime();
    self.averageSystemLoadPercent = 100 * totalExecTime / deltaTime;
    lastExecutedAtUs = currentTimeUs;
  } else {
    self.scheduler->ignoreTaskExecTime();
  }
}

void TaskManager::taskMainStatic(uint32_t currentTimeUs) {
  (void)currentTimeUs;
}

void TaskManager::taskGamepadCoreStatic(uint32_t currentTimeUs) {
  (void)currentTimeUs;
  ThetaGamepad &theta = ThetaGamepad::getInstance();
  theta.gamepad.process();
  theta.gpDriverManager.getgpdriverDevice()->process(&theta.gamepad);
  tud_task();
}

} // namespace ThetaGP::Gamepad
