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

namespace ThetaGP::Sched {

namespace {

uint8_t pending_cap(const TaskSpec &spec) {
  if (spec.miss != MissPolicy::CatchUp) {
    return 1;
  }
  return spec.max_pending == 0 ? 1 : spec.max_pending;
}

} // namespace

bool Core::set_port(const Port &port) {
  if (_started || port.now_us == nullptr) {
    return false;
  }
  _port = port;
  return true;
}

AddError Core::add(const TaskSpec &spec, TaskIndex *index) {
  if (_started) {
    return AddError::Started;
  }
  if (_port.now_us == nullptr) {
    return AddError::NoPort;
  }
  if (spec.run == nullptr) {
    return AddError::Invalid;
  }
  if (spec.release == Release::Periodic && spec.period_us == 0) {
    return AddError::Invalid;
  }
  if (_count >= kMaxTasks) {
    return AddError::Full;
  }

  State &task = _tasks[_count];
  task = State{};
  task.spec = spec;
  if (task.spec.release == Release::Periodic && task.spec.deadline_us == 0) {
    task.spec.deadline_us = task.spec.period_us;
  }

  if (index != nullptr) {
    *index = static_cast<TaskIndex>(_count);
  }
  ++_count;
  return AddError::None;
}

bool Core::start() {
  if (_started || _port.now_us == nullptr) {
    return false;
  }
  const uint32_t now = _port.now_us(_port.context);
  for (std::size_t i = 0; i < _count; ++i) {
    State &task = _tasks[i];
    if (task.spec.release == Release::Periodic) {
      task.next_release_us = now + task.spec.phase_us;
    }
  }
  _started_us = now;
  _started = true;
  return true;
}

uint32_t Core::take_busy_us() {
  const uint32_t busy = _busy_us;
  _busy_us = 0;
  return busy;
}

void Core::collect(State &task, uint32_t now) {
  const uint32_t period = task.spec.period_us;
  if (task.spec.release != Release::Periodic || period == 0) {
    return;
  }
  if (static_cast<int32_t>(now - task.next_release_us) < 0) {
    return;
  }

  // Every release at or before now is due; one arriving after a long gap is
  // counted whole rather than walked one period at a time.
  const uint64_t due = static_cast<uint64_t>(now - task.next_release_us) / period + 1;
  if (task.pending == 0) {
    task.first_pending_us = task.next_release_us;
  }

  const uint32_t room = kMaxPending - task.pending;
  const uint32_t kept = due < room ? static_cast<uint32_t>(due) : room;
  task.pending = static_cast<uint8_t>(task.pending + kept);
  task.stats.releases += static_cast<uint32_t>(due);
  task.stats.missed_releases += static_cast<uint32_t>(due) - kept;
  task.next_release_us += static_cast<uint32_t>(due * period);

  const uint8_t cap = pending_cap(task.spec);
  if (task.pending > cap) {
    task.stats.missed_releases += task.pending - cap;
    task.pending = cap;
  }
}

bool Core::post_event(State &task, uint32_t now) {
  const uint8_t cap = task.spec.max_pending == 0 ? 1 : task.spec.max_pending;
  if (task.pending < cap) {
    if (task.pending == 0) {
      task.first_pending_us = now;
    }
    ++task.pending;
    return true;
  }
  ++task.stats.missed_releases;
  return false;
}

bool Core::post(TaskIndex task) {
  if (!_started || !valid(task)) {
    return false;
  }
  State &held = _tasks[task];
  if (held.spec.release != Release::Event) {
    return false;
  }
  if (_port.enter_critical != nullptr) {
    _port.enter_critical(_port.context);
  }
  const bool taken = post_event(held, _port.now_us(_port.context));
  if (_port.exit_critical != nullptr) {
    _port.exit_critical(_port.context);
  }
  return taken;
}

bool Core::post_from_isr(TaskIndex task) {
  if (!_started || !valid(task)) {
    return false;
  }
  State &held = _tasks[task];
  if (held.spec.release != Release::Event) {
    return false;
  }
  return post_event(held, _port.now_us(_port.context));
}

bool Core::run_once() {
  if (!_started) {
    return false;
  }

  const uint32_t now = _port.now_us(_port.context);
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
  if (_port.enter_critical != nullptr) {
    _port.enter_critical(_port.context);
  }
  const uint32_t started = _port.now_us(_port.context);
  const uint32_t released = task.first_pending_us;
  --task.pending;
  if (task.pending == 0) {
    task.first_pending_us = 0;
  }
  if (_port.exit_critical != nullptr) {
    _port.exit_critical(_port.context);
  }

  const uint32_t latency = started - released;
  if (latency > task.stats.max_start_latency_us) {
    task.stats.max_start_latency_us = latency;
  }
  ++task.stats.runs;
  task.spec.run(task.spec.context);

  const uint32_t finished = _port.now_us(_port.context);
  const uint32_t runtime = finished - started;
  task.stats.last_runtime_us = runtime;
  task.stats.total_runtime_us += runtime;
  _busy_us += runtime;
  if (runtime > task.stats.max_runtime_us) {
    task.stats.max_runtime_us = runtime;
  }
  if (task.spec.budget_us != 0 && runtime > task.spec.budget_us) {
    ++task.stats.budget_overruns;
  }
  if (task.spec.deadline_us != 0 && (finished - released) > task.spec.deadline_us) {
    ++task.stats.deadline_misses;
  }
  return true;
}

bool Core::pump() {
  bool ran = false;
  while (run_once()) {
    ran = true;
  }
  uint32_t wake = 0;
  if (_port.idle_until_us != nullptr && next_wake_us(&wake)) {
    _port.idle_until_us(_port.context, wake);
  }
  return ran;
}

bool Core::ready() const {
  if (!_started) {
    return false;
  }
  const uint32_t now = _port.now_us(_port.context);
  for (std::size_t i = 0; i < _count; ++i) {
    const State &task = _tasks[i];
    if (task.pending > 0) {
      return true;
    }
    if (task.spec.release == Release::Periodic &&
        static_cast<int32_t>(now - task.next_release_us) >= 0) {
      return true;
    }
  }
  return false;
}

bool Core::next_wake_us(uint32_t *deadline_us) const {
  if (!_started) {
    return false;
  }
  const uint32_t now = _port.now_us(_port.context);
  bool found = false;
  int32_t soonest = 0;
  uint32_t wake = 0;
  for (std::size_t i = 0; i < _count; ++i) {
    const State &task = _tasks[i];
    if (task.pending > 0) {
      return false;
    }
    if (task.spec.release != Release::Periodic) {
      continue;
    }
    const int32_t delta = static_cast<int32_t>(task.next_release_us - now);
    if (delta <= 0) {
      return false;
    }
    if (!found || delta < soonest) {
      found = true;
      soonest = delta;
      wake = task.next_release_us;
    }
  }
  if (found && deadline_us != nullptr) {
    *deadline_us = wake;
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
