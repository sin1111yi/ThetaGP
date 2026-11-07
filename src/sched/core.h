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
// max_pending of them.
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
  uint32_t period_us = 0;
  uint32_t phase_us = 0;
  // Relative to release. Zero takes the period for a periodic task and means
  // no check for an event task.
  uint32_t deadline_us = 0;
  // Declared upper bound of one run. Zero means no budget check.
  uint32_t budget_us = 0;
  // Event only: posts kept pending, 0 or 1 meaning that posts coalesce.
  uint8_t max_pending = 1;
};

// What the platform supplies. A member left null is not used, except now_us,
// which a core without it refuses to run.
struct Port {
  uint32_t (*now_us)(void *context) = nullptr;
  // Brackets the claim of a pending release; a single-core port may leave both
  // null when nothing posts from an interrupt.
  void (*enter_critical)(void *context) = nullptr;
  void (*exit_critical)(void *context) = nullptr;
  void *context = nullptr;
};

struct TaskStats {
  uint32_t releases = 0;
  uint32_t runs = 0;
  uint32_t missed_releases = 0;
  uint32_t deadline_misses = 0;
  uint32_t budget_overruns = 0;
  uint32_t max_start_latency_us = 0;
  uint32_t max_runtime_us = 0;
  uint32_t last_runtime_us = 0;
};

class Core {
 public:
  Core() = default;

  // Both may be called once, before start(). A port without now_us is refused.
  bool set_port(const Port &port);
  AddError add(const TaskSpec &spec, TaskIndex *index = nullptr);

  // Freezes the task set and places each periodic task's first release at
  // now + phase.
  bool start();
  bool started() const { return _started; }

  // An event release. post() brackets the claim against an interrupt; the
  // interrupt side calls post_from_isr() without that bracket.
  bool post(TaskIndex task);
  bool post_from_isr(TaskIndex task);

  // Collects the releases that came due and runs the highest-priority ready
  // task, at most one. Returns whether one ran.
  bool run_once();

  // Whether a task is ready now. When none is, next_wake_us writes the earliest
  // release the platform may sleep until.
  bool ready() const;
  bool next_wake_us(uint32_t *deadline_us) const;

  const TaskStats *stats(TaskIndex task) const;
  const TaskSpec *spec(TaskIndex task) const;
  std::size_t task_count() const { return _count; }

 private:
  struct State {
    TaskSpec spec = {};
    TaskStats stats = {};
    uint32_t next_release_us = 0;
    uint32_t first_pending_us = 0;
    uint8_t pending = 0;
  };

  State _tasks[kMaxTasks] = {};
  Port _port = {};
  std::size_t _count = 0;
  bool _started = false;

  bool valid(TaskIndex task) const {
    return task >= 0 && static_cast<std::size_t>(task) < _count;
  }
  void collect(State &task, uint32_t now);
  bool post_event(State &task, uint32_t now);
};

} // namespace ThetaGP::Sched
