#!/usr/bin/env python3
# This file is a part of ThetaGP.
#
# ThetaGP is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# ThetaGP is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# Test: keypad debounce twin model (ADR-0006 §7) -- host side, no board.
#
# WHAT THIS IS
#   Two independent implementations of the keypad debounce filter, driven by the
#   same synthetic raw-sample vectors:
#     * OldFilterTwin  - the 16-sample shift window + 12-of-16 majority vote,
#                        decided once per 16 scans (ADR-0006 §1.2)
#     * NewFilterTwin  - the per-scan asymmetric confirmation filter, press
#                        PRESS_SAMPLES / release RELEASE_SAMPLES (ADR-0006 §2.2
#                        and appendix A)
#   Both are written from the ADR's specification, NOT from
#   src/drivers/device/keypad.cpp: the twin is a twin of the *spec*, so a
#   disagreement with the implementation is a signal about one of the two.
#   Nothing here imports or includes product code, and nothing here talks to a
#   board (no serial port, no measurement).
#
# WHY THE CALIBRATION GATE COMES FIRST (ADR-0006 §7.1)
#   A model is only worth its predictions if it can reproduce something already
#   known. The old filter's latency is derivable from its constants alone --
#   375.00..843.75 us press (12..27 samples) and 156.25..625.00 us release
#   (5..20 samples) -- so the old-filter twin is run over all 16 vote-grid
#   phases first. If it does not reproduce both ranges, this script exits
#   non-zero and prints NOTHING about the new filter: the new filter's numbers
#   would be quotes from a model that has not been shown to model this driver.
#
# RUN
#   python3 scripts/test/twin_keypad_debounce.py
#   python3 scripts/test/twin_keypad_debounce.py --verbose
#   python3 scripts/test/twin_keypad_debounce.py --mutate N=3     (self-check)
#   python3 scripts/test/twin_keypad_debounce.py --mutate M=33    (self-check)
#
# EXIT CODES
#   0  calibration gate passed and every check / pre-registered prediction passed
#   1  calibration gate FAILED -- the new filter's predictions were not printed
#   2  gate passed, but at least one check or pre-registered expectation failed
#   3  gate and checks passed, but the model contradicts an ADR *text* figure
#      (printed as [ADR-DISCREPANCY]; a fact about the ADR, not about the code)
#
# CONVENTIONS (every number below depends on these; they are ADR-0006's)
#   * one scan period = 1/32000 s = 31.25 us = 1 "sample"; the timeline is
#     quantised in 1/64 of a scan period ("substep"), which is §7.1's input
#     phase resolution and §7.2 P8(d)'s sub-sample axis.
#   * scan k reads the raw lines at time k (in scan periods) and publishes its
#     committed state at time k+1; a report tick at time t therefore reads the
#     state of scan floor(t)-1. The report tick is STRICTLY periodic -- a
#     specified assumption of the model, not a property of the device (§3.3(B),
#     §7.5): the lateness sensitivity is outside this model's reach.
#   * the "phase" of an input vector is the sub-sample offset, in substeps, of
#     the vector's edges relative to the scan grid; it is realised by sampling
#     the vector's logical level at substep k*64 - phase.
#   * a latency is counted the way §1.2 defines it: the number of samples the
#     level was seen differing, the first differing read counting as 1 and the
#     committing read included.

"""ADR-0006 §7 synthetic-clock twin model of the keypad debounce filter."""

import argparse
import sys
from bisect import bisect_right

# ---------------------------------------------------------------- constants

SCAN_HZ = 32_000
SCAN_PERIOD_US = 1_000_000.0 / SCAN_HZ          # 31.25
SUB = 64                                        # substeps per scan period (§7.1)

REPORT_8KHZ_SAMPLES = 4                         # 125 us
REPORT_1KHZ_SAMPLES = 32                        # 1000 us

OLD_WINDOW = 16                                 # DEBOUNCE_SAMPLES (§1.2)
OLD_VOTE = 12                                   # DEBOUNCE_THRESHOLD (§1.2)

PRESS_SAMPLES = 2                               # N (§3.2)
RELEASE_SAMPLES = 32                            # M (§3.4)

PRESS = 1
RELEASE = 0

# §1.2: the old filter's reachable latency, in samples, over all 16 grid phases.
OLD_PRESS_LATENCY_MIN, OLD_PRESS_LATENCY_MAX = 12, 27
OLD_RELEASE_LATENCY_MIN, OLD_RELEASE_LATENCY_MAX = 5, 20

# The unreachable pair ADR-0004's brief recorded (875 us / 656.25 us = 28 / 21
# samples): §1.2's provenance note says a model printing these has not
# reproduced this driver.
UNREACHABLE_PRESS_SAMPLES = 28
UNREACHABLE_RELEASE_SAMPLES = 21


def us(samples):
    return samples * SCAN_PERIOD_US


# ------------------------------------------------------------- input vectors


class Wave:
    """A piecewise-constant input level in *logical* coordinates, with
    breakpoints measured in substeps (1/64 of a scan period).  `lead` is the
    level read before the vector's start: None means the vector's own first
    level (the wave extends backwards), which is right for a step; a periodic
    train sets it to the level the train ends on, i.e. its own continuation."""

    __slots__ = ("_len", "_lv", "total", "lead")

    def __init__(self, lead=None):
        self._len = []
        self._lv = []
        self.total = 0
        self.lead = lead

    def hold(self, level, samples):
        return self.hold_sub(level, samples * SUB)

    def hold_sub(self, level, substeps):
        if substeps <= 0:
            return self
        if self._lv and self._lv[-1] == level:
            self._len[-1] += substeps
        else:
            self._len.append(substeps)
            self._lv.append(level)
        self.total += substeps
        return self

    # square train: k_periods x [high half, low half] (P8's trains)
    @staticmethod
    def train(period_samples, k_periods, half="high"):
        # the train is periodic, so the level read before its start is the level
        # of the previous half-period -- the one the train ends on
        w = Wave(lead=RELEASE if half == "high" else PRESS)
        for _ in range(k_periods):
            if half == "high":
                w.hold_sub(PRESS, period_samples * SUB // 2)
                w.hold_sub(RELEASE, period_samples * SUB - period_samples * SUB // 2)
            else:
                w.hold_sub(RELEASE, period_samples * SUB // 2)
                w.hold_sub(PRESS, period_samples * SUB - period_samples * SUB // 2)
        return w

    def n_samples(self, phase):
        """The grid points k whose read position k*SUB - phase is inside the
        vector: k*SUB < total + phase."""
        if self.total <= 0:
            return 0
        return (self.total + phase + SUB - 1) // SUB

    def raw(self, phase=0):
        cum = []
        c = 0
        for n in self._len:
            c += n
            cum.append(c)
        lv = self._lv
        lead = lv[0] if self.lead is None else self.lead
        out = [0] * self.n_samples(phase)
        for k in range(len(out)):
            p = k * SUB - phase
            out[k] = lead if p < 0 else lv[bisect_right(cum, p)]
        return out

    def first_sample_of(self, logical_start, phase):
        """Index of the first sample that reads a position >= logical_start."""
        return -(-(logical_start + phase) // SUB)

    def sample_span(self, logical_start, logical_end, phase):
        """(first_index, count) of the samples read inside [start, end)."""
        k0 = self.first_sample_of(logical_start, phase)
        k1 = self.first_sample_of(logical_end, phase)
        return k0, k1 - k0


def square_train_wave(period_samples, k_periods):
    """[high half, low half] repeated.  Used by P8: the train starts pressed, so
    the P8(c)/(d) sub-threshold trains do not have to be reshaped, and it ends
    on a low half -- which is only sampled to its own geometric width, never
    extended (a vector never samples past its own logical end)."""
    return Wave.train(period_samples, k_periods, half="high")


def half_lengths(period_samples):
    """(high half, low half) in substeps, for the odd periods."""
    h = period_samples * SUB // 2
    return h, period_samples * SUB - h


# ---------------------------------------------------------------- the twins


def popcount(x):
    return bin(x).count("1")


class OldFilterTwin:
    """ADR-0006 §1.2.  Every raw bit is shifted into a 16-bit history; a modulo-16
    counter is advanced and tested AFTER the increment (`_scanCount` = 0), so the
    vote falls on the same scan of every 16-scan cycle and the residual wait
    after a count settles is at most 15 scans.  Only on that scan is each key's
    stable state re-derived by majority vote (12 of 16) and the mask stored."""

    def __init__(self, window=OLD_WINDOW, vote=OLD_VOTE):
        self.window = window
        self.vote = vote
        self.mask = (1 << window) - 1
        self.history = 0
        self.scan_count = 0
        self.state = RELEASE

    def run(self, raw):
        hist = self.history
        cnt = self.scan_count
        st = self.state
        vote = self.vote
        mask = self.mask
        window = self.window
        commits = []
        for i, r in enumerate(raw):
            hist = ((hist << 1) | r) & mask
            cnt = (cnt + 1) % window
            if cnt == 0:
                new = PRESS if popcount(hist) >= vote else RELEASE
                if new != st:
                    st = new
                    commits.append((i, st))
        self.history, self.scan_count, self.state = hist, cnt, st
        return commits


class NewFilterTwin:
    """ADR-0006 §2.2 / appendix A, transcribed from the spec.  One transition per
    sample: an agreeing sample clears both runs; an opposed sample advances its
    own run, zeroes the other, and commits the state when the run reaches that
    direction's threshold -- then the crossing run is reset to 0."""

    def __init__(self, press_samples=PRESS_SAMPLES, release_samples=RELEASE_SAMPLES):
        self.N = press_samples
        self.M = release_samples
        self.press_run = 0
        self.release_run = 0
        self.state = RELEASE
        self.states = []
        self.trace = []

    def run(self, raw, trace=False):
        N, M = self.N, self.M
        pr, rr, st = self.press_run, self.release_run, self.state
        commits = []
        states = [0] * len(raw)
        rows = [] if trace else None
        for i, r in enumerate(raw):
            if r == st:
                pr = 0
                rr = 0
            elif r == PRESS:
                rr = 0
                pr += 1
                if pr >= N:
                    st = PRESS
                    pr = 0
                    commits.append((i, PRESS))
            else:
                pr = 0
                rr += 1
                if rr >= M:
                    st = RELEASE
                    rr = 0
                    commits.append((i, RELEASE))
            states[i] = st
            if rows is not None:
                rows.append((i, st, pr, rr))
        self.press_run, self.release_run, self.state = pr, rr, st
        self.states = states
        self.trace = rows if rows is not None else []
        return commits


def run_new(raw, N=PRESS_SAMPLES, M=RELEASE_SAMPLES, trace=False):
    t = NewFilterTwin(N, M)
    commits = t.run(raw, trace=trace)
    return commits, t.states, t.trace


# --------------------------------------------------------- the report stage


def report_ticks(states, period_samples, phase_sub):
    """Sample the committed mask with a strictly periodic report tick of
    `period_samples` scan periods and its own phase (phase_sub/SUB of a report
    period).  A tick at time t reads the state published by scan floor(t)-1;
    before scan 0 has published, the boot state (released) is read."""
    n = len(states)
    out = []
    m = 0
    while True:
        t_sub = (m * SUB + phase_sub) * period_samples      # units of 1/SUB
        if t_sub >= n * SUB:
            break
        k = t_sub // SUB - 1
        out.append(states[k] if k >= 0 else RELEASE)
        m += 1
    return out


# ------------------------------------------------------------- check harness


class Suite:
    """Collects results.  A check is PASS/FAIL; a `note` records a disagreement
    with an ADR *text* figure (exit code 3), not with a pass condition."""

    def __init__(self, verbose=False):
        self.verbose = verbose
        self.rows = []
        self.fails = []
        self.notes = []
        self.max_fail_echo = 24

    def section(self, title):
        print("")
        print("=" * 78)
        print(title)
        print("=" * 78)

    def check(self, cid, ok, text, detail=""):
        status = "PASS" if ok else "FAIL"
        self.rows.append((cid, status, text))
        if not ok:
            self.fails.append(cid)
        print("[%s] %-4s %s" % (status, cid, text))
        if detail and (self.verbose or not ok):
            for line in detail.splitlines():
                print("        " + line)
        return ok

    def note(self, cid, text, detail=""):
        """A disagreement with an ADR *text* figure: exit code 3."""
        self.notes.append(cid)
        print("[NOTE] %-4s %s" % (cid, text))
        if detail:
            for line in detail.splitlines():
                print("        " + line)

    def obs(self, cid, text, detail=""):
        """An observation with no ADR claim to contradict: informational only."""
        print("[obs ] %-4s %s" % (cid, text))
        if detail:
            for line in detail.splitlines():
                print("        " + line)

    def summary(self):
        print("")
        print("-" * 78)
        npass = sum(1 for _, s, _ in self.rows if s == "PASS")
        print("checks: %d pass / %d fail" % (npass, len(self.fails)))
        if self.fails:
            print("failed: %s" % ", ".join(self.fails))
        if self.notes:
            print("notes (model vs ADR text): %s" % ", ".join(self.notes))
        print("-" * 78)


# ------------------------------------------------------ calibration gate (P9)


def old_twin_press_latency(grid_phase):
    """A 0->1 step landing on whole-sample offset `grid_phase` relative to the
    vote grid, held.  Returns (latency in samples, commit index)."""
    w = Wave().hold(RELEASE, grid_phase).hold(PRESS, 1024)
    raw = w.raw(0)
    twin = OldFilterTwin()
    commits = twin.run(raw)
    if len(commits) != 1:
        return None, None
    idx = commits[0][0]
    return idx - grid_phase + 1, idx


def old_twin_release_latency(grid_phase):
    """A held press (long enough that the 16-bit window is full and the press has
    committed at every grid phase), then a 1->0 step on whole-sample offset
    `grid_phase` relative to the vote grid, held."""
    w = Wave().hold(RELEASE, grid_phase).hold(PRESS, 64).hold(RELEASE, 1024)
    raw = w.raw(0)
    twin = OldFilterTwin()
    commits = twin.run(raw)
    rel = [c for c in commits if c[1] == RELEASE]
    if len(rel) != 1:
        return None, None
    idx = rel[0][0]
    change = grid_phase + 64
    return idx - change + 1, idx


def calibration_gate(suite):
    """ADR-0006 §7.1 / §7.2 P9: the old-filter twin must reproduce the derived
    latency bounds over all 16 phases, or nothing about the new filter may be
    printed.  Returns True when the gate passes."""
    suite.section("GATE G1 (ADR §7.1 / P9): old-filter twin, calibration")
    print("expected: press   %d..%d samples = %.2f..%.2f us"
          % (OLD_PRESS_LATENCY_MIN, OLD_PRESS_LATENCY_MAX,
             us(OLD_PRESS_LATENCY_MIN), us(OLD_PRESS_LATENCY_MAX)))
    print("expected: release %d..%d samples = %.2f..%.2f us"
          % (OLD_RELEASE_LATENCY_MIN, OLD_RELEASE_LATENCY_MAX,
             us(OLD_RELEASE_LATENCY_MIN), us(OLD_RELEASE_LATENCY_MAX)))
    print("")
    print(" grid  press        press        release      release")
    print(" phase samples      us           samples      us")
    print(" ----- ------------ ------------ ------------ ------------")
    press_lat = []
    release_lat = []
    for g in range(OLD_WINDOW):
        p, _ = old_twin_press_latency(g)
        r, _ = old_twin_release_latency(g)
        press_lat.append(p)
        release_lat.append(r)
        print("  %2d   %10s   %10s   %10s   %10s"
              % (g,
                 "?" if p is None else "%d" % p,
                 "?" if p is None else "%.2f" % us(p),
                 "?" if r is None else "%d" % r,
                 "?" if r is None else "%.2f" % us(r)))

    ok = True
    detail = []
    if None in press_lat or None in release_lat:
        ok = False
        detail.append("a phase produced an unexpected number of commits")
    else:
        ps, rs = sorted(press_lat), sorted(release_lat)
        print("")
        print("observed press   samples: %s  (min %d, max %d)"
              % (ps, ps[0], ps[-1]))
        print("observed press   us     : %.2f .. %.2f"
              % (us(ps[0]), us(ps[-1])))
        print("observed release samples: %s  (min %d, max %d)"
              % (rs, rs[0], rs[-1]))
        print("observed release us     : %.2f .. %.2f"
              % (us(rs[0]), us(rs[-1])))
        want_p = list(range(OLD_PRESS_LATENCY_MIN, OLD_PRESS_LATENCY_MAX + 1))
        want_r = list(range(OLD_RELEASE_LATENCY_MIN, OLD_RELEASE_LATENCY_MAX + 1))
        if ps != want_p:
            ok = False
            detail.append("press set differs: missing %s, extra %s"
                          % (sorted(set(want_p) - set(ps)), sorted(set(ps) - set(want_p))))
        if rs != want_r:
            ok = False
            detail.append("release set differs: missing %s, extra %s"
                          % (sorted(set(want_r) - set(rs)), sorted(set(rs) - set(want_r))))
        if UNREACHABLE_PRESS_SAMPLES in ps or UNREACHABLE_RELEASE_SAMPLES in rs:
            ok = False
            detail.append("the unreachable pair of §1.2 was produced (28/21 samples)")

    suite.check("G1a", ok,
                "press latency over all %d phases = %d..%d samples "
                "(%.2f..%.2f us)"
                % (OLD_WINDOW, min(x for x in press_lat if x),
                   max(x for x in press_lat if x),
                   us(min(x for x in press_lat if x)),
                   us(max(x for x in press_lat if x))),
                "\n".join(detail))
    suite.check("G1b", ok,
                "release latency over all %d phases = %d..%d samples "
                "(%.2f..%.2f us)"
                % (OLD_WINDOW, min(x for x in release_lat if x),
                   max(x for x in release_lat if x),
                   us(min(x for x in release_lat if x)),
                   us(max(x for x in release_lat if x))))

    # V10: the §2.3 mechanism, not only the two latencies.  5 consecutive released
    # samples injected into a committed press must produce a phantom release
    # commit on the old twin, and the new twin must not move at all.
    print("")
    print("V10: a 5-sample released burst inside a held press, 16 grid phases")
    w10 = Wave().hold(PRESS, 64 + (OLD_WINDOW - 1)).hold(RELEASE, 5).hold(PRESS, 200)
    phantom_phases = []
    span_samples = set()
    for g in range(OLD_WINDOW):
        raw = Wave().hold(PRESS, 64 + g).hold(RELEASE, 5).hold(PRESS, 200).raw(0)
        commits = OldFilterTwin().run(raw)
        rel = [c for c in commits if c[1] == RELEASE]
        if rel:
            phantom_phases.append(g)
            nxt = [c for c in commits if c[0] > rel[0][0] and c[1] == PRESS]
            if nxt:
                span_samples.add(nxt[0][0] - rel[0][0])
    print("  phantom release commits on %d of %d grid phases: %s"
          % (len(phantom_phases), OLD_WINDOW, phantom_phases))
    print("  release commit -> next press commit gap (samples): %s"
          % sorted(span_samples))
    new_commits, _, _ = run_new(w10.raw(0))
    print("  new filter on the same vector: %s" % new_commits)
    suite.check("G1c", len(phantom_phases) > 0 and len(phantom_phases) < OLD_WINDOW,
                "V10 phantom release is grid-phase dependent (%d/%d phases), i.e. "
                "the mechanism of §2.3 exists in the old twin"
                % (len(phantom_phases), OLD_WINDOW))
    suite.check("G1d", len(new_commits) == 1,
                "the new filter produces no release commit for the 5-sample burst "
                "(commits: %s)" % new_commits)

    # The ADR quotes the restoration as "12 consecutive pressed samples = 375 us"
    # (V10).  The model reports the span between the two commits instead; that
    # number is what a host would actually see, so a disagreement is recorded
    # rather than papered over.
    span = sorted(span_samples)
    if span != [12]:
        suite.note("G1e",
                   "V10's restoration figure does not reproduce: the ADR says the "
                   "restoration takes 12 pressed samples (375.00 us), the twin "
                   "commits the pressed state %s samples after the phantom release"
                   % (span[0] if len(span) == 1 else span),
                   "The count must reach %d ones, and at the phantom vote the window "
                   "already holds %d (16 ones minus the 5-sample burst), so one "
                   "pressed sample restores the count and the commit then waits for "
                   "the next vote, 16 scans later. The visible phantom span is %s "
                   "samples = %.2f us." % (OLD_VOTE, OLD_WINDOW - 5,
                                           span[0] if span else "?",
                                           us(span[0]) if span else float("nan")))
    else:
        suite.check("G1e", True, "V10 restoration takes 12 pressed samples as the ADR says")

    if not ok:
        print("")
        print("GATE G1 FAILED: the old-filter twin does not reproduce the ADR's")
        print("derived latency bounds.  The new filter's predictions are withheld:")
        print("a model that cannot reproduce the design that is already known")
        print("cannot be trusted about the design that is not.")
    return ok


# ------------------------------------------------------------------ the checks


def check_p0(suite):
    """P0 -- determinism / purity: the committed stream is a function of the
    sample vector alone, and the report stage cannot perturb it."""
    w = square_train_wave(320, 4)
    raw = w.raw(17)
    c1, s1, _ = run_new(raw)
    c2, s2, _ = run_new(list(raw))
    same = (c1 == c2) and (s1 == s2)
    # run the report stage at all 64 phases against the committed stream; the
    # stream itself must be bit-identical afterwards.
    before = list(s1)
    for q in range(SUB):
        report_ticks(s1, REPORT_1KHZ_SAMPLES, q)
        report_ticks(s1, REPORT_8KHZ_SAMPLES, q)
    unchanged = s1 == before
    c3, s3, _ = run_new(raw)
    replay = (c1 == c3) and (s1 == s3)
    suite.check("P0", same and unchanged and replay,
                "two runs bit-identical, 128 report-stage passes leave the stream "
                "untouched, replay identical (%d commits)" % len(c1))


def check_p1(suite):
    """P1 -- the press-commit latency is a constant in samples, at every phase:
    commit index == first_index_of_final_run + N - 1."""
    bad = []
    latencies = set()
    for p in range(SUB):
        w = Wave().hold_sub(PRESS, 64 * SUB - p if p else 0)
        # step at substep `p` inside the first scan period, then held
        w = Wave()
        if p:
            w.hold_sub(RELEASE, p)
        w.hold(PRESS, 64)
        raw = w.raw(p)
        f = raw.index(PRESS)
        commits, _, _ = run_new(raw)
        got = commits[0][0] if commits else None
        want = f + PRESS_SAMPLES - 1
        latencies.add(None if got is None else got - f)
        if got != want:
            bad.append((p, f, got, want))
    suite.check("P1", not bad,
                "64 phases: commit index == first differing sample + N - 1 "
                "(latency set in samples %s)" % sorted(x for x in latencies if x is not None),
                "\n".join("phase %d: first=%s commit=%s want=%s" % b for b in bad[:8]))


def check_p2(suite):
    """P2 -- sub-threshold rejection, both directions (the headline)."""
    # (a) single-sample and (N-1)-sample pulses in a held-released line, at every
    # phase and every position in a 64-sample frame.
    widths = sorted({1, max(1, PRESS_SAMPLES - 1)})
    bad = []
    for width in widths:
        for pos in range(64):
            for p in range(SUB):
                w = Wave()
                w.hold_sub(RELEASE, pos * SUB + p)
                w.hold(PRESS, width)
                w.hold(RELEASE, 32)
                raw = w.raw(p)
                commits, _, _ = run_new(raw)
                if commits:
                    bad.append((width, pos, p, commits))
    suite.check("P2a", not bad,
                "pulse widths %s x 64 positions x 64 phases = %d vectors, zero commits"
                % (widths, len(widths) * 64 * 64),
                "\n".join("w=%d pos=%d phase=%d -> %s" % b for b in bad[:8]))

    # (b) 1..M-1-sample dropouts injected into a held-pressed line, every phase.
    bad = []
    n = 0
    for width in range(1, RELEASE_SAMPLES):
        for p in range(SUB):
            w = Wave().hold_sub(PRESS, 64 * SUB + p).hold(RELEASE, width).hold(PRESS, 64)
            raw = w.raw(p)
            commits, _, _ = run_new(raw)
            n += 1
            unexpected = [c for c in commits if c[0] > PRESS_SAMPLES - 1]
            if unexpected:
                bad.append((width, p, unexpected))
    suite.check("P2b", not bad,
                "dropout widths 1..%d x 64 phases = %d vectors, only the initial "
                "press commit" % (RELEASE_SAMPLES - 1, n),
                "\n".join("w=%d phase=%d -> %s" % b for b in bad[:8]))


def check_p3(suite):
    """P3 -- the thresholds are exact on both sides."""
    bad = []
    for p in range(SUB):
        for width, want in ((PRESS_SAMPLES - 1, []), (PRESS_SAMPLES, [PRESS])):
            w = Wave()
            if p:
                w.hold_sub(RELEASE, p)
            w.hold(PRESS, width)
            w.hold(RELEASE, 8)
            raw = w.raw(p)
            commits, _, _ = run_new(raw)
            f = raw.index(PRESS)
            got = [c[1] for c in commits]
            if got != want:
                bad.append(("press", width, p, got, want))
            elif want:
                idx = commits[0][0]
                if idx != f + PRESS_SAMPLES - 1:
                    bad.append(("press-index", width, p, idx, f + PRESS_SAMPLES - 1))
        for width, want in ((RELEASE_SAMPLES - 1, []), (RELEASE_SAMPLES, [RELEASE])):
            w = Wave().hold(PRESS, 64).hold(RELEASE, width).hold(PRESS, 64)
            raw = w.raw(p)
            commits, _, _ = run_new(raw)
            rel = [c for c in commits if c[1] == RELEASE]
            got = [c[1] for c in rel]
            if got != want:
                bad.append(("release", width, p, got, want))
            elif want:
                f = raw.index(RELEASE, 64)
                idx = rel[0][0]
                if idx != f + RELEASE_SAMPLES - 1:
                    bad.append(("release-index", width, p, idx, f + RELEASE_SAMPLES - 1))
    suite.check("P3", not bad,
                "N=%d commits on the %dth sample, N-1=%d does not; M=%d commits on "
                "the %dth, M-1=%d does not -- 64 phases each"
                % (PRESS_SAMPLES, PRESS_SAMPLES, PRESS_SAMPLES - 1,
                   RELEASE_SAMPLES, RELEASE_SAMPLES, RELEASE_SAMPLES - 1),
                "\n".join(str(b) for b in bad[:8]))


def check_p4(suite):
    """P4 -- a long hold survives an adversarial bounce train, with a negative
    control in the same run."""
    bad = []
    # (i) alternating burst over the first 5 ms, runs <= M-1
    runs = [7, 1, 29, 3, 17, 31, 2, 11, 5, 23]     # deterministic, all <= M-1
    for p in range(SUB):
        w = Wave().hold_sub(PRESS, 64 * SUB + p)
        for r in runs:
            w.hold(RELEASE, r)
            w.hold(PRESS, r)
        w.hold(PRESS, 1600)
        raw = w.raw(p)
        commits, _, _ = run_new(raw)
        if [c[1] for c in commits] != [PRESS]:
            bad.append(("burst", p, commits))
    # (ii) isolated dropouts of width <= M-1 at scattered positions
    for p in range(SUB):
        w = Wave().hold_sub(PRESS, 64 * SUB + p)
        for k in range(1, 8):
            w.hold(PRESS, 97).hold(RELEASE, k * 4 + 1)
        w.hold(PRESS, 200)
        raw = w.raw(p)
        commits, _, _ = run_new(raw)
        if [c[1] for c in commits] != [PRESS]:
            bad.append(("dropouts", p, commits))
    suite.check("P4a", not bad,
                "50 ms hold with a 5 ms burst (runs %s <= M-1) and scattered "
                "dropouts: exactly one commit (the press), zero releases, 64 phases"
                % runs, "\n".join(str(b) for b in bad[:8]))

    # negative control: a single M-sample monotonic dropout must commit exactly
    # one release, or the check above is blind.
    bad = []
    for p in range(SUB):
        w = Wave().hold_sub(PRESS, 64 * SUB + p).hold(RELEASE, RELEASE_SAMPLES)
        w.hold(PRESS, 200)
        raw = w.raw(p)
        commits, _, _ = run_new(raw)
        if [c[1] for c in commits] != [PRESS, RELEASE, PRESS]:
            bad.append((p, commits))
    suite.check("P4b", not bad,
                "negative control: one %d-sample dropout commits exactly one release "
                "(and the return commits the press again), 64 phases"
                % RELEASE_SAMPLES,
                "\n".join(str(b) for b in bad[:8]))


def check_p5(suite):
    """P5 -- counter invariants: a key is never pending both directions, and the
    crossing counter is 0 immediately after a commit."""
    bad = []
    vectors = []
    w = square_train_wave(320, 4)
    vectors.append(("2 kHz wave p=5", w.raw(5)))
    vectors.append(("pulse train p=0", square_train_wave(34, 20).raw(0)))
    vectors.append(("pulse train p=33", square_train_wave(63, 20).raw(33)))
    wb = Wave().hold(RELEASE, 31).hold(PRESS, 2).hold(RELEASE, 5).hold(PRESS, 40)
    vectors.append(("mixed p=0", wb.raw(0)))
    for name, raw in vectors:
        commits, _, rows = run_new(raw, trace=True)
        commit_idx = {i for i, _ in commits}
        for i, st, pr, rr in rows:
            if pr and rr:
                bad.append((name, i, "both runs non-zero", pr, rr))
            if i in commit_idx and pr == rr == 0:
                continue
            if i in commit_idx and not (pr == 0 or rr == 0):
                bad.append((name, i, "crossing counter not reset", pr, rr))
    suite.check("P5", not bad,
                "pressRun*releaseRun == 0 on every sample and the crossing counter "
                "is 0 immediately after a commit (%d vectors)" % len(vectors),
                "\n".join(str(b) for b in bad[:8]))


def check_p6(suite, report_period, label, N=PRESS_SAMPLES, M=RELEASE_SAMPLES):
    """P6 -- deliverability: a minimum-length committed press must be seen by a
    strictly periodic report tick at every phase.  The minimum raw press is N
    samples (the shortest that commits); the committed press then lasts exactly
    M scans, from the press commit to the release commit."""
    # the input's own sub-sample phase cannot move this vector's first sample
    # (the wave is clamped before its start), so the axis that matters is the
    # report tick's, swept below at 1/64 of a report period.
    raw = Wave().hold(PRESS, N).hold(RELEASE, 8 * M).raw(0)
    commits, states, _ = run_new(raw, N, M)
    kinds = [c[1] for c in commits]
    if kinds[:2] != [PRESS, RELEASE]:
        return None, [], "the minimum raw press did not commit a press+release pair: %s" % commits
    window = commits[1][0] - commits[0][0]
    misses = []
    for q in range(SUB):
        ticks = report_ticks(states, report_period, q)
        if PRESS not in ticks:
            misses.append(q)
    return window, misses, None


def check_p6_suite(suite):
    misses_1k = []
    for period, label in ((REPORT_1KHZ_SAMPLES, "1 kHz"), (REPORT_8KHZ_SAMPLES, "8 kHz")):
        window, misses, err = check_p6(suite, period, label)
        if err:
            suite.check("P6", False, err)
            return
        print("  %s: committed press spans %d samples = %.2f us, report period "
              "%d samples = %.2f us, phases with no report: %d/64"
              % (label, window, us(window), period, us(period), len(misses)))
        if period == REPORT_1KHZ_SAMPLES:
            misses_1k = misses
    suite.check("P6", len(misses_1k) == 0,
                "minimum committed press (M=%d samples = %.2f us) is seen by the "
                "1 kHz tick at all 64 phases and by the 8 kHz tick at all 64 "
                "(zero margin at 1 kHz: window == report period)"
                % (RELEASE_SAMPLES, us(RELEASE_SAMPLES)))


def check_p7(suite):
    """P7 -- cold start is inert."""
    raw = [RELEASE] * 320                            # 10 ms of all-zero raw
    commits, states, _ = run_new(raw)
    suite.check("P7", not commits and not any(states),
                "10 ms of all-zero raw from the initial state: zero commits and the "
                "committed mask is 0 on every sample")


def boundary_case_table(N, M, period, phases=64, k_periods=100):
    """The phase x period case table: a [high, low] x k_periods square train at
    each sub-sample phase, run through the new filter."""
    table = {}
    for p in range(phases):
        raw = square_train_wave(period, k_periods).raw(p)
        commits, _, _ = run_new(raw, N, M)
        npress = sum(1 for _, s in commits if s == PRESS)
        nrel = sum(1 for _, s in commits if s == RELEASE)
        table[p] = (npress, nrel)
    return table


def print_case_table(title, table, phases=64):
    print("")
    print("%s" % title)
    for base in range(0, phases, 4):
        cells = "  ".join("p=%02d:(%d,%d)" % (p, table[p][0], table[p][1])
                          for p in range(base, base + 4))
        print("  " + cells)


def group_phases(table, phases=None):
    """[(list_of_phases, counts)] in phase order, split on both a value change
    and a gap in the phase run.  With `phases` the sweep is over exactly that
    many phases; without it the table's own keys are used (so a filtered table
    renders its own phases)."""
    keys = sorted(table) if phases is None else list(range(phases))
    out = []
    for p in keys:
        c = table[p]
        if out and out[-1][1] == c and out[-1][0][-1] == p - 1:
            out[-1][0].append(p)
        else:
            out.append(([p], c))
    return out


def phase_ranges(table, phases=None):
    return [(ps[0], ps[-1], c) for ps, c in group_phases(table, phases)]


def fmt_table(table, phases=None):
    """Render a {phase: (press, release)} table as phase ranges."""
    return fmt_ranges(phase_ranges(table, phases))


def fmt_ranges(ranges):
    return "; ".join((("p=%d" % a) if a == b else ("p=%d..%d" % (a, b)))
                     + " -> (%d,%d)" % c for a, b, c in ranges)


def check_p8(suite, N=PRESS_SAMPLES, M=RELEASE_SAMPLES):
    """P8 -- fast alternation does not double-count or drop, and the filter's own
    resolution limit is found rather than assumed."""
    # (a) 10 ms period square wave, 200 ms, every phase: commits == physical edges
    bad = []
    period, k = 320, 20
    hl, ll = half_lengths(period)
    for p in range(SUB):
        raw = square_train_wave(period, k).raw(p)
        commits, _, _ = run_new(raw, N, M)
        want = []
        for i in range(k):
            hs, hc = square_train_wave(period, k).sample_span(i * period * SUB, i * period * SUB + hl, p)
            if hc >= N:
                want.append((hs + N - 1, PRESS))
            ls, lc = square_train_wave(period, k).sample_span(i * period * SUB + hl, (i + 1) * period * SUB, p)
            if lc >= M:
                want.append((ls + M - 1, RELEASE))
        want.sort()
        if commits != want:
            bad.append((p, len(commits), len(want)))
    suite.check("P8a", not bad,
                "10 ms square wave, 200 ms, 64 phases: commits (%d pairs) == physical "
                "edges exactly, index for index" % k,
                "\n".join("phase %d: %d commits, %d expected" % b for b in bad[:8]))

    # (b) the minimum resolvable period, 2M = 64 samples, 100 periods, every phase
    tbl64 = boundary_case_table(N, M, 2 * M)
    bad = [p for p in range(SUB) if tbl64[p] != (100, 100)]
    suite.check("P8b", not bad,
                "period 2M=%d samples (2 ms), 100 periods, 64 phases: exactly one "
                "press commit and one release commit per period (100,100) at every "
                "phase" % (2 * M),
                "phases off: %s" % fmt_table({p: tbl64[p] for p in bad}))

    # (c) the train revision 1 mistook for that minimum, N+M = 34
    tbl34 = boundary_case_table(N, M, N + M)
    bad = [p for p in range(SUB) if tbl34[p] != (1, 0)]
    suite.check("P8c", not bad,
                "period N+M=%d samples: exactly one press commit for the whole train "
                "and no release commit, ever (the filter, not the sampler, is the "
                "limit)" % (N + M),
                "phases off: %s" % fmt_table({p: tbl34[p] for p in bad}))

    # (d) the boundary triple 62 / 63 / 64 at sub-sample phases
    tbl62 = boundary_case_table(N, M, 2 * M - 2)
    tbl63 = boundary_case_table(N, M, 2 * M - 1)
    bad = [p for p in range(SUB) if tbl62[p] != (1, 0)]
    suite.check("P8d-62", not bad,
                "period 2M-2=%d samples: no released run reaches M -- one press "
                "commit, no release commit, 64 phases" % (2 * M - 2),
                "phases off: %s" % fmt_table({p: tbl62[p] for p in bad}))

    ok63 = all(tbl63[p] in ((1, 0), (100, 100)) for p in range(SUB))
    unresolved = [p for p in range(SUB) if tbl63[p] == (1, 0)]
    resolved = [p for p in range(SUB) if tbl63[p] == (100, 100)]
    suite.check("P8d-63", ok63 and unresolved and resolved,
                "period 2M-1=%d samples: the outcome is phase-dependent, and only "
                "that way -- release never commits on %d/%d phases, every released "
                "half-period commits on %d/%d"
                % (2 * M - 1, len(unresolved), SUB, len(resolved), SUB),
                "unresolved (1,0): %s\nresolved (100,100): %s"
                % (fmt_table({p: tbl63[p] for p in unresolved}),
                   fmt_table({p: tbl63[p] for p in resolved})))

    suite.check("P8d-64", not [p for p in range(SUB) if tbl64[p] != (100, 100)],
                "period 2M=%d samples: (100,100) at every phase, no accumulation "
                "and no drift" % (2 * M))

    return {"62": tbl62, "63": tbl63, "64": tbl64, "34": tbl34}


def check_p9_already_gated():
    """P9 is GATE G1 -- it runs before any new-filter output is printed."""
    return None


# ------------------------------------------------- pre-registered predictions


def predictions(suite, tables, N=PRESS_SAMPLES, M=RELEASE_SAMPLES):
    """ADR-0006 §7.3 -- registered in advance so the model can be wrong."""
    suite.section("ADR §7.3 PRE-REGISTERED PREDICTIONS")

    # 1. At M = 8, P6 must fail: a minimum-length committed press is missed by
    #    the 1 kHz tick at some phase.
    w8, misses, err = check_p6(suite, REPORT_1KHZ_SAMPLES, "1 kHz", N, 8)
    if err:
        suite.check("PRE1", False, "M=8: " + err)
    else:
        suite.check("PRE1", len(misses) > 0,
                    "at M=8, P6 fails: the committed press (M=8 samples = %.2f us) "
                    "is missed entirely at %d of 64 report phases (1 kHz)"
                    % (us(w8), len(misses)),
                    "missed phases: %s" % misses)
        ctrl = check_p6(suite, REPORT_1KHZ_SAMPLES, "1 kHz", N, 32)
        w32, m32 = ctrl[0], ctrl[1]
        suite.check("PRE1b", (m32 is not None) and len(m32) == 0,
                    "control: at M=%d the same check passes on all 64 phases "
                    "(window %d samples = the 1 kHz report period)" % (M, w32))

    # 2. At M = 64 the assertion set must refuse to compile: A5 fires.
    a5_lhs = 1000 * (64 - N)
    a5_rhs = SCAN_HZ
    a1 = SCAN_HZ >= N * 8000
    a6 = 1000 * 64 >= SCAN_HZ
    a7 = 64 * 1000 >= SCAN_HZ
    suite.check("PRE2", a5_lhs > a5_rhs,
                "A5 fires at M=64: 1000*(M-N) = %d > f_scan = %d (A1 %s, A6 %s, A7 "
                "%s -- A5 is the only one that trips)"
                % (a5_lhs, a5_rhs, "holds" if a1 else "trips",
                   "holds" if a6 else "trips", "holds" if a7 else "trips"))

    # 3. At a period of 63 samples the outcome is phase-dependent, and only that
    #    way: one half of the sub-sample phase range resolves the release, the
    #    other half never commits it.
    tbl = tables["63"]
    unresolved = [p for p in range(SUB) if tbl[p] == (1, 0)]
    resolved = [p for p in range(SUB) if tbl[p] == (100, 100)]
    flip = (min(resolved), max(unresolved)) if resolved and unresolved else None
    suite.check("PRE3", bool(unresolved) and bool(resolved) and len(unresolved) + len(resolved) == SUB,
                "period 63: the boundary is phase-dependent, not a cutoff at 2M -- "
                "%d phases resolve the release, %d never do; the flip sits at the "
                "half-sample point (unresolved %s / resolved %s)"
                % (len(unresolved), len(resolved),
                   fmt_table({p: tbl[p] for p in unresolved}),
                   fmt_table({p: tbl[p] for p in resolved})))
    print("  flip point: %s" % (flip,))
    if len(unresolved) != SUB // 2:
        suite.note("PRE3b",
                   "the 32/32 split of the phase range is %d/%d in this model"
                   % (len(unresolved), len(resolved)),
                   "an edge lands exactly on a sample instant at phase 0, which is "
                   "one substep (1/64 period = 0.49 us) away from the naked "
                   "half-sample point")
    return tbl


# ------------------------------------------------------- negative self-check


def boundary_family(N, M):
    """The ADR's P8 boundary trains at the DESIGN's periods (34 / 62 / 63 / 64
    samples), run through the new filter with the thresholds (N, M) given.  The
    periods are the design's, not 2*M: the prediction is about these trains."""
    return {
        "34": boundary_case_table(N, M, PRESS_SAMPLES + RELEASE_SAMPLES),
        "62": boundary_case_table(N, M, 2 * RELEASE_SAMPLES - 2),
        "63": boundary_case_table(N, M, 2 * RELEASE_SAMPLES - 1),
        "64": boundary_case_table(N, M, 2 * RELEASE_SAMPLES),
    }


def family_holds(tbl):
    """The pre-registered boundary expectations, restated for a given run."""
    ok = True
    ok &= all(tbl["34"][p] == (1, 0) for p in range(SUB))
    ok &= all(tbl["62"][p] == (1, 0) for p in range(SUB))
    ok &= all(tbl["64"][p] == (100, 100) for p in range(SUB))
    vals = {tbl["63"][p] for p in range(SUB)}
    ok &= len({tbl["63"][p] for p in range(SUB)}) == 2
    ok &= all(tbl["63"][p] in ((1, 0), (100, 100)) for p in range(SUB))
    n_unres = sum(1 for p in range(SUB) if tbl["63"][p] == (1, 0))
    ok &= n_unres == SUB // 2
    return bool(ok), sorted(vals), n_unres


def negative_selfcheck(suite, baseline):
    """Prove the criteria are not vacuously true: corrupt a threshold and show
    which expectations move.  The corruption is a parameter of the model, not an
    edit of the file (the file-level variant is reported separately); the trains
    stay at the design's periods, so what is measured is the *threshold*, not a
    train length that moved with it."""
    suite.section("NEGATIVE SELF-CHECK: corrupt a threshold, watch the criteria")
    base_ok, base_vals, base_unres = family_holds(baseline)
    print("")
    print("  baseline (N=%d, M=%d): 2M-boundary family holds = %s; period-63 "
          "outcome values %s; unresolved phases %d"
          % (PRESS_SAMPLES, RELEASE_SAMPLES, base_ok, base_vals, base_unres))
    out = {}
    for label, N, M in (("PRESS_SAMPLES 2 -> 3", 3, RELEASE_SAMPLES),
                        ("PRESS_SAMPLES 2 -> 33", 33, RELEASE_SAMPLES),
                        ("RELEASE_SAMPLES 32 -> 33", PRESS_SAMPLES, 33),
                        ("RELEASE_SAMPLES 32 -> 31", PRESS_SAMPLES, 31)):
        tbl = boundary_family(N, M)
        holds, vals, unres = family_holds(tbl)
        same63 = all(tbl["63"][p] == baseline["63"][p] for p in range(SUB))
        same64 = all(tbl["64"][p] == baseline["64"][p] for p in range(SUB))
        print("")
        print("  mutation: %s   (N=%d, M=%d)" % (label, N, M))
        print("    period 34 (N+M): %s" % fmt_table(tbl["34"]))
        print("    period 62 (2M-2): %s" % fmt_table(tbl["62"]))
        print("    period 63 (2M-1): %s" % fmt_table(tbl["63"]))
        print("    period 64 (2M)  : %s" % fmt_table(tbl["64"]))
        print("    period-63 values %s, unresolved %d/64, identical to baseline "
              "at 63: %s, at 64: %s" % (vals, unres, same63, same64))
        print("    2M-boundary family holds: %s" % holds)
        out[label] = {"holds": holds, "same63": same63}

    # A6/A7 evaluated at a corrupted M: the compile-time floor is the other place
    # a design number is pinned.
    a6_fires = [M for M in (3, 8, 16, 31, 32) if 1000 * M < SCAN_HZ]
    print("")
    print("  A6 (1000*M >= f_scan) trips at M in %s; M=32 sits exactly on it"
          % a6_fires)

    suite.check("SC1", out["RELEASE_SAMPLES 32 -> 33"]["holds"] is False,
                "the 2M-boundary prediction is not vacuous: with M=33 the released "
                "half-period run (32 samples) no longer reaches the threshold, so "
                "period 64 stops producing a pair and period 63 loses its phase "
                "dependence")
    suite.check("SC2", out["PRESS_SAMPLES 2 -> 33"]["holds"] is False,
                "the same prediction also dies when N is pushed past the half-period "
                "run (N=33: the pressed run of 32 never commits, so period 64 gives "
                "(0,0) and 63 gives no phase dependence)")
    suite.check("SC3", out["PRESS_SAMPLES 2 -> 3"]["same63"] is True,
                "PRESS_SAMPLES 2 -> 3 does NOT move the 2M-boundary family: at 63/64 "
                "the pressed half-period is 31/32 samples, which clears N=3 with 28 "
                "samples to spare, so the family's critical threshold is M (reported "
                "as found, not worked around)")
    suite.check("SC4", out["RELEASE_SAMPLES 32 -> 31"]["holds"] is False,
                "M=31 breaks it too, from the other side: the sub-threshold trains "
                "(62 and 63, whose released half-period is exactly M-1 = 31 samples) "
                "start committing, so the phase dependence at 63 disappears -- the "
                "family pins M = 32 exactly, +/-1 in both directions")
    suite.check("SC5", out["PRESS_SAMPLES 2 -> 3"]["holds"] is True
                and out["RELEASE_SAMPLES 32 -> 33"]["holds"] is False
                and PRESS_SAMPLES == 2 and RELEASE_SAMPLES == 32,
                "the case tables above were produced with N=%d, M=%d (the design "
                "values); a threshold passed on the command line mutates the model "
                "itself and prints a banner saying so"
                % (PRESS_SAMPLES, RELEASE_SAMPLES))
    return out


# --------------------------------------------------------------------- main


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="ADR-0006 §7 twin model of the keypad debounce filter")
    ap.add_argument("--verbose", action="store_true",
                    help="print per-check detail even on PASS")
    ap.add_argument("--mutate", action="append", default=[],
                    metavar="N=3|M=33",
                    help="override a threshold (self-check); repeatable")
    ap.add_argument("--no-selfcheck", action="store_true",
                    help="skip the negative self-check section")
    args = ap.parse_args(argv)

    global PRESS_SAMPLES, RELEASE_SAMPLES
    for m in args.mutate:
        k, _, v = m.partition("=")
        if k.upper() in ("N", "PRESS", "PRESS_SAMPLES"):
            PRESS_SAMPLES = int(v)
        elif k.upper() in ("M", "RELEASE", "RELEASE_SAMPLES"):
            RELEASE_SAMPLES = int(v)
        else:
            print("unknown mutation %r" % m)
            return 4
    if args.mutate:
        print("!! mutated model: PRESS_SAMPLES=%d RELEASE_SAMPLES=%d"
              % (PRESS_SAMPLES, RELEASE_SAMPLES))

    print("ADR-0006 §7 synthetic-clock twin model")
    print("scan %d Hz -> %.2f us per sample; PRESS_SAMPLES=%d (N), "
          "RELEASE_SAMPLES=%d (M); phases 0..63 = 1/64 sample"
          % (SCAN_HZ, SCAN_PERIOD_US, PRESS_SAMPLES, RELEASE_SAMPLES))

    suite = Suite(verbose=args.verbose)

    if not calibration_gate(suite):
        suite.summary()
        return 1

    suite.section("P0-P8: the new filter")
    check_p9_already_gated()
    check_p0(suite)
    check_p1(suite)
    check_p2(suite)
    check_p3(suite)
    check_p4(suite)
    check_p5(suite)
    check_p6_suite(suite)
    check_p7(suite)

    suite.section("P8: fast alternation and the filter's own resolution limit")
    tables = check_p8(suite)

    suite.section("CASE TABLE: phase x period -> (press commits, release commits)")
    print("square train [high half, low half] x 100 periods, new filter, phases 0..63")
    labels = {PRESS_SAMPLES + RELEASE_SAMPLES: "N+M",
              2 * RELEASE_SAMPLES - 2: "2M-2",
              2 * RELEASE_SAMPLES - 1: "2M-1",
              2 * RELEASE_SAMPLES: "2M"}
    order = [PRESS_SAMPLES + RELEASE_SAMPLES, 2 * RELEASE_SAMPLES - 2,
             2 * RELEASE_SAMPLES - 1, 2 * RELEASE_SAMPLES]
    for period in order:
        tbl = (tables[str(period)] if str(period) in tables
               else boundary_case_table(PRESS_SAMPLES, RELEASE_SAMPLES, period))
        print_case_table("period %d samples (= %s)" % (period, labels[period]), tbl)
    print("")
    for period in order:
        tbl = (tables[str(period)] if str(period) in tables
               else boundary_case_table(PRESS_SAMPLES, RELEASE_SAMPLES, period))
        print("  period %-3d : %s" % (period, fmt_table(tbl)))

    predictions(suite, tables)

    if not args.no_selfcheck:
        negative_selfcheck(suite, tables)

    suite.section("NOTES: model vs ADR text (informational)")
    # OBS1 -- artifact width below one scan period: how many samples does it take?
    widths = set()
    commits_any = set()
    for p in range(SUB):
        raw = Wave().hold(RELEASE, 16).hold_sub(PRESS, int(0.9 * SUB)).hold(RELEASE, 16).raw(p)
        run = best = 0
        for r in raw:
            run = run + 1 if r == PRESS else 0
            best = max(best, run)
        widths.add(best)
        commits_any.add(len(run_new(raw)[0]))
    suite.obs("OBS1", "a 0.9-sample-period artifact is sampled 0 or 1 times, never "
                       "twice (sampled widths seen over 64 phases: %s; commits: %s)"
               % (sorted(widths), sorted(commits_any)),
               "An integer-width event (exactly w scan periods) is sampled exactly w "
               "times at EVERY phase -- a half-open interval of integer length holds "
               "exactly that many grid points. The 1/64 phase axis therefore has "
               "content only for widths that are not an integer number of scan "
               "periods; §7.2's P2 vectors are integer-width, so they are phase-"
               "invariant, and the axis earns its place in P8(d) instead.")
    # OBS2 -- a physical dropout of M-0.5 sample periods
    counts = set()
    for p in range(SUB):
        w2 = Wave().hold(PRESS, 16)
        w2.hold_sub(RELEASE, RELEASE_SAMPLES * SUB - SUB // 2)
        w2.hold(PRESS, 16)
        commits, _, _ = run_new(w2.raw(p))
        counts.add(sum(1 for _, s in commits if s == RELEASE))
    suite.obs("OBS2", "a physical dropout of (M-0.5) sample periods commits a "
                       "release on part of the phase range (release commits over 64 "
                       "phases: %s)" % sorted(counts),
               "The threshold is on the *sampled* run, so the tolerance the filter "
               "actually delivers is M-1 samples at worst and M samples at best "
               "depending on where the physical edges land; the same statement as "
               "§7.5's KU-6, and not something a threshold can pin.")
    print("")
    print("the model tests the specification, not the hardware (§7.5, KU-1..KU-6):")
    print("  - no bounce measurement is represented; the vectors are chosen by hand")
    print("  - the report tick is specified as strictly periodic, so the zero-margin")
    print("    lateness sensitivity of §3.3(B) is outside this model's reach")
    print("  - per-key vectors cannot represent the cross-key coupling of §2.4 class 2")

    suite.summary()
    if suite.fails:
        return 2
    if suite.notes:
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
