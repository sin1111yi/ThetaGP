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

#include "sched/scheduler.h"

#include "build_info.h"

#include "sched/port.h"

namespace ThetaGP::Sched {

bool Scheduler::init() { return _core.setPort(systemPort()); }

bool Scheduler::add(const TaskSpec &spec, const char *sub) {
  TaskIndex index = kInvalidTask;
  if (_core.add(spec, &index) != AddError::None) {
    return false;
  }
  _subs[static_cast<std::size_t>(index)] = sub;
  return true;
}

bool Scheduler::addPeriodic(const char *name, const char *sub,
                            void (*run)(void *), void *context,
                            uint32_t periodUs, Priority priority,
                            uint32_t deadlineUs, uint32_t budgetUs,
                            MissPolicy miss) {
  TaskSpec spec;
  spec.name = name;
  spec.run = run;
  spec.context = context;
  spec.release = Release::Periodic;
  spec.priority = priority;
  spec.miss = miss;
  spec.periodUs = periodUs;
  spec.deadlineUs = deadlineUs;
  spec.budgetUs = budgetUs;
  return add(spec, sub);
}

bool Scheduler::addEvent(const char *name, const char *sub, void (*run)(void *),
                         void *context, Priority priority,
                         uint8_t maxPending) {
  TaskSpec spec;
  spec.name = name;
  spec.run = run;
  spec.context = context;
  spec.release = Release::Event;
  spec.priority = priority;
  spec.maxPending = maxPending;
  return add(spec, sub);
}

bool Scheduler::start() {
  if (!_core.start()) {
    return false;
  }
  _lastLoadUs = _core.nowUs();
  return true;
}

bool Scheduler::post(TaskIndex task) { return _core.post(task); }

FAST_CODE void Scheduler::run() {
  for (;;) {
    _core.pump();
  }
}

uint16_t Scheduler::loadPercent() {
  const uint32_t now = _core.nowUs();
  const uint32_t busy = _core.takeBusyUs();
  const uint32_t delta = now - _lastLoadUs;
  _lastLoadUs = now;
  if (delta == 0) {
    return 0;
  }
  const uint32_t percent = (100U * busy) / delta;
  return percent > 100U ? 100U : static_cast<uint16_t>(percent);
}

const TaskInfo *Scheduler::taskInfo(TaskIndex task) const {
  const TaskSpec *spec = _core.spec(task);
  const TaskStats *stats = _core.stats(task);
  if (spec == nullptr || stats == nullptr) {
    return nullptr;
  }

  TaskInfo &info = _reported;
  info.name = spec->name;
  info.sub = _subs[static_cast<std::size_t>(task)];
  info.desiredPeriodUs =
      spec->release == Release::Periodic ? spec->periodUs : 0;
  info.maxExecUs = stats->maxRuntimeUs;
  info.totalExecUs = stats->totalRuntimeUs;
  info.avgExecUs =
      stats->runs == 0 ? 0 : stats->totalRuntimeUs / stats->runs;
  const uint32_t elapsed = _core.nowUs() - _core.startedUs();
  info.avgDeltaUs = stats->runs == 0 ? 0 : elapsed / stats->runs;
  // The measured spacing between starts, so a task that is blocked or folded
  // reports the rate it actually ran at.
  info.avgCycleUs = stats->intervals == 0
                          ? info.desiredPeriodUs
                          : static_cast<uint32_t>(stats->sumIntervalUs / stats->intervals);
  info.runCount = stats->runs;
  info.lateCount = stats->deadlineMisses;
  return &info;
}

} // namespace ThetaGP::Sched
