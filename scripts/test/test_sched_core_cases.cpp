// This file is a part of ThetaGP.
//
// ThetaGP is free software: you can redistribute it
// and/or modify it under the terms of the GNU General
// Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// ThetaGP is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General
// Public License along with this program.
//
// If not, see <https://www.gnu.org/licenses/>.

// Cases: the platform-independent core, against a clock this file advances.
//
// A core has no timer of its own, so every case below moves a simulated clock
// and asks for one run. The cases pin the release rules (period, phase, event),
// the choice among ready tasks, the miss policies, the statistics and the
// comparison across the counter's wrap.

#include "sched/core.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {

using ThetaGP::Sched::AddError;
using ThetaGP::Sched::Core;
using ThetaGP::Sched::kInvalidTask;
using ThetaGP::Sched::MissPolicy;
using ThetaGP::Sched::Port;
using ThetaGP::Sched::Priority;
using ThetaGP::Sched::Release;
using ThetaGP::Sched::TaskIndex;
using ThetaGP::Sched::TaskSpec;

int gFailed = 0;
int gRan = 0;

void check(bool ok, const char *what) {
  ++gRan;
  if (!ok) {
    ++gFailed;
    std::printf("FAIL: %s\n", what);
  }
}

struct Clock {
  uint32_t now = 0;
};

uint32_t clockNow(void *context) { return static_cast<Clock *>(context)->now; }

void attach(Core &core, Clock &clock) {
  Port port;
  port.nowUs = clockNow;
  port.context = &clock;
  check(core.setPort(port), "the port is set");
}

struct Counted {
  Clock *clock = nullptr;
  int runs = 0;
  uint32_t advanceUs = 0;
};

void runCounted(void *context) {
  Counted *counted = static_cast<Counted *>(context);
  ++counted->runs;
  if (counted->clock != nullptr) {
    counted->clock->now += counted->advanceUs;
  }
}

struct SelfPost {
  Core *core = nullptr;
  TaskIndex self = kInvalidTask;
  int runs = 0;
};

void runSelfPost(void *context) {
  SelfPost *post = static_cast<SelfPost *>(context);
  ++post->runs;
  if (post->runs == 1) {
    post->core->post(post->self);
  }
}

TaskSpec periodic(const char *name, void *context, uint32_t periodUs,
                  Priority priority) {
  TaskSpec spec;
  spec.name = name;
  spec.run = runCounted;
  spec.context = context;
  spec.release = Release::Periodic;
  spec.priority = priority;
  spec.periodUs = periodUs;
  return spec;
}

void theTaskSetIsFrozenByStartAndFullAtItsCapacity() {
  Core noPort;
  Counted counted;
  TaskSpec spec = periodic("a", &counted, 1000, Priority::Normal);
  check(noPort.add(spec) == AddError::NoPort, "a core without a port takes none");

  Core core;
  Clock clock;
  attach(core, clock);

  TaskSpec invalid = spec;
  invalid.run = nullptr;
  check(core.add(invalid) == AddError::Invalid, "a task without a callback is refused");

  TaskSpec noPeriod = spec;
  noPeriod.periodUs = 0;
  check(core.add(noPeriod) == AddError::Invalid, "a periodic task without a period is refused");

  for (std::size_t i = 0; i < ThetaGP::Sched::kMaxTasks; ++i) {
    const AddError added = core.add(spec);
    check(added == AddError::None, "a task is taken while there is room");
  }
  check(core.add(spec) == AddError::Full, "the capacity is the task set's end");
  check(core.start(), "the set starts");
  check(core.add(spec) == AddError::Started, "a started set takes no more");
}

void aPeriodicTaskRunsOncePerReleaseInIndexOrder() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted slow;
  Counted fast;
  TaskIndex low = kInvalidTask;
  TaskIndex high = kInvalidTask;
  check(core.add(periodic("slow", &slow, 2000, Priority::Normal), &low) == AddError::None,
        "the slower task is added");
  check(core.add(periodic("fast", &fast, 1000, Priority::Normal), &high) == AddError::None,
        "the faster task is added");
  check(low == 0 && high == 1, "the indices follow the order they were added");
  check(core.start(), "the set starts");

  check(core.runOnce() && slow.runs == 1 && fast.runs == 0,
        "the lower index goes first at one priority");
  check(core.runOnce() && fast.runs == 1, "the other task of the same release follows");
  check(!core.runOnce(), "nothing else is due");

  clock.now = 1000;
  check(core.runOnce() && fast.runs == 2 && slow.runs == 1, "only the faster task is due at 1 ms");
  clock.now = 2000;
  check(core.runOnce() && slow.runs == 2, "the slower task is due at 2 ms");
}

void aHigherPriorityTaskIsChosenFirst() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted low;
  Counted high;
  check(core.add(periodic("low", &low, 1000, Priority::Low)) ==
            AddError::None,
        "the low task is added");
  check(core.add(periodic("high", &high, 1000, Priority::High)) ==
            AddError::None,
        "the high task is added");
  check(core.start(), "the set starts");

  check(core.runOnce() && high.runs == 1 && low.runs == 0, "the higher priority goes first");
  check(core.runOnce() && low.runs == 1, "the lower priority follows in the same release");
}

void aLateStartAndALongRunAreRecorded() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted counted;
  counted.clock = &clock;
  counted.advanceUs = 600;
  TaskSpec spec = periodic("late", &counted, 1000, Priority::Normal);
  spec.deadlineUs = 500;
  spec.budgetUs = 100;
  TaskIndex index = kInvalidTask;
  check(core.add(spec, &index) == AddError::None, "the task is added");
  check(core.start(), "the set starts");
  check(core.runOnce(), "the task runs");

  const ThetaGP::Sched::TaskStats *stats = core.stats(index);
  check(stats != nullptr && stats->runs == 1, "the run is counted");
  check(stats != nullptr && stats->deadlineMisses == 1, "a run past its deadline is counted");
  check(stats != nullptr && stats->budgetOverruns == 1, "a run past its budget is counted");
  check(stats != nullptr && stats->lastRuntimeUs == 600, "the run's length is recorded");
  check(core.stats(9) == nullptr, "an index the set does not carry has no statistics");
}

void postsCoalesceUnlessACountIsAskedFor() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted coalesced;
  TaskSpec spec = periodic("event", &coalesced, 0, Priority::Normal);
  spec.release = Release::Event;
  spec.maxPending = 0;
  TaskIndex index = kInvalidTask;
  check(core.add(spec, &index) == AddError::None, "a coalescing event task is added");
  check(core.start(), "the set starts");

  check(core.post(index), "the first post is taken");
  check(!core.post(index), "the next post is folded into it");
  check(core.stats(index)->missedReleases == 1, "the folded post is counted");
  check(core.runOnce() && coalesced.runs == 1, "one run covers the folded posts");
  check(!core.runOnce(), "nothing is left pending");

  Core counted;
  Clock clock2;
  attach(counted, clock2);
  Counted bounded;
  TaskSpec many = periodic("event", &bounded, 0, Priority::Normal);
  many.release = Release::Event;
  many.maxPending = 3;
  TaskIndex boundedIndex = kInvalidTask;
  check(counted.add(many, &boundedIndex) == AddError::None, "a counting event task is added");
  check(counted.start(), "the set starts");
  check(counted.post(boundedIndex) && counted.post(boundedIndex) && counted.post(boundedIndex),
        "three posts are taken");
  check(!counted.post(boundedIndex), "the fourth post overflows");
  check(counted.stats(boundedIndex)->missedReleases == 1, "the overflow is counted");
  int runs = 0;
  while (counted.runOnce()) {
    ++runs;
  }
  check(runs == 3 && bounded.runs == 3, "each taken post runs once");
}

void aPostInsideACallbackIsKept() {
  Core core;
  Clock clock;
  attach(core, clock);

  SelfPost self;
  self.core = &core;
  TaskSpec spec = periodic("self", &self, 0, Priority::Normal);
  spec.run = runSelfPost;
  spec.release = Release::Event;
  spec.maxPending = 0;
  TaskIndex index = kInvalidTask;
  check(core.add(spec, &index) == AddError::None, "the task is added");
  self.self = index;
  check(core.start(), "the set starts");

  check(core.post(index), "the first post is taken");
  check(core.runOnce() && self.runs == 1, "the task runs once");
  check(core.runOnce() && self.runs == 2, "the post made inside the run is still pending");
  check(!core.runOnce(), "and only once");
}

void aMissedPeriodKeepsOneReleaseOrUptoACount() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted skipped;
  TaskIndex skipIndex = kInvalidTask;
  check(core.add(periodic("skip", &skipped, 1000, Priority::Normal), &skipIndex) == AddError::None,
        "the skipping task is added");
  check(core.start(), "the set starts");
  check(core.runOnce(), "the first release runs");
  clock.now = 5000;
  check(core.runOnce() && skipped.runs == 2, "a long gap runs once");
  check(core.stats(skipIndex)->missedReleases == 4, "the four releases it skipped are counted");

  Core catching;
  Clock clock2;
  attach(catching, clock2);
  Counted caught;
  TaskSpec spec = periodic("catch", &caught, 1000, Priority::Normal);
  spec.miss = MissPolicy::CatchUp;
  spec.maxPending = 3;
  TaskIndex catchIndex = kInvalidTask;
  check(catching.add(spec, &catchIndex) == AddError::None, "the catching task is added");
  check(catching.start(), "the set starts");
  check(catching.runOnce(), "the first release runs");
  clock2.now = 5000;
  int runs = 0;
  while (catching.runOnce()) {
    ++runs;
  }
  check(runs == 3 && caught.runs == 4, "the backlog runs up to its count");
  check(catching.stats(catchIndex)->missedReleases == 2, "what the count does not hold is missed");
}

void theEarliestReleaseIsTheWakeTime() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted counted;
  check(core.add(periodic("a", &counted, 1000, Priority::Normal)) ==
            AddError::None,
        "the task is added");
  check(core.start(), "the set starts");
  check(core.ready(), "the first release is already due");
  check(!core.nextWakeUs(nullptr), "a due release is not a wake time");

  check(core.runOnce(), "the release runs");
  check(!core.ready(), "nothing is due now");
  uint32_t wake = 0;
  check(core.nextWakeUs(&wake) && wake == 1000, "the next release is the wake time");
}

void releasesAreComparedAcrossTheCounterWrap() {
  Core core;
  Clock clock;
  clock.now = 0xFFFFFF00u;
  attach(core, clock);

  Counted counted;
  check(core.add(periodic("wrap", &counted, 0x200, Priority::Normal)) == AddError::None,
        "the task is added");
  check(core.start(), "the set starts");
  check(core.runOnce() && counted.runs == 1, "the first release runs before the wrap");

  clock.now = 0x00000100u;
  check(core.runOnce() && counted.runs == 2, "the release after the wrap is due");

  clock.now = 0x00000300u;
  check(core.runOnce() && counted.runs == 3, "the next release is due too");
}

struct IdleHook {
  Clock clock;
  Core *core = nullptr;
  TaskIndex event = kInvalidTask;
  bool postOnce = false;
  bool fired = false;
};

uint32_t hookNow(void *context) {
  return static_cast<IdleHook *>(context)->clock.now;
}

void idleUntil(void *context, uint32_t deadlineUs) {
  IdleHook *hook = static_cast<IdleHook *>(context);
  if (hook->postOnce && !hook->fired) {
    hook->fired = true;
    hook->core->post(hook->event);
    return;
  }
  hook->clock.now = deadlineUs;
}

void thePumpSleepsToTheEarliestRelease() {
  Core core;
  IdleHook hook;

  Port port;
  port.nowUs = hookNow;
  port.idleUntilUs = idleUntil;
  port.context = &hook;
  check(core.setPort(port), "the port is set");

  Counted counted;
  check(core.add(periodic("a", &counted, 5000, Priority::Normal)) == AddError::None,
        "the task is added");
  check(core.start(), "the set starts");

  check(core.pump() && counted.runs == 1, "the first release runs");
  check(hook.clock.now == 5000, "the port slept to the release");
  check(core.pump() && counted.runs == 2, "the release the sleep reached runs");
}

void aPostMadeDuringTheSleepIsNotLost() {
  Core core;
  IdleHook hook;
  hook.core = &core;
  hook.postOnce = true;

  Port port;
  port.nowUs = hookNow;
  port.idleUntilUs = idleUntil;
  port.context = &hook;
  check(core.setPort(port), "the port is set");

  Counted counted;
  Counted event;
  TaskIndex eventIndex = kInvalidTask;
  check(core.add(periodic("tick", &counted, 10000, Priority::Normal)) == AddError::None,
        "the periodic task is added");
  TaskSpec spec = periodic("event", &event, 0, Priority::Normal);
  spec.release = Release::Event;
  check(core.add(spec, &eventIndex) == AddError::None, "the event task is added");
  hook.event = eventIndex;
  check(core.start(), "the set starts");

  check(core.pump() && counted.runs == 1, "the periodic task runs before the sleep");
  check(hook.clock.now == 0, "the sleep ended on the post, not on the deadline");
  check(core.pump() && event.runs == 1, "the post made during the sleep runs next");
}

void aTaskWaitsWhileAHigherReleaseIsTooNear() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted high;
  Counted low;
  TaskIndex highIndex = kInvalidTask;
  TaskIndex lowIndex = kInvalidTask;
  check(core.add(periodic("high", &high, 1000, Priority::High), &highIndex) ==
            AddError::None,
        "the high task is added");
  TaskSpec spec = periodic("low", &low, 100, Priority::Low);
  spec.budgetUs = 200;
  check(core.add(spec, &lowIndex) == AddError::None, "the budgeted task is added");
  check(core.start(), "the set starts");

  check(core.runOnce() && high.runs == 1, "the high task runs first");
  check(core.runOnce() && low.runs == 1, "the low task runs while the window is wide");

  clock.now = 900;
  check(!core.runOnce(), "the low task waits when the window is narrow");
  check(core.stats(lowIndex)->deferrals == 1, "the hold is counted");

  clock.now = 1000;
  check(core.runOnce() && high.runs == 2, "the high task runs at its release");
  check(core.runOnce() && low.runs == 2, "and the low task follows it");
}

void aSkippedBacklogKeepsTheNewestRelease() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted counted;
  TaskIndex index = kInvalidTask;
  check(core.add(periodic("skip", &counted, 1000, Priority::Normal), &index) ==
            AddError::None,
        "the task is added");
  check(core.start(), "the set starts");
  check(core.runOnce(), "the first release runs");

  clock.now = 5000;
  check(core.runOnce(), "the backlog runs once");
  check(core.stats(index)->maxStartLatencyUs == 0,
        "the run is measured from the newest release");
  check(core.stats(index)->missedReleases == 4, "the four it skipped are counted");
}

void aCaughtUpBacklogKeepsItsReleaseTimes() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted counted;
  TaskSpec spec = periodic("catch", &counted, 1000, Priority::Normal);
  spec.miss = MissPolicy::CatchUp;
  spec.maxPending = 3;
  TaskIndex index = kInvalidTask;
  check(core.add(spec, &index) == AddError::None, "the task is added");
  check(core.start(), "the set starts");
  check(core.runOnce(), "the first release runs");

  clock.now = 5000;
  for (int i = 0; i < 3; ++i) {
    check(core.runOnce(), "a backlogged release runs");
  }
  check(core.stats(index)->maxStartLatencyUs == 2000,
        "the oldest kept release sets the longest wait");
}

void aLargeBacklogReplacesAlreadyPendingReleases() {
  for (MissPolicy policy : {MissPolicy::Skip, MissPolicy::CatchUp}) {
    Core core;
    Clock clock;
    attach(core, clock);
    Counted high;
    Counted low;
    TaskSpec blocker = periodic("high", &high, 1000000, Priority::High);
    blocker.phaseUs = 0;
    TaskSpec spec = periodic("low", &low, 1000, Priority::Low);
    spec.miss = policy;
    spec.maxPending = 3;
    TaskIndex index = kInvalidTask;
    check(core.add(blocker) == AddError::None, "the blocker is added");
    check(core.add(spec, &index) == AddError::None, "the backlog task is added");
    check(core.start() && core.runOnce(), "the first release remains pending");
    clock.now = 300000;
    check(core.runOnce(), "the backlog runs");
    const uint32_t expected = policy == MissPolicy::Skip ? 0 : 2000;
    check(core.stats(index)->maxStartLatencyUs == expected,
          "the newest releases retain their timestamps after a large gap");
  }
}

struct AdvancingClock {
  uint32_t now = 0;
  bool advanceOnEnter = false;
};

uint32_t advancingNow(void *context) {
  return static_cast<AdvancingClock *>(context)->now;
}

void advanceOnEnter(void *context) {
  AdvancingClock *clock = static_cast<AdvancingClock *>(context);
  if (clock->advanceOnEnter) {
    clock->now = 1000;
  }
}

void exitNoop(void *) {}

void aReleaseDueDuringSelectionBlocksABudgetedTask() {
  Core core;
  AdvancingClock clock;
  Port port;
  port.nowUs = advancingNow;
  port.enterCritical = advanceOnEnter;
  port.exitCritical = exitNoop;
  port.context = &clock;
  check(core.setPort(port), "the advancing clock is set");
  Counted high;
  Counted low;
  TaskSpec highSpec = periodic("high", &high, 1000, Priority::High);
  highSpec.phaseUs = 1000;
  TaskSpec lowSpec = periodic("low", &low, 1000, Priority::Low);
  lowSpec.budgetUs = 10;
  check(core.add(highSpec) == AddError::None && core.add(lowSpec) == AddError::None,
        "the tasks are added");
  check(core.start(), "the set starts");
  clock.advanceOnEnter = true;
  check(!core.runOnce() && low.runs == 0, "a newly due higher release blocks the low task");
  clock.advanceOnEnter = false;
  check(core.runOnce() && high.runs == 1, "the higher task runs next");
}

void intervalTotalSurvives32BitOverflow() {
  Core core;
  Clock clock;
  attach(core, clock);
  Counted counted;
  TaskIndex index = kInvalidTask;
  check(core.add(periodic("slow", &counted, 1000000000, Priority::Normal),
                 &index) == AddError::None,
        "the slow task is added");
  check(core.start() && core.runOnce(), "the first release runs");
  for (int i = 0; i < 5; ++i) {
    clock.now += 1000000000;
    check(core.runOnce(), "the next release runs");
  }
  check(core.stats(index)->sumIntervalUs == 5000000000ULL,
        "the interval total exceeds 32 bits");
}

} // namespace

int main() {
  theTaskSetIsFrozenByStartAndFullAtItsCapacity();
  aPeriodicTaskRunsOncePerReleaseInIndexOrder();
  aHigherPriorityTaskIsChosenFirst();
  aLateStartAndALongRunAreRecorded();
  postsCoalesceUnlessACountIsAskedFor();
  aPostInsideACallbackIsKept();
  aMissedPeriodKeepsOneReleaseOrUptoACount();
  theEarliestReleaseIsTheWakeTime();
  releasesAreComparedAcrossTheCounterWrap();
  thePumpSleepsToTheEarliestRelease();
  aPostMadeDuringTheSleepIsNotLost();
  aTaskWaitsWhileAHigherReleaseIsTooNear();
  aSkippedBacklogKeepsTheNewestRelease();
  aCaughtUpBacklogKeepsItsReleaseTimes();
  aLargeBacklogReplacesAlreadyPendingReleases();
  aReleaseDueDuringSelectionBlocksABudgetedTask();
  intervalTotalSurvives32BitOverflow();

  std::printf("%d checks, %d failed\n", gRan, gFailed);
  return gFailed == 0 ? 0 : 1;
}
