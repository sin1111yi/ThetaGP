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

int g_failed = 0;
int g_ran = 0;

void check(bool ok, const char *what) {
  ++g_ran;
  if (!ok) {
    ++g_failed;
    std::printf("FAIL: %s\n", what);
  }
}

struct Clock {
  uint32_t now = 0;
};

uint32_t clock_now(void *context) { return static_cast<Clock *>(context)->now; }

void attach(Core &core, Clock &clock) {
  Port port;
  port.now_us = clock_now;
  port.context = &clock;
  check(core.set_port(port), "the port is set");
}

struct Counted {
  Clock *clock = nullptr;
  int runs = 0;
  uint32_t advance_us = 0;
};

void run_counted(void *context) {
  Counted *counted = static_cast<Counted *>(context);
  ++counted->runs;
  if (counted->clock != nullptr) {
    counted->clock->now += counted->advance_us;
  }
}

struct SelfPost {
  Core *core = nullptr;
  TaskIndex self = kInvalidTask;
  int runs = 0;
};

void run_self_post(void *context) {
  SelfPost *post = static_cast<SelfPost *>(context);
  ++post->runs;
  if (post->runs == 1) {
    post->core->post(post->self);
  }
}

TaskSpec periodic(const char *name, void *context, uint32_t period_us,
                  Priority priority) {
  TaskSpec spec;
  spec.name = name;
  spec.run = run_counted;
  spec.context = context;
  spec.release = Release::Periodic;
  spec.priority = priority;
  spec.period_us = period_us;
  return spec;
}

void the_task_set_is_frozen_by_start_and_full_at_its_capacity() {
  Core no_port;
  Counted counted;
  TaskSpec spec = periodic("a", &counted, 1000, Priority::Normal);
  check(no_port.add(spec) == AddError::NoPort, "a core without a port takes none");

  Core core;
  Clock clock;
  attach(core, clock);

  TaskSpec invalid = spec;
  invalid.run = nullptr;
  check(core.add(invalid) == AddError::Invalid, "a task without a callback is refused");

  TaskSpec no_period = spec;
  no_period.period_us = 0;
  check(core.add(no_period) == AddError::Invalid, "a periodic task without a period is refused");

  for (std::size_t i = 0; i < ThetaGP::Sched::kMaxTasks; ++i) {
    const AddError added = core.add(spec);
    check(added == AddError::None, "a task is taken while there is room");
  }
  check(core.add(spec) == AddError::Full, "the capacity is the task set's end");
  check(core.start(), "the set starts");
  check(core.add(spec) == AddError::Started, "a started set takes no more");
}

void a_periodic_task_runs_once_per_release_in_index_order() {
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

  check(core.run_once() && slow.runs == 1 && fast.runs == 0,
        "the lower index goes first at one priority");
  check(core.run_once() && fast.runs == 1, "the other task of the same release follows");
  check(!core.run_once(), "nothing else is due");

  clock.now = 1000;
  check(core.run_once() && fast.runs == 2 && slow.runs == 1, "only the faster task is due at 1 ms");
  clock.now = 2000;
  check(core.run_once() && slow.runs == 2, "the slower task is due at 2 ms");
}

void a_higher_priority_task_is_chosen_first() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted low;
  Counted high;
  check(core.add(periodic("low", &low, 1000, Priority::Low)) == AddError::None, "the low task is added");
  check(core.add(periodic("high", &high, 1000, Priority::High)) == AddError::None, "the high task is added");
  check(core.start(), "the set starts");

  check(core.run_once() && high.runs == 1 && low.runs == 0, "the higher priority goes first");
  check(core.run_once() && low.runs == 1, "the lower priority follows in the same release");
}

void a_late_start_and_a_long_run_are_recorded() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted counted;
  counted.clock = &clock;
  counted.advance_us = 600;
  TaskSpec spec = periodic("late", &counted, 1000, Priority::Normal);
  spec.deadline_us = 500;
  spec.budget_us = 100;
  TaskIndex index = kInvalidTask;
  check(core.add(spec, &index) == AddError::None, "the task is added");
  check(core.start(), "the set starts");
  check(core.run_once(), "the task runs");

  const ThetaGP::Sched::TaskStats *stats = core.stats(index);
  check(stats != nullptr && stats->runs == 1, "the run is counted");
  check(stats != nullptr && stats->deadline_misses == 1, "a run past its deadline is counted");
  check(stats != nullptr && stats->budget_overruns == 1, "a run past its budget is counted");
  check(stats != nullptr && stats->last_runtime_us == 600, "the run's length is recorded");
  check(core.stats(9) == nullptr, "an index the set does not carry has no statistics");
}

void posts_coalesce_unless_a_count_is_asked_for() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted coalesced;
  TaskSpec spec = periodic("event", &coalesced, 0, Priority::Normal);
  spec.release = Release::Event;
  spec.max_pending = 0;
  TaskIndex index = kInvalidTask;
  check(core.add(spec, &index) == AddError::None, "a coalescing event task is added");
  check(core.start(), "the set starts");

  check(core.post(index), "the first post is taken");
  check(!core.post(index), "the next post is folded into it");
  check(core.stats(index)->missed_releases == 1, "the folded post is counted");
  check(core.run_once() && coalesced.runs == 1, "one run covers the folded posts");
  check(!core.run_once(), "nothing is left pending");

  Core counted;
  Clock clock2;
  attach(counted, clock2);
  Counted bounded;
  TaskSpec many = periodic("event", &bounded, 0, Priority::Normal);
  many.release = Release::Event;
  many.max_pending = 3;
  TaskIndex bounded_index = kInvalidTask;
  check(counted.add(many, &bounded_index) == AddError::None, "a counting event task is added");
  check(counted.start(), "the set starts");
  check(counted.post(bounded_index) && counted.post(bounded_index) && counted.post(bounded_index),
        "three posts are taken");
  check(!counted.post(bounded_index), "the fourth post overflows");
  check(counted.stats(bounded_index)->missed_releases == 1, "the overflow is counted");
  int runs = 0;
  while (counted.run_once()) {
    ++runs;
  }
  check(runs == 3 && bounded.runs == 3, "each taken post runs once");
}

void a_post_inside_a_callback_is_kept() {
  Core core;
  Clock clock;
  attach(core, clock);

  SelfPost self;
  self.core = &core;
  TaskSpec spec = periodic("self", &self, 0, Priority::Normal);
  spec.run = run_self_post;
  spec.release = Release::Event;
  spec.max_pending = 0;
  TaskIndex index = kInvalidTask;
  check(core.add(spec, &index) == AddError::None, "the task is added");
  self.self = index;
  check(core.start(), "the set starts");

  check(core.post(index), "the first post is taken");
  check(core.run_once() && self.runs == 1, "the task runs once");
  check(core.run_once() && self.runs == 2, "the post made inside the run is still pending");
  check(!core.run_once(), "and only once");
}

void a_missed_period_keeps_one_release_or_upto_a_count() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted skipped;
  TaskIndex skip_index = kInvalidTask;
  check(core.add(periodic("skip", &skipped, 1000, Priority::Normal), &skip_index) == AddError::None,
        "the skipping task is added");
  check(core.start(), "the set starts");
  check(core.run_once(), "the first release runs");
  clock.now = 5000;
  check(core.run_once() && skipped.runs == 2, "a long gap runs once");
  check(core.stats(skip_index)->missed_releases == 4, "the four releases it skipped are counted");

  Core catching;
  Clock clock2;
  attach(catching, clock2);
  Counted caught;
  TaskSpec spec = periodic("catch", &caught, 1000, Priority::Normal);
  spec.miss = MissPolicy::CatchUp;
  spec.max_pending = 3;
  TaskIndex catch_index = kInvalidTask;
  check(catching.add(spec, &catch_index) == AddError::None, "the catching task is added");
  check(catching.start(), "the set starts");
  check(catching.run_once(), "the first release runs");
  clock2.now = 5000;
  int runs = 0;
  while (catching.run_once()) {
    ++runs;
  }
  check(runs == 3 && caught.runs == 4, "the backlog runs up to its count");
  check(catching.stats(catch_index)->missed_releases == 2, "what the count does not hold is missed");
}

void the_earliest_release_is_the_wake_time() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted counted;
  check(core.add(periodic("a", &counted, 1000, Priority::Normal)) == AddError::None, "the task is added");
  check(core.start(), "the set starts");
  check(core.ready(), "the first release is already due");
  check(!core.next_wake_us(nullptr), "a due release is not a wake time");

  check(core.run_once(), "the release runs");
  check(!core.ready(), "nothing is due now");
  uint32_t wake = 0;
  check(core.next_wake_us(&wake) && wake == 1000, "the next release is the wake time");
}

void releases_are_compared_across_the_counter_wrap() {
  Core core;
  Clock clock;
  clock.now = 0xFFFFFF00u;
  attach(core, clock);

  Counted counted;
  check(core.add(periodic("wrap", &counted, 0x200, Priority::Normal)) == AddError::None,
        "the task is added");
  check(core.start(), "the set starts");
  check(core.run_once() && counted.runs == 1, "the first release runs before the wrap");

  clock.now = 0x00000100u;
  check(core.run_once() && counted.runs == 2, "the release after the wrap is due");

  clock.now = 0x00000300u;
  check(core.run_once() && counted.runs == 3, "the next release is due too");
}

struct IdleHook {
  Clock clock;
  Core *core = nullptr;
  TaskIndex event = kInvalidTask;
  bool post_once = false;
  bool fired = false;
};

uint32_t hook_now(void *context) {
  return static_cast<IdleHook *>(context)->clock.now;
}

void idle_until(void *context, uint32_t deadline_us) {
  IdleHook *hook = static_cast<IdleHook *>(context);
  if (hook->post_once && !hook->fired) {
    hook->fired = true;
    hook->core->post(hook->event);
    return;
  }
  hook->clock.now = deadline_us;
}

void the_pump_sleeps_to_the_earliest_release() {
  Core core;
  IdleHook hook;

  Port port;
  port.now_us = hook_now;
  port.idle_until_us = idle_until;
  port.context = &hook;
  check(core.set_port(port), "the port is set");

  Counted counted;
  check(core.add(periodic("a", &counted, 5000, Priority::Normal)) == AddError::None,
        "the task is added");
  check(core.start(), "the set starts");

  check(core.pump() && counted.runs == 1, "the first release runs");
  check(hook.clock.now == 5000, "the port slept to the release");
  check(core.pump() && counted.runs == 2, "the release the sleep reached runs");
}

void a_post_made_during_the_sleep_is_not_lost() {
  Core core;
  IdleHook hook;
  hook.core = &core;
  hook.post_once = true;

  Port port;
  port.now_us = hook_now;
  port.idle_until_us = idle_until;
  port.context = &hook;
  check(core.set_port(port), "the port is set");

  Counted counted;
  Counted event;
  TaskIndex event_index = kInvalidTask;
  check(core.add(periodic("tick", &counted, 10000, Priority::Normal)) == AddError::None,
        "the periodic task is added");
  TaskSpec spec = periodic("event", &event, 0, Priority::Normal);
  spec.release = Release::Event;
  check(core.add(spec, &event_index) == AddError::None, "the event task is added");
  hook.event = event_index;
  check(core.start(), "the set starts");

  check(core.pump() && counted.runs == 1, "the periodic task runs before the sleep");
  check(hook.clock.now == 0, "the sleep ended on the post, not on the deadline");
  check(core.pump() && event.runs == 1, "the post made during the sleep runs next");
}

void a_task_waits_while_a_higher_release_is_too_near() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted high;
  Counted low;
  TaskIndex high_index = kInvalidTask;
  TaskIndex low_index = kInvalidTask;
  check(core.add(periodic("high", &high, 1000, Priority::High), &high_index) ==
            AddError::None,
        "the high task is added");
  TaskSpec spec = periodic("low", &low, 100, Priority::Low);
  spec.budget_us = 200;
  check(core.add(spec, &low_index) == AddError::None, "the budgeted task is added");
  check(core.start(), "the set starts");

  check(core.run_once() && high.runs == 1, "the high task runs first");
  check(core.run_once() && low.runs == 1, "the low task runs while the window is wide");

  clock.now = 900;
  check(!core.run_once(), "the low task waits when the window is narrow");
  check(core.stats(low_index)->deferrals == 1, "the hold is counted");

  clock.now = 1000;
  check(core.run_once() && high.runs == 2, "the high task runs at its release");
  check(core.run_once() && low.runs == 2, "and the low task follows it");
}

void a_skipped_backlog_keeps_the_newest_release() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted counted;
  TaskIndex index = kInvalidTask;
  check(core.add(periodic("skip", &counted, 1000, Priority::Normal), &index) ==
            AddError::None,
        "the task is added");
  check(core.start(), "the set starts");
  check(core.run_once(), "the first release runs");

  clock.now = 5000;
  check(core.run_once(), "the backlog runs once");
  check(core.stats(index)->max_start_latency_us == 0,
        "the run is measured from the newest release");
  check(core.stats(index)->missed_releases == 4, "the four it skipped are counted");
}

void a_caught_up_backlog_keeps_its_release_times() {
  Core core;
  Clock clock;
  attach(core, clock);

  Counted counted;
  TaskSpec spec = periodic("catch", &counted, 1000, Priority::Normal);
  spec.miss = MissPolicy::CatchUp;
  spec.max_pending = 3;
  TaskIndex index = kInvalidTask;
  check(core.add(spec, &index) == AddError::None, "the task is added");
  check(core.start(), "the set starts");
  check(core.run_once(), "the first release runs");

  clock.now = 5000;
  for (int i = 0; i < 3; ++i) {
    check(core.run_once(), "a backlogged release runs");
  }
  check(core.stats(index)->max_start_latency_us == 2000,
        "the oldest kept release sets the longest wait");
}

void a_large_backlog_replaces_already_pending_releases() {
  for (MissPolicy policy : {MissPolicy::Skip, MissPolicy::CatchUp}) {
    Core core;
    Clock clock;
    attach(core, clock);
    Counted high;
    Counted low;
    TaskSpec blocker = periodic("high", &high, 1000000, Priority::High);
    blocker.phase_us = 0;
    TaskSpec spec = periodic("low", &low, 1000, Priority::Low);
    spec.miss = policy;
    spec.max_pending = 3;
    TaskIndex index = kInvalidTask;
    check(core.add(blocker) == AddError::None, "the blocker is added");
    check(core.add(spec, &index) == AddError::None, "the backlog task is added");
    check(core.start() && core.run_once(), "the first release remains pending");
    clock.now = 300000;
    check(core.run_once(), "the backlog runs");
    const uint32_t expected = policy == MissPolicy::Skip ? 0 : 2000;
    check(core.stats(index)->max_start_latency_us == expected,
          "the newest releases retain their timestamps after a large gap");
  }
}

struct AdvancingClock {
  uint32_t now = 0;
  bool advance_on_enter = false;
};

uint32_t advancing_now(void *context) {
  return static_cast<AdvancingClock *>(context)->now;
}

void advance_on_enter(void *context) {
  AdvancingClock *clock = static_cast<AdvancingClock *>(context);
  if (clock->advance_on_enter) {
    clock->now = 1000;
  }
}

void exit_noop(void *) {}

void a_release_due_during_selection_blocks_a_budgeted_task() {
  Core core;
  AdvancingClock clock;
  Port port;
  port.now_us = advancing_now;
  port.enter_critical = advance_on_enter;
  port.exit_critical = exit_noop;
  port.context = &clock;
  check(core.set_port(port), "the advancing clock is set");
  Counted high;
  Counted low;
  TaskSpec high_spec = periodic("high", &high, 1000, Priority::High);
  high_spec.phase_us = 1000;
  TaskSpec low_spec = periodic("low", &low, 1000, Priority::Low);
  low_spec.budget_us = 10;
  check(core.add(high_spec) == AddError::None && core.add(low_spec) == AddError::None,
        "the tasks are added");
  check(core.start(), "the set starts");
  clock.advance_on_enter = true;
  check(!core.run_once() && low.runs == 0, "a newly due higher release blocks the low task");
  clock.advance_on_enter = false;
  check(core.run_once() && high.runs == 1, "the higher task runs next");
}

void interval_total_survives_32_bit_overflow() {
  Core core;
  Clock clock;
  attach(core, clock);
  Counted counted;
  TaskIndex index = kInvalidTask;
  check(core.add(periodic("slow", &counted, 1000000000, Priority::Normal), &index) == AddError::None,
        "the slow task is added");
  check(core.start() && core.run_once(), "the first release runs");
  for (int i = 0; i < 5; ++i) {
    clock.now += 1000000000;
    check(core.run_once(), "the next release runs");
  }
  check(core.stats(index)->sum_interval_us == 5000000000ULL,
        "the interval total exceeds 32 bits");
}

} // namespace

int main() {
  the_task_set_is_frozen_by_start_and_full_at_its_capacity();
  a_periodic_task_runs_once_per_release_in_index_order();
  a_higher_priority_task_is_chosen_first();
  a_late_start_and_a_long_run_are_recorded();
  posts_coalesce_unless_a_count_is_asked_for();
  a_post_inside_a_callback_is_kept();
  a_missed_period_keeps_one_release_or_upto_a_count();
  the_earliest_release_is_the_wake_time();
  releases_are_compared_across_the_counter_wrap();
  the_pump_sleeps_to_the_earliest_release();
  a_post_made_during_the_sleep_is_not_lost();
  a_task_waits_while_a_higher_release_is_too_near();
  a_skipped_backlog_keeps_the_newest_release();
  a_caught_up_backlog_keeps_its_release_times();
  a_large_backlog_replaces_already_pending_releases();
  a_release_due_during_selection_blocks_a_budgeted_task();
  interval_total_survives_32_bit_overflow();

  std::printf("%d checks, %d failed\n", g_ran, g_failed);
  return g_failed == 0 ? 0 : 1;
}
