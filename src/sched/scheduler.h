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
  uint32_t desiredPeriodUs = 0;
  uint32_t avgCycleUs = 0;
  uint32_t maxExecUs = 0;
  uint32_t avgExecUs = 0;
  uint32_t totalExecUs = 0;
  uint32_t avgDeltaUs = 0;
  uint32_t runCount = 0;
  uint32_t lateCount = 0;
  uint32_t clearanceOverruns = 0;
  uint32_t deferrals = 0;
  uint32_t missedReleases = 0;
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
                   void *context, uint32_t periodUs, Priority priority,
                   uint32_t deadlineUs = 0, uint32_t startClearanceUs = 0,
                   MissPolicy miss = MissPolicy::Skip);
  bool addEvent(const char *name, const char *sub, void (*run)(void *),
                void *context, Priority priority, uint8_t maxPending = 1);

  // Places the first release of every periodic task and freezes the set.
  bool start();

  bool post(TaskIndex task);
  // The same claim, made from an interrupt: the core brackets it with the
  // mask rather than a lock, so a nested interrupt cannot interleave it.
  bool postFromIsr(TaskIndex task);

  // Runs until the device is reset: every ready task, then the port's sleep.
  [[noreturn]] void run();

  // Share of the wall time spent in callbacks since the last call, as a
  // percentage. The window is the caller's polling interval.
  uint16_t loadPercent();

  std::size_t taskCount() const { return _core.taskCount(); }
  const TaskInfo *taskInfo(TaskIndex task) const;

private:
  Scheduler() = default;

  Core _core = {};
  const char *_subs[kMaxTasks] = {};
  uint64_t _lastLoadUptimeUs = 0;
  mutable TaskInfo _reported = {};
};

} // namespace ThetaGP::Sched
