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

#include "sched/core.h"

#include "build_info.h"

namespace ThetaGP::Sched {

namespace {

uint8_t pendingCap(const TaskSpec &spec) {
  if (spec.miss != MissPolicy::CatchUp) {
    return 1;
  }
  return spec.maxPending == 0 ? 1 : spec.maxPending;
}

} // namespace

bool Core::setPort(const Port &port) {
  if (_started || port.nowUs == nullptr) {
    return false;
  }
  _port = port;
  return true;
}

AddError Core::add(const TaskSpec &spec, TaskIndex *index) {
  if (_started) {
    return AddError::Started;
  }
  if (_port.nowUs == nullptr) {
    return AddError::NoPort;
  }
  if (spec.run == nullptr) {
    return AddError::Invalid;
  }
  if (spec.release == Release::Periodic && spec.periodUs == 0) {
    return AddError::Invalid;
  }
  if (_count >= kMaxTasks) {
    return AddError::Full;
  }

  State &task = _tasks[_count];
  task = State{};
  task.spec = spec;
  if (task.spec.release == Release::Periodic && task.spec.deadlineUs == 0) {
    task.spec.deadlineUs = task.spec.periodUs;
  }

  if (index != nullptr) {
    *index = static_cast<TaskIndex>(_count);
  }
  ++_count;
  return AddError::None;
}

bool Core::start() {
  if (_started || _port.nowUs == nullptr) {
    return false;
  }
  const uint32_t now = _port.nowUs(_port.context);
  for (std::size_t i = 0; i < _count; ++i) {
    State &task = _tasks[i];
    if (task.spec.release == Release::Periodic) {
      task.nextReleaseUs = now + task.spec.phaseUs;
    }
  }
  _lastNowUs = now;
  _uptimeUs = 0;
  _started = true;
  return true;
}

uint64_t Core::takeBusyUs() {
  const uint64_t busy = _busyUs;
  _busyUs = 0;
  return busy;
}

FAST_CODE void Core::collect(State &task, uint32_t now) {
  if (task.deferred && static_cast<int32_t>(now - task.deferUntilUs) >= 0) {
    task.deferred = false;
  }

  const uint32_t period = task.spec.periodUs;
  if (task.spec.release != Release::Periodic || period == 0) {
    return;
  }
  if (static_cast<int32_t>(now - task.nextReleaseUs) < 0) {
    return;
  }

  // Every release at or before now is due; one arriving after a long gap is
  // counted whole rather than walked one period at a time.
  const uint64_t due = static_cast<uint64_t>(now - task.nextReleaseUs) / period + 1;
  const uint32_t cap = pendingCap(task.spec);
  const uint64_t total = task.pending + due;
  const uint32_t kept = total < cap ? static_cast<uint32_t>(total) : cap;
  const uint64_t dropped = total - kept;
  if (dropped >= task.pending) {
    task.firstPendingUs = task.nextReleaseUs +
        static_cast<uint32_t>(dropped - task.pending) * period;
  } else {
    task.firstPendingUs += static_cast<uint32_t>(dropped) * period;
  }
  task.pending = static_cast<uint8_t>(kept);
  task.stats.releases += static_cast<uint32_t>(due);
  task.stats.missedReleases += static_cast<uint32_t>(dropped);
  task.nextReleaseUs += static_cast<uint32_t>(due * period);
}

FAST_CODE bool Core::postEvent(State &task, uint32_t now) {
  const uint8_t cap = task.spec.maxPending == 0 ? 1 : task.spec.maxPending;
  if (task.pending < cap) {
    if (task.pending == 0) {
      task.firstPendingUs = now;
    }
    ++task.pending;
    return true;
  }
  ++task.stats.missedReleases;
  return false;
}

FAST_CODE bool Core::post(TaskIndex task) {
  if (!_started || !valid(task)) {
    return false;
  }
  State &held = _tasks[task];
  if (held.spec.release != Release::Event) {
    return false;
  }
  if (_port.enterCritical != nullptr) {
    _port.enterCritical(_port.context);
  }
  const bool taken = postEvent(held, nowIsr());
  if (_port.exitCritical != nullptr) {
    _port.exitCritical(_port.context);
  }
  return taken;
}

FAST_CODE bool Core::postFromIsr(TaskIndex task) {
  if (!_started || !valid(task)) {
    return false;
  }
  State &held = _tasks[task];
  if (held.spec.release != Release::Event) {
    return false;
  }
  // The bracket is the same one the thread side takes: a nested interrupt that
  // posts the same task cannot interleave the count and the timestamp.
  if (_port.enterCritical != nullptr) {
    _port.enterCritical(_port.context);
  }
  const bool taken = postEvent(held, nowIsr());
  if (_port.exitCritical != nullptr) {
    _port.exitCritical(_port.context);
  }
  return taken;
}

FAST_CODE uint32_t Core::higherReleaseWindow(Priority priority, uint32_t now) const {
  uint32_t window = UINT32_MAX;
  for (std::size_t i = 0; i < _count; ++i) {
    const State &task = _tasks[i];
    if (task.spec.release != Release::Periodic) {
      continue;
    }
    if (static_cast<uint8_t>(task.spec.priority) <=
        static_cast<uint8_t>(priority)) {
      continue;
    }
    const int32_t delta = static_cast<int32_t>(task.nextReleaseUs - now);
    const uint32_t dueIn = delta > 0 ? static_cast<uint32_t>(delta) : 0;
    if (dueIn < window) {
      window = dueIn;
    }
  }
  return window;
}

FAST_CODE bool Core::runOnce() {
  if (!_started) {
    return false;
  }

  const uint32_t now = _port.nowUs(_port.context);
  _uptimeUs += now - _lastNowUs;
  _lastNowUs = now;
  for (std::size_t i = 0; i < _count; ++i) {
    collect(_tasks[i], now);
  }

  TaskIndex pick = kInvalidTask;
  Priority best = Priority::Idle;
  for (std::size_t i = 0; i < _count; ++i) {
    if (_tasks[i].pending == 0) {
      continue;
    }
    if (pick == kInvalidTask ||
        static_cast<uint8_t>(_tasks[i].spec.priority) > static_cast<uint8_t>(best)) {
      pick = static_cast<TaskIndex>(i);
      best = _tasks[i].spec.priority;
    }
  }
  if (pick == kInvalidTask) {
    return false;
  }

  State &task = _tasks[pick];
  if (_port.enterCritical != nullptr) {
    _port.enterCritical(_port.context);
  }
  const uint32_t started = nowIsr();
  if (task.spec.startClearanceUs != 0) {
    const uint32_t window = higherReleaseWindow(task.spec.priority, started);
    if (window != UINT32_MAX &&
        (window <= kStartGuardUs || task.spec.startClearanceUs >= window - kStartGuardUs)) {
      // One episode counts once: the run is held until the release it waits
      // on lands, and every attempt inside that wait is the same deferral.
      if (!task.deferred) {
        task.deferred = true;
        ++task.stats.deferrals;
      }
      task.deferUntilUs = started + window;
      if (_port.exitCritical != nullptr) {
        _port.exitCritical(_port.context);
      }
      return false;
    }
  }
  const uint32_t released = task.firstPendingUs;
  --task.pending;
  if (task.pending == 0) {
    task.firstPendingUs = 0;
  } else if (task.spec.release == Release::Periodic) {
    task.firstPendingUs += task.spec.periodUs;
  }
  if (_port.exitCritical != nullptr) {
    _port.exitCritical(_port.context);
  }

  const uint32_t latency = started - released;
  if (latency > task.stats.maxStartLatencyUs) {
    task.stats.maxStartLatencyUs = latency;
  }
  if (task.stats.runs > 0) {
    task.stats.sumIntervalUs += started - task.stats.lastStartUs;
    ++task.stats.intervals;
  }
  task.stats.lastStartUs = started;
  task.deferred = false;
  task.deferUntilUs = 0;
  ++task.stats.runs;
  task.spec.run(task.spec.context);

  const uint32_t finished = _port.nowUs(_port.context);
  const uint32_t runtime = finished - started;
  task.stats.lastRuntimeUs = runtime;
  task.stats.totalRuntimeUs += runtime;
  _busyUs += runtime;
  if (runtime > task.stats.maxRuntimeUs) {
    task.stats.maxRuntimeUs = runtime;
  }
  if (task.spec.startClearanceUs != 0 && runtime > task.spec.startClearanceUs) {
    ++task.stats.clearanceOverruns;
  }
  if (task.spec.deadlineUs != 0 && (finished - released) > task.spec.deadlineUs) {
    ++task.stats.deadlineMisses;
  }
  return true;
}

FAST_CODE bool Core::readyAt(uint32_t now) const {
  if (!_started) {
    return false;
  }
  for (std::size_t i = 0; i < _count; ++i) {
    const State &task = _tasks[i];
    if (task.pending > 0 && !task.deferred) {
      return true;
    }
    if (task.spec.release == Release::Periodic &&
        static_cast<int32_t>(now - task.nextReleaseUs) >= 0) {
      return true;
    }
  }
  return false;
}

bool Core::ready() const {
  return _started && readyAt(_port.nowUs(_port.context));
}

FAST_CODE bool Core::pump() {
  bool ran = false;
  while (runOnce()) {
    ran = true;
  }

  uint32_t wake = 0;
  if (_port.idleUntilUs == nullptr || !nextWakeUs(&wake)) {
    return ran;
  }

  // The ready check and the sleep are one step: a post that arrives between
  // them would otherwise be missed until the next interrupt.
  if (_port.enterCritical != nullptr) {
    _port.enterCritical(_port.context);
  }
  if (!readyAt(nowIsr())) {
    _port.idleUntilUs(_port.context, wake);
  }
  if (_port.exitCritical != nullptr) {
    _port.exitCritical(_port.context);
  }
  return ran;
}

FAST_CODE bool Core::nextWakeUs(uint32_t *deadlineUs) const {
  if (!_started) {
    return false;
  }
  const uint32_t now = _port.nowUs(_port.context);
  bool found = false;
  int32_t soonest = 0;
  uint32_t wake = 0;
  for (std::size_t i = 0; i < _count; ++i) {
    const State &task = _tasks[i];
    if (task.pending > 0) {
      if (!task.deferred) {
        return false;
      }
      // Held back by its clearance: the core may sleep until that release is
      // due, then this task gets its turn.
      const int32_t held = static_cast<int32_t>(task.deferUntilUs - now);
      const uint32_t until = held > 0 ? static_cast<uint32_t>(held) : 0;
      if (!found || until < static_cast<uint32_t>(soonest)) {
        found = true;
        soonest = static_cast<int32_t>(until);
        wake = task.deferUntilUs;
      }
      continue;
    }
    if (task.spec.release != Release::Periodic) {
      continue;
    }
    const int32_t delta = static_cast<int32_t>(task.nextReleaseUs - now);
    if (delta <= 0) {
      return false;
    }
    if (!found || delta < soonest) {
      found = true;
      soonest = delta;
      wake = task.nextReleaseUs;
    }
  }
  if (found && deadlineUs != nullptr) {
    *deadlineUs = wake;
  }
  return found;
}

const TaskStats *Core::stats(TaskIndex task) const {
  return valid(task) ? &_tasks[task].stats : nullptr;
}

const TaskSpec *Core::spec(TaskIndex task) const {
  return valid(task) ? &_tasks[task].spec : nullptr;
}

} // namespace ThetaGP::Sched
