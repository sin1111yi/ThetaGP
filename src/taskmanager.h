#pragma once

#include "gamepad/scheduler/scheduler.h"

#include "utils/mempool/mempoolmanager.h"

#include <cstddef>
#include <cstdint>

namespace ThetaGP::Gamepad {

using TID = int;

class TaskManager {
private:
  static constexpr size_t MAX_TASKS = 16;
  static constexpr size_t TASK_POOL_SIZE = 4096;

  struct TaskRecord {
    Task *task = nullptr;
    TaskAttribute *attribute = nullptr;
    bool inUse = false;
  };

  Scheduler *scheduler;
  Mempool::MempoolManager *mempoolManager;

  uint8_t taskPoolMemory[TASK_POOL_SIZE]{};

  TaskRecord records[MAX_TASKS]{};
  size_t taskCount = 0;
  uint16_t averageSystemLoadPercent = 0;

  TaskManager();

  static void taskSystemLoadStatic(uint32_t currentTimeUs);
  static void taskMainStatic(uint32_t currentTimeUs);
  static void taskGamepadCoreStatic(uint32_t currentTimeUs);

public:
  static TaskManager &getInstance() {
    static TaskManager instance;
    return instance;
  }

  void init();
  void run();

  TID createTask(const char *name, const char *subName, TaskFunc func,
                 uint32_t periodUs, TaskPriority priority);
  void destroyTask(TID tid);

  bool isValidTID(TID tid) const;
};

} // namespace ThetaGP::Gamepad
