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
  static void taskGamepadCore(uint32_t currentTimeUs);

public:
  static Mempool::PoolID getTaskPoolId() { return taskPoolId; }

  static void init();
  static void run();

  static TID createTask(const char *name, const char *subName, TaskFunc func,
                        uint32_t periodUs, TaskPriority priority);
  static void destroyTask(TID tid);

  static bool isValidTID(TID tid);
};

} // namespace ThetaGP::Gamepad
