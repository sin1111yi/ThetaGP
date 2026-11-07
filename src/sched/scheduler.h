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
 * You should have received a copy of the GNU General
 * Public License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "sched/core.h"

#include <cstddef>
#include <cstdint>

namespace ThetaGP::Sched {

// One microsecond period from a rate, for the call that registers a task.
constexpr uint32_t periodUs(uint32_t hz) { return 1000000U / hz; }

// One task as the diagnostics report it. The names are held here because they
// belong to the outer layer, not to the core.
struct TaskInfo {
  const char *name = nullptr;
  const char *sub = nullptr;
  uint32_t desired_period_us = 0;
  uint32_t avg_cycle_us = 0;
  uint32_t max_exec_us = 0;
  uint32_t avg_exec_us = 0;
  uint32_t total_exec_us = 0;
  uint32_t avg_delta_us = 0;
  uint32_t run_count = 0;
  uint32_t late_count = 0;
};

// The scheduler the firmware runs on: a core over the board's clock, with the
// task set registered before start() and frozen from there.
class Scheduler {
 public:
  static Scheduler &getInstance() {
    static Scheduler instance;
    return instance;
  }

  // Binds the board's clock and interrupt mask. Registration follows, then
  // start().
  bool init();

  bool add(const TaskSpec &spec, const char *sub = nullptr);
  bool addPeriodic(const char *name, const char *sub, void (*run)(void *),
                   void *context, uint32_t period_us, Priority priority,
                   uint32_t deadline_us = 0, uint32_t budget_us = 0,
                   MissPolicy miss = MissPolicy::Skip);
  bool addEvent(const char *name, const char *sub, void (*run)(void *),
                void *context, Priority priority, uint8_t max_pending = 1);

  // Places the first release of every periodic task and freezes the set.
  bool start();

  bool post(TaskIndex task);

  // Runs until the device is reset: every ready task, then the port's sleep.
  [[noreturn]] void run();

  // Share of the wall time spent in callbacks since the last call, as a
  // percentage. The window is the caller's polling interval.
  uint16_t loadPercent();

  std::size_t taskCount() const { return _core.task_count(); }
  const TaskInfo *taskInfo(TaskIndex task) const;

 private:
  Scheduler() = default;

  Core _core = {};
  const char *_subs[kMaxTasks] = {};
  uint32_t _last_load_us = 0;
  mutable TaskInfo _reported = {};
};

} // namespace ThetaGP::Sched
