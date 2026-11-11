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

#include <cstddef>
#include <cstdint>

namespace ThetaGP::Sched {

// A core holds at most this many tasks; one event task keeps at most
// kMaxPending posts, and a count above that is recorded as an overflow.
inline constexpr std::size_t kMaxTasks = 16;
inline constexpr uint8_t kMaxPending = 255;

// Added to a task's start clearance: the dispatch and the port's bookkeeping
// between the check and the callback's first instruction.
inline constexpr uint32_t kStartGuardUs = 32;

enum class Priority : uint8_t {
  Idle = 0,
  Low = 1,
  Normal = 2,
  High = 3,
  Realtime = 4,
};

enum class Release : uint8_t {
  Periodic,
  Event,
};

// How a periodic task treats releases that came due while it was not run.
// Skip keeps the newest one and folds the rest into it; CatchUp keeps up to
// maxPending of them.
enum class MissPolicy : uint8_t {
  Skip,
  CatchUp,
};

enum class AddError : uint8_t {
  None,
  NoPort,
  Invalid,
  Full,
  Started,
};

using TaskIndex = int8_t;
inline constexpr TaskIndex kInvalidTask = -1;

struct TaskSpec {
  const char *name = nullptr;
  void (*run)(void *context) = nullptr;
  void *context = nullptr;
  Release release = Release::Periodic;
  Priority priority = Priority::Normal;
  MissPolicy miss = MissPolicy::Skip;
  // Periodic only: the period and the offset of the first release.
  uint32_t periodUs = 0;
  uint32_t phaseUs = 0;
  // Relative to release. Zero takes the period for a periodic task and means
  // no check for an event task.
  uint32_t deadlineUs = 0;
  // The time that has to be clear before the next higher-priority periodic
  // release for a run to start. It gates the start and does not bound the run:
  // a run longer than it carries on across that release. Zero means the task
  // starts at any moment.
  uint32_t startClearanceUs = 0;
  // Event only: posts kept pending, 0 or 1 meaning that posts coalesce.
  uint8_t maxPending = 1;
};

// What the platform supplies. A member left null is not used, except nowUs,
// which a core without it refuses to run.
struct Port {
  uint32_t (*nowUs)(void *context) = nullptr;
  // The same clock read between enterCritical and exitCritical, where nowUs
  // may wait on the very interrupt it has masked. Null falls back to nowUs.
  uint32_t (*nowUsIsr)(void *context) = nullptr;
  // Brackets the claim of a pending release and the check before an idle. A
  // single-core port may leave both null when nothing posts from an interrupt.
  void (*enterCritical)(void *context) = nullptr;
  void (*exitCritical)(void *context) = nullptr;
  // Sleeps until the deadline and returns early on an interrupt. A port
  // without it leaves the caller to poll.
  void (*idleUntilUs)(void *context, uint32_t deadlineUs) = nullptr;
  void *context = nullptr;
};

struct TaskStats {
  uint32_t releases = 0;
  uint32_t runs = 0;
  uint32_t missedReleases = 0;
  uint32_t deadlineMisses = 0;
  // Runs that lasted longer than the clearance they started under.
  uint32_t clearanceOverruns = 0;
  uint32_t maxStartLatencyUs = 0;
  uint32_t maxRuntimeUs = 0;
  uint32_t lastRuntimeUs = 0;
  uint64_t totalRuntimeUs = 0;
  // Start attempts held back because a higher-priority release was nearer
  // than the clearance. One held run counts every attempt made while it waits.
  uint32_t deferrals = 0;
  // The time between consecutive starts, summed over the intervals counted.
  uint64_t sumIntervalUs = 0;
  uint32_t intervals = 0;
  uint32_t lastStartUs = 0;
};

class Core {
public:
  Core() = default;

  // Both may be called once, before start(). A port without nowUs is refused.
  bool setPort(const Port &port);
  AddError add(const TaskSpec &spec, TaskIndex *index = nullptr);

  // Freezes the task set and places each periodic task's first release at
  // now + phase.
  bool start();
  bool started() const { return _started; }

  // An event release. post() brackets the claim against an interrupt; the
  // interrupt side calls postFromIsr(), which brackets it the same way so a
  // nested interrupt cannot interleave the update.
  bool post(TaskIndex task);
  bool postFromIsr(TaskIndex task);

  // Collects the releases that came due and runs the highest-priority ready
  // task, at most one. Returns whether one ran.
  bool runOnce();

  // Runs every ready task, then asks the port to sleep until the earliest
  // release. Returns whether a task ran. A post that arrives while the port
  // sleeps ends the sleep, so the next call sees it.
  bool pump();

  // Whether a task is ready now. When none is, nextWakeUs writes the earliest
  // release the platform may sleep until.
  bool ready() const;
  bool nextWakeUs(uint32_t *deadlineUs) const;

  const TaskStats *stats(TaskIndex task) const;
  const TaskSpec *spec(TaskIndex task) const;
  std::size_t taskCount() const { return _count; }

  // The port's clock, the microseconds since start() as runOnce() has counted
  // them, and the microseconds spent in callbacks since the last call of
  // takeBusyUs(). The two counts do not wrap with the clock.
  uint32_t nowUs() const { return _port.nowUs(_port.context); }
  uint64_t uptimeUs() const { return _uptimeUs; }
  uint64_t takeBusyUs();

private:
  struct State {
    TaskSpec spec = {};
    TaskStats stats = {};
    uint32_t nextReleaseUs = 0;
    uint32_t firstPendingUs = 0;
    uint8_t pending = 0;
  };

  State _tasks[kMaxTasks] = {};
  Port _port = {};
  std::size_t _count = 0;
  bool _started = false;
  uint32_t _lastNowUs = 0;
  uint64_t _uptimeUs = 0;
  uint64_t _busyUs = 0;

  bool valid(TaskIndex task) const {
    return task >= 0 && static_cast<std::size_t>(task) < _count;
  }
  uint32_t nowIsr() const {
    return _port.nowUsIsr != nullptr ? _port.nowUsIsr(_port.context)
                                       : _port.nowUs(_port.context);
  }
  bool readyAt(uint32_t now) const;
  uint32_t higherReleaseWindow(Priority priority, uint32_t now) const;
  void collect(State &task, uint32_t now);
  bool postEvent(State &task, uint32_t now);
};

} // namespace ThetaGP::Sched
