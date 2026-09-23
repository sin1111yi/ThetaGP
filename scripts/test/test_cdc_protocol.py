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
# Test: CDC JSON protocol — sys domain + test domain
# Target: CDC ACM virtual serial port on firmware built with
#         -DBUILD_TEST_API=ON (source sees THETAGP_CFG_BUILD_TEST_API=1)
#
# Note: The gamepad/HID injection commands (test.set_mode, inject_*,
# set_override, etc.) were removed in commit 2408e32 (refactor(test):
# remove TestInjector and gamepad/HID report hooks). The current
# firmware test domain exposes flash/memory commands instead:
#   test.flash_info, test.mem_info, test.flash_read (read-only)
#   test.spi_mode, test.erase_sector, test.compaction, test.chip_erase
# Destructive commands (chip_erase / erase_sector / compaction) are
# NOT executed by this suite to avoid wiping the SPI flash.
#
# Board-aware: not every board is fitted with the external SPI flash chip, and
# test.flash_info / test.flash_read / test.spi_mode sit inside
# #ifdef THETAGP_CFG_HAS_FLASH (test_cmd_handler.cpp:265-272), so on a board without it
# they are not in the build and answer as unknown commands. The suite asks the
# board (sys.get_usage.ext_flash_total_sectors) instead of assuming an answer:
# with no chip those checks are SKIP with the reason, and with the chip they are
# exactly as strict as before. Stage 2 as a whole is SKIP when the test domain
# is absent (test.mem_info is the probe), and so are the three Stage 3 checks
# that compare sys.get_usage against the raw test.mem_info readout — a build
# without the domain has nothing to compare, so "this build has no test domain"
# no longer reads as a list of failures.

# The expected response fields of a command are not written down here: they are
# read from protocol/proto_fields.json, which scripts/gen_proto.py derives from
# protocol/protocol.toml (the single source of truth for the protocol). A field
# added to, removed from, or reordered in protocol.toml therefore reaches these
# checks by regenerating, without an edit to this file. The manifest is a
# generated artifact and is not committed (.gitignore), so a checkout that has
# never run the generator has none — load_field_manifest() then stops the run
# with the command that produces it, before the port is opened, rather than
# letting every field check compare against an empty list.
#
# The manifest carries the sha256 of the protocol.toml it was derived from, and
# load_field_manifest() recomputes it: a manifest left behind by an edit to
# protocol.toml describes a shape the protocol no longer has, and comparing
# against it would let every field check below pass against the wrong source.
# That is a failure of the input, not of the device, so it stops the run the
# same way a missing manifest does — regenerating is the fix.
#
# The manifest carries a second digest beside that one, of the generator that
# emitted it, and that is recomputed too. The field lists come from the emitter
# as much as from the TOML, so a manifest whose protocol.toml is untouched can
# still have been written by an emitter that has changed since — and that is the
# quieter of the two stalenesses, because a field the emitter stopped writing
# makes the lists read from here shorter, and a check that only looks up the
# fields it expects finds the ones it expects and passes. Both digests have to
# match the files on disk, or the run stops.
#
# A subset of a response is stated the same way. Which fields of
# sys.get_task_info the per-task accounting checks below require is the set
# protocol.toml marks role = "accounting": a field that joins or leaves that set
# does so in the source, and those checks follow it without an edit to this
# file, exactly as they already follow the full field list.
#
# The other marking the source carries, role = "task_counters", is guarded the
# same way and for the same reason: the fields it marks are reported only in a
# build that compiles the task counters in, so a run whose manifest no longer
# marks them stops before the port is opened — without the marking the response
# table claims both keys are written in every build, and a build that does not
# compile the counters in answers them with zeros.
#
# Stage 5 is the config domain (config.*), and three things about it shape that
# stage. They are stated where they are used as well; the summary is here.
#
# One request in flight. The frame layer holds a single TX slot while the task
# that dispatches commands drains a whole tick's queue at once, so requests
# written back to back leave only the last reply on the wire
# (frame_layer.cpp:197-213; the stage measures it). Every check below waits for
# its own reply before the next request goes out, and one check states the limit
# rather than depending on it silently.
#
# config.save has a precondition. Writing the configuration out is refused when
# the active profile is the factory one — error_code 8, ERR_INVALID_STATE
# (config_cmd_handler.cpp:394-401) — so the stage asks the board which profile
# is active (profile.status) and holds the reply to what that answer allows: an
# ok reply has to carry persisted true, a refusal has to be that code with the
# reason naming the state. A check that demanded persisted true regardless would
# be reading the board's state as the device's failure, and one that accepted
# either would not be checking the precondition at all.
#
# The reply shapes. A success reply carries the response envelope in front of
# the fields its command declares; an error reply carries the three keys
# [error_reply] declares and neither `cmd` nor `queued`. Both are read from
# protocol.toml — the generated manifest names the commands but not the
# envelope (T-35) — and the error codes the negative checks expect come from
# [error_codes]. A copy kept in this file would stay green while the protocol
# source renamed a key, which is the shape of failure the envelope assertions
# are here to catch.
#
# The save reply's dropped-keys report is read off the wire twice over, because
# the reply alone cannot say whether it is right: the count it carries is a
# statement about two bodies — the one the save replaced and the one it wrote —
# and both are on the wire (profile.get, the command the store's own read rules
# answer). The stage reads them around a save and holds the reply to what they
# say: a save that carried every key over has no count to report and its reply
# must not carry the key at all (`optional = true` in protocol.toml, and the
# write site's `if (droppedKeys > 0)`), which is the one failure a compiler and
# the generator cannot see — a site that keeps the flag name and loses the
# condition compiles and generates exactly as before, and only the reply's bytes
# give it away. This is the judgement the stage's other save checks do not make:
# they read `persisted`, the shape, and the profile's content, and a count that
# is always written, or written from the wrong pair of bodies, satisfies all
# three.

import hashlib
import json
import os
import sys
import time
import tomllib
from pathlib import Path

from cdc_serial import open_serial, readline, read_bytes, TestContext

# The dispatcher answers a command it has no handler for with this reason
# (dispatcher.cpp:95-102). It is the only thing that separates "this command is
# not in this build" from "the command is there and rejected its arguments":
# both carry error_code 1, because the length guard of test.flash_read answers
# with error_code 1 too (test_cmd_handler.cpp:221-228). A check that only asks "did an
# error come back" therefore scores those two opposite results the same way.
UNKNOWN_CMD_REASON = "unknown command"

# Memory regions exported by the linker script
RAM_REGIONS = ("dtcm", "axi", "d2", "d3", "itcm")
REGIONS = ("flash",) + RAM_REGIONS

# The wire keys of one entry of the sys.get_usage region list. protocol.toml
# declares that field an array of the record type MemoryRegion, whose four
# fields the manifest carries under `types`; the shape the reply is held to is
# the same four keys: a region name and its size, used and reserved bytes.
REGION_MEMBERS = {"name", "size", "used", "reserved"}

# Region capacities = linker script LENGTH. Board constants: only a board
# change moves them, so asserting them is build independent.
REGION_SIZES = {
    "flash": 2097152,   # 2 MB
    "dtcm": 131072,     # 128 KB
    "axi": 524288,      # 512 KB
    "d2": 294912,       # 288 KB
    "d3": 65536,        # 64 KB
    "itcm": 64512,      # 63 KB
}

# The generated field manifest: protocol/proto_fields.json, one entry per
# [[commands]] of protocol.toml, each with its ordered request/response fields.
REPO_ROOT = Path(__file__).resolve().parents[2]
PROTOCOL_DIR = REPO_ROOT / "protocol"
FIELDS_MANIFEST = PROTOCOL_DIR / "proto_fields.json"

# The protocol source the manifest is derived from. The manifest records this
# file's sha256 when it is written, so a manifest that is there but older than
# the source is detectable rather than silently authoritative.
PROTOCOL_SOURCE = PROTOCOL_DIR / "protocol.toml"

# The generator the manifest is emitted by: the entry point, the file a run
# names. It is not the coverage — the manifest names the files its own digest
# was computed over and those are what is hashed below — it is the one file
# every coverage has to include, so the coverage cannot be narrowed to nothing
# that matters.
GENERATOR_ENTRY = REPO_ROOT / "scripts" / "gen_proto.py"


def source_sha256(path):
    """SHA-256 of a file's bytes — what the manifest records of its source."""
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def generator_sha256(sources):
    """The manifest's generator digest, recomputed from the files it names.

    The generator derives it the same way — each source's path relative to the
    repository root, then its bytes, NUL-separated, in the order named — so the
    two sides agree on the algorithm and not on a value copied between them.
    """
    digest = hashlib.sha256()
    for name in sources:
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update((REPO_ROOT / name).read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def check_generator(manifest, path):
    """Abort unless the manifest was emitted by the generator on disk.

    The other half of a manifest's provenance: ``source_sha256`` says which
    protocol.toml was read, this says which emitter read it. A manifest can
    match its source and still be stale — the emitter decides which fields reach
    the manifest, under which keys and in which order, so one that has changed
    since produces a manifest that describes neither the TOML on disk nor the
    firmware. Exit 2, on the same terms as the source check: it is a failure of
    the input, the fix is to regenerate, and a run that went ahead would be
    scoring the protocol against a shape nothing derives any more.

    The files hashed are the ones the manifest names, so a generator that grows
    a module extends its own coverage without an edit here; the entry point is
    required to be among them, so that coverage cannot be narrowed to something
    that is not the generator; and a manifest that names sources outside the
    repository, or files that are not there, is not something this can check, so
    it stops the run rather than being believed. A manifest old enough to carry
    no generator digest at all is stale by the same reasoning as one that
    carries no source digest: it cannot say what emitted it.
    """
    entry = GENERATOR_ENTRY.relative_to(REPO_ROOT).as_posix()
    recorded = manifest.get("generator_sha256")
    sources = manifest.get("generator_sources")

    def unverifiable(reason):
        print(f"ERROR: protocol field manifest is stale: {path}", file=sys.stderr)
        print(f"       {reason}", file=sys.stderr)
        print("       Its field lists were emitted by a generator this run "
              "cannot check against, so what they describe is unknown.",
              file=sys.stderr)
        print("       Run: python3 scripts/gen_proto.py", file=sys.stderr)
        sys.exit(2)

    if not isinstance(sources, list) or not sources:
        unverifiable("It carries no generator_sources — it was generated "
                     "before the manifest recorded the generator it came from.")
    if entry not in sources:
        unverifiable(f"It names generator_sources {sources}, which does not "
                     f"include the generator entry point {entry}.")
    for name in sources:
        if (not isinstance(name, str) or Path(name).is_absolute()
                or ".." in Path(name).parts):
            unverifiable(f"It names the generator source {name!r}, which is "
                         "not a path inside the repository.")
        if not (REPO_ROOT / name).is_file():
            unverifiable(f"It names the generator source {name!r}, which is "
                         "not on disk.")
    actual = generator_sha256(sources)
    if recorded != actual:
        print(f"ERROR: protocol field manifest is stale: {path}", file=sys.stderr)
        if recorded is None:
            print("       It carries no generator_sha256 — it was generated "
                  "before the manifest recorded the generator it came from.",
                  file=sys.stderr)
        else:
            print(f"       It records the generator {', '.join(sources)} "
                  f"at sha256 {recorded}.", file=sys.stderr)
        print(f"       That generator is at sha256 {actual}.", file=sys.stderr)
        print("       The manifest was emitted by a different generator, so "
              "the expected fields below it are the ones that generator "
              "wrote, not the ones the generator on disk derives from "
              "protocol.toml.", file=sys.stderr)
        print("       Run: python3 scripts/gen_proto.py", file=sys.stderr)
        sys.exit(2)
    return manifest


def load_field_manifest(path=FIELDS_MANIFEST, source=PROTOCOL_SOURCE):
    """The generated field manifest, or a hard error naming how to produce it.

    A missing manifest aborts the run with exit 2. It is deliberately not a
    skipped check: the checks below read their expected fields from here, so a
    run without it would silently compare against nothing — a result that looks
    like the suite did its work when the input it works from was never there.

    A manifest that is present but stale aborts the same way, with exit 2: the
    digest it recorded of its source no longer matches the source on disk, so
    its field lists describe a protocol.toml that has been edited since. Those
    lists are the expected values of every field check below, and a run against
    them would pass while checking a protocol the firmware no longer speaks —
    the failure would surface later, on the wire, as a mismatch blamed on the
    device. Regenerating is the fix; nothing else clears the gate.

    The generator is checked the same way, by check_generator(): a manifest is
    only current if both the protocol it was read from and the emitter that read
    it are the ones on disk, since the fields below are derived from both.
    """
    if not path.is_file():
        print(f"ERROR: protocol field manifest not found: {path}", file=sys.stderr)
        print("       It is derived from protocol/protocol.toml and is not "
              "committed.", file=sys.stderr)
        print("       Run: python3 scripts/gen_proto.py", file=sys.stderr)
        sys.exit(2)
    with open(path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    recorded = manifest.get("source_sha256")
    actual = source_sha256(source)
    if recorded != actual:
        print(f"ERROR: protocol field manifest is stale: {path}", file=sys.stderr)
        if recorded is None:
            print("       It carries no source_sha256 — it was generated before "
                  "the manifest recorded one.", file=sys.stderr)
        else:
            print(f"       It records source {manifest.get('source')} at "
                  f"sha256 {recorded}.", file=sys.stderr)
        print(f"       {source} is at sha256 {actual}.", file=sys.stderr)
        print("       The manifest is older than the protocol it was derived "
              "from, so the expected fields below it are out of date.",
              file=sys.stderr)
        print("       Run: python3 scripts/gen_proto.py", file=sys.stderr)
        sys.exit(2)
    return check_generator(manifest, path)


def command_entry(manifest, cmd):
    """One command's entry in the manifest, or a hard error naming the fix."""
    entry = manifest["commands"].get(cmd)
    if entry is None:
        print(f"ERROR: command '{cmd}' is not in {FIELDS_MANIFEST} — the "
              f"manifest was generated from a different protocol.toml",
              file=sys.stderr)
        print("       Run: python3 scripts/gen_proto.py", file=sys.stderr)
        sys.exit(2)
    return entry


def response_fields(manifest, cmd):
    """The json names a command's response carries, in the declared order.

    The order is the one the manifest kept from protocol.toml, which is the
    order the firmware writes the fields in.
    """
    return tuple(f["json"] for f in command_entry(manifest, cmd)["response"])


def role_fields(manifest, cmd, role):
    """The json names of a command's response fields carrying one role.

    A role is what the protocol source says about a field besides its type and
    its place in the list. `accounting` marks a per-task accounting field: one
    a response carries in every build, which is what makes a check on it mean
    the same thing in each of them. Reading the subset here, from the same
    artifact as the field list itself, is what keeps the marking and the checks
    that depend on it from drifting apart.
    """
    return tuple(f["json"] for f in command_entry(manifest, cmd)["response"]
                 if f.get("role") == role)


def field_int(resp, key):
    """Response field as int, or None when missing/not an integer."""
    value = (resp or {}).get(key)
    return value if isinstance(value, int) else None


def field(resp, key):
    """Response field, or None when there is no usable response."""
    if not isinstance(resp, dict):
        return None
    return resp.get(key)


def status_of(resp):
    """The status field of a response, or None when there is none."""
    return field(resp, "status")


def cmd_routed(resp):
    """True when an answer came from the command's own handler.

    A command this build does not have is answered by the dispatcher itself,
    with the reason "unknown command" and error_code 1 (dispatcher.cpp:95-102).
    Every other answer — including the error of a handler that rejected its
    arguments — came from a handler, so the command exists. test.flash_info and
    test.flash_read sit inside #ifdef THETAGP_CFG_HAS_FLASH (test_cmd_handler.cpp:265),
    so on a board without the SPI flash chip they are exactly this case.
    """
    return isinstance(resp, dict) and resp.get("reason") != UNKNOWN_CMD_REASON


def brief(resp):
    """A response on one line, for a check detail or a skip reason."""
    if resp is None:
        return "(no response: timeout)"
    return json.dumps(resp, separators=(",", ":"))


def has_fields(resp, keys):
    """True when every named field is present and integral."""
    for key in keys:
        if field_int(resp, key) is None:
            return False
    return True


def eq_fields(resp_a, key_a, resp_b, key_b):
    """True when both fields are present and equal."""
    value = field_int(resp_a, key_a)
    return value is not None and value == field_int(resp_b, key_b)


def le_fields(resp, low_key, high_key):
    """True when low_key <= high_key, both present and integral."""
    low = field_int(resp, low_key)
    high = field_int(resp, high_key)
    if low is None or high is None:
        return False
    return low <= high


def sum_regions(resp, key_fmt):
    """Sum of the five RAM region fields named by key_fmt, or None."""
    total = 0
    for region in RAM_REGIONS:
        value = field_int(resp, key_fmt.format(region=region))
        if value is None:
            return None
        total += value
    return total


def region_entries(resp):
    """The sys.get_usage region list, as the objects the protocol declares.

    protocol.toml declares that field one `any` — an array of objects, because
    the type vocabulary carries no array-of-object type — so its shape is held
    here rather than derived: every entry is an object carrying exactly the
    four wire keys the source names, with a name and three counts. None when
    the reply carries no such array.
    """
    entries = field(resp, "regions")
    if not isinstance(entries, list) or not entries:
        return None
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != REGION_MEMBERS:
            return None
        if not isinstance(entry["name"], str) or not entry["name"]:
            return None
        for member in sorted(REGION_MEMBERS - {"name"}):
            value = entry[member]
            if not isinstance(value, int) or isinstance(value, bool):
                return None
    return entries


def regions_sum(resp, member):
    """Σ of one member over the sys.get_usage region list, or None."""
    entries = region_entries(resp)
    if entries is None:
        return None
    return sum(entry[member] for entry in entries)


def regions_match_mem(usage, mem):
    """Every region of the usage list equals the raw mem_info readout of it.

    The two domains are matched by the region names the usage reply reports:
    an entry naming a region mem_info does not report has nothing to be
    compared against and fails here instead of passing over the difference.
    """
    entries = region_entries(usage)
    if entries is None or len(entries) != len(RAM_REGIONS):
        return False
    for entry in entries:
        for member in ("size", "used"):
            if field_int(mem, f"{entry['name']}_{member}") != entry[member]:
                return False
    return True


def region_used_ok(resp, region):
    """used == end - base and used <= size for one region."""
    base = field_int(resp, f"{region}_base")
    end = field_int(resp, f"{region}_end")
    size = field_int(resp, f"{region}_size")
    used = field_int(resp, f"{region}_used")
    if base is None or end is None or size is None or used is None:
        return False
    return used == end - base and used <= size


def reserve_ok(mem):
    """reserved == stack + heap and live == sum(RAM used) - reserved."""
    reserved = field_int(mem, "ram_reserved_bytes")
    stack = field_int(mem, "stack_bytes")
    heap = field_int(mem, "heap_bytes")
    live = field_int(mem, "ram_live_bytes")
    ram_used = sum_regions(mem, "{region}_used")
    if (reserved is None or stack is None or heap is None
            or live is None or ram_used is None):
        return False
    return reserved == stack + heap and live == ram_used - reserved


def ext_flash_ok(usage):
    """used + free + reserved == total for the external SPI flash.

    The reserved sectors ahead of the User Ring appear in neither used nor
    free, so the sectors of the chip close only with that third term.
    """
    total = field_int(usage, "ext_flash_total_sectors")
    used = field_int(usage, "ext_flash_used_sectors")
    free = field_int(usage, "ext_flash_free_sectors")
    reserved = field_int(usage, "ext_flash_reserved_sectors")
    if total is None or used is None or free is None or reserved is None:
        return False
    return used + free + reserved == total


def ram_totals_match(usage, mem):
    """usage aggregates agree with the per-region raw readout of mem_info."""
    used = sum_regions(mem, "{region}_used")
    total = sum_regions(mem, "{region}_size")
    reserved = field_int(mem, "ram_reserved_bytes")
    if used is None or total is None or reserved is None:
        return False
    return (field_int(usage, "ram_used_bytes") == used
            and field_int(usage, "ram_total_bytes") == total
            and field_int(usage, "ram_reserved_bytes") == reserved)


# sys.get_task_info — the per-task accounting checks. The fields they require
# are the ones protocol.toml marks role = "accounting"; avgExecUs / avgDeltaUs
# are the two 8-sample moving averages the scheduler carries in tenths of a
# microsecond, which the handler reports in whole microseconds.

# The five field names task_info_ok() reads to compare them against each other.
# That is a fact about the check and not about the protocol, so they are named
# here; that they are part of the accounting subset is a fact about the
# protocol, so the subset is read from the source — and main() refuses to run if
# the two no longer agree, rather than let a comparison keyed on a field the
# subset dropped turn into a KeyError (or, worse, be skipped).
TASK_INFO_INVARIANTS = ("avgCycleUs", "actualHz", "maxExecUs", "avgExecUs",
                        "totalExecUs")

# The fields sys.get_task_info reports only in a build that compiles the task
# counters in. Whether they are conditional is a fact about the protocol, so
# that is read from the source and main() refuses to run when the marking that
# says so is gone. The two names below are a fact about the guard and not about
# the protocol: they are the fields this suite knows to look for. It does not
# compare them against a device response — they are not part of the accounting
# subset, so no check here reads them off the wire — it holds the declaration
# the response table and the firmware both follow.
TASK_COUNTER_FIELDS = ("runCount", "lateCount")

# Existing TIDs: 0 SYSTEM/LOAD, 1 SYSTEM/UPDATE, 2 GAMEPAD/CORE,
# 3 TEST/CMD_PROC. TIDs beyond the build's task set answer with
# ERR_INVALID_PARAM instead.
TASK_IDS = (0, 1, 2, 3)


# ── The config domain (Stage 5) ────────────────────────────────────────────

# The six commands the domain carries, in the order the protocol declares them
# (protocol.toml's config block). main() holds the manifest to all six before
# the port is opened: the stage reads each command's declared response fields
# from there, so a manifest generated from a protocol without one of them would
# leave those checks comparing against nothing.
CONFIG_COMMANDS = (
    "config.set_key",
    "config.get_key",
    "config.list_keys",
    "config.save",
    "config.load",
    "config.factory_reset",
)

# The key table this firmware carries, as `config.list_keys` reports it: the
# name a caller sends, the range the key accepts, whether the key needs a reboot
# and the rest of the columns key_table.cpp holds beside them — count is the
# field's element count, and accepts_unmapped says the entry also takes the
# unmapped sentinel beside its range (key_table.h:39-48, :52-59).
#
# A transcription of the table rather than a list read from the protocol: the
# manifest carries the element's fields (protocol.toml declares list_keys' `keys`
# as an array of the record type ConfigKeyEntry) but not the table's values, and
# the point of the check this feeds is that the table and the
# list a host is told about stay in step — the list is derived from the table
# and nothing derives it into a file this suite reads. A key added to the table
# without reaching this tuple turns that check red instead of being compared
# against nothing, which is what the ADR's D1 criterion asks for. The values
# move with the table: map.socd_mode's max is SOCDMode's last enumerator, so an
# enumerator added to that enum moves both and the red is the prompt to say so
# here too.
CONFIG_KEYS = (
    # name, min, max, reboot, count, accepts_unmapped
    ("map.socd_mode", 0, 4, False, 1, False),
    ("map.four_way", 0, 1, False, 1, False),
    ("map.btn_map", 0, 31, False, 32, True),
)

# The slot value standing for a physical key the board maps to no button. It
# lies outside every range above, which is why the table carries the flag that
# admits it: a map holding unmapped slots could not be written back otherwise
# (T-36).
CONFIG_BTN_UNMAPPED = 255

# Slots in map.btn_map — one per physical key (config_defaults.h:47).
CONFIG_BTN_SLOTS = 32

# A btn_map holding the sentinel in some slots and button bits in others: the
# value the sentinel half of the round trip writes. The first four slots carry
# B1..B4's bit indexes and the slots behind them are unmapped, so one write
# covers both halves of the accepted domain and the value is not one the table
# holds as it stands.
CONFIG_BTN_SENTINEL = (4, 5, 255, 255) + (255,) * 28

# The btn_map a factory reset leaves in the store, on this board: the key table
# of configs/BoringTechH743/BoardConfig.toml ([0,"B1"] .. [3,"B4"]) converted to
# the bit indexes of GAMEPAD_MASK_B1..B4 (gamepad_state.h:60-63 — bits 4..7) with
# every slot the board does not list left at the sentinel, which is the
# conversion config_defaults.h:80-91 does at compile time. Board constants, in
# the same sense as REGION_SIZES above: a board with a different key table moves
# this tuple, and the check that reads it is what says which board the reset
# left the table looking like.
CONFIG_DEFAULT_BTN_MAP = (4, 5, 6, 7) + (255,) * 28

# The appearances an envelope key can declare for the reply side and still be
# carried by one ([envelope] documents the three words; a key of `always` is in
# every reply, one of `some` is in some of them).
REPLY_APPEARANCES = ("always", "some")


def load_protocol_shape(path=PROTOCOL_SOURCE):
    """The reply shapes and error codes protocol.toml declares, or a hard error.

    Three things the generated field manifest does not carry and the config
    stage's checks are stated in terms of:

      * the keys a reply carries in front of a command's own fields — the
        response envelope, declared in [envelope];
      * the keys an error reply carries — `status`, which the envelope declares
        as appearing in every reply, plus what [error_reply] declares;
      * the numbers [error_codes] gives the codes the negative checks expect, so
        those checks are stated in the codes the protocol names rather than in
        digits copied out of it.

    Reading them here rather than copying them into this file is what makes a
    rename reach these checks: the firmware's format strings are written by hand
    (T-40), so a key renamed in the source and not in the firmware is a
    divergence this can catch, and a tuple kept here would pass over it (T-30).

    A section this needs and cannot find stops the run with exit 2, the way a
    missing or stale manifest does: the checks below would otherwise be scoring
    the device against a shape nothing declares. Nothing here writes the file it
    reads — the manifest's digests of the source are unchanged by a run, and
    load_field_manifest() verifies them before this is called.
    """
    try:
        with open(path, "rb") as fh:
            proto = tomllib.load(fh)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        print(f"ERROR: cannot read the protocol source {path}: {exc}",
              file=sys.stderr)
        sys.exit(2)

    def section(name):
        found = proto.get(name)
        if not isinstance(found, dict) or not found:
            print(f"ERROR: {path} declares no [{name}] section.", file=sys.stderr)
            print("       The config stage states the shape of a reply in terms "
                  "of it, so a run without it has nothing to compare against.",
                  file=sys.stderr)
            sys.exit(2)
        return found

    def reply_keys(entries, what):
        """The wire keys of a section's entries that appear in a reply.

        `json` is the key on the wire; the section's own key is the name a
        target writes it as and validate_envelope() holds the two equal, so a
        section missing the column is read under its own name rather than
        refused. An entry whose `appears` column does not say for the reply side
        is one this cannot place, and that is a failure of the input.
        """
        keys = []
        for name, entry in entries.items():
            appears = entry.get("appears") if isinstance(entry, dict) else None
            if not isinstance(appears, dict) or "reply" not in appears:
                print(f"ERROR: {path}: [{what}].{name} declares no 'reply' "
                      f"cell in its 'appears' column.", file=sys.stderr)
                sys.exit(2)
            if appears["reply"] in REPLY_APPEARANCES:
                keys.append(entry.get("json", name))
        if not keys:
            print(f"ERROR: {path}: [{what}] declares no key that appears in a "
                  f"reply.", file=sys.stderr)
            sys.exit(2)
        return tuple(keys)

    envelope_entries = section("envelope")
    always_keys = tuple(
        entry.get("json", name)
        for name, entry in envelope_entries.items()
        if entry.get("appears", {}).get("reply") == "always")
    if not always_keys:
        print(f"ERROR: {path}: [envelope] declares no key that appears in "
              f"every reply.", file=sys.stderr)
        print("       An error reply is stated as those keys plus "
              "[error_reply]'s.", file=sys.stderr)
        sys.exit(2)

    codes = {}
    for name, entry in section("error_codes").items():
        if isinstance(entry, dict) and isinstance(entry.get("code"), int):
            codes[name] = entry["code"]
    for needed in ("ERR_INVALID_PARAM", "ERR_INVALID_STATE"):
        if needed not in codes:
            print(f"ERROR: {path} declares no [error_codes].{needed}.",
                  file=sys.stderr)
            print("       The config stage's negative checks are stated in that "
                  "code.", file=sys.stderr)
            sys.exit(2)

    return {
        "success": reply_keys(envelope_entries, "envelope"),
        "error": always_keys + reply_keys(section("error_reply"), "error_reply"),
        "codes": codes,
    }


def success_shape_ok(resp, cmd, manifest, envelope_keys):
    """A success reply: the envelope, then the fields the command declares.

    The keys beyond the envelope are read from the manifest, so this says both
    that the reply opens with the envelope and that it carries nothing the
    protocol does not declare for it. The envelope keys are the ones
    load_protocol_shape() read from the source: every success reply of this
    domain carries all of them, which is a fact about this domain's emitters —
    config_cmd_handler.cpp's eight format strings for the six commands and the
    two refusals — and is what this asserts on the wire.

    A declared field the protocol marks `optional` may be absent from a reply,
    so the declared list is read as both a floor and a ceiling: every field the
    manifest does not mark optional has to be there, and nothing outside the
    envelope plus the declared fields may be. Which replies leave such a key out
    is the firmware's rule and not this check's — config.save writes
    `dropped_keys` only when it dropped one — and a field with no `optional` in
    the manifest is required, which is what every declared response field was
    before the column existed.
    """
    if not isinstance(resp, dict):
        return False
    declared = set(response_fields(manifest, cmd))
    optional = {f["json"] for f in command_entry(manifest, cmd)["response"]
                if f.get("optional")}
    carried = set(resp)
    must_carry = (set(envelope_keys) | declared) - optional
    may_carry = set(envelope_keys) | declared
    return must_carry <= carried <= may_carry


def error_shape_ok(resp, error_keys):
    """An error reply: exactly the keys [error_reply] declares beside status.

    A reply carrying `cmd` is not one of these — the dispatcher's answer to a
    command this build does not have carries it (dispatcher.cpp:95-102) — so
    this is also what separates a refused argument from a missing handler.
    """
    return isinstance(resp, dict) and set(resp) == set(error_keys)


def int_value(value):
    """A JSON number that is an integer, and not a bool.

    `isinstance(True, int)` is True in Python, so field_int() above reads a bool
    as an integer — harmless for the byte sizes and counts it was written for,
    and not harmless here, where a `min` of false would compare equal to min 0.
    """
    return isinstance(value, int) and not isinstance(value, bool)


# ── The save reply's dropped-keys report ───────────────────────────────────

# The two limits Json::missingKeyCount's lookup walks within (json.cpp: the
# chain of names it carries and the dotted path it builds a format from). They
# are transcription of a fact about the firmware's comparison, which is why they
# are stated here: past either one the firmware answers "unknown" as 0, and a
# check that scored the reply against a count this file derived past them would
# be demanding a number the device has no way to produce.
KEY_CHAIN_MAX = 8
LOOKUP_PATH_MAX = 122


def plain_name(name):
    """Whether a member name can be spelled as one name of a dotted path.

    The firmware's isPlainName (json.cpp): an empty name reaches nothing, and one
    carrying '.' would be read as two names while one carrying the brackets of an
    array element would be read as an element.
    """
    return bool(name) and not any(c in name for c in ".[]")


def key_paths(node, chain=()):
    """Every object key of a parsed body, by its chain of names, and whether the
    firmware's comparison is defined for all of them.

    A key is a member of an object, named by the chain that reaches it; the
    elements an array holds are not keys, and the member holding the array is
    one. That is Json::missingKeyCount's definition of a key (json.h), and it is
    what makes this count comparable with the one the reply reports.

    The second half is namability: a name that cannot be spelled into a lookup, a
    chain deeper than KEY_CHAIN_MAX names and a path longer than LOOKUP_PATH_MAX
    bytes are the three cases the firmware answers 0 for, and the caller has to
    SKIP rather than score the reply against a number that is really "unknown".
    The name checks run on the name itself and not on the joined path, where a
    name carrying '.' would read as two names.
    """
    paths = []
    namable = True
    if isinstance(node, dict):
        for name, value in node.items():
            if not plain_name(name):
                namable = False
            path = ".".join(chain + (name,))
            paths.append(path)
            if len(chain) + 1 > KEY_CHAIN_MAX or len(path) > LOOKUP_PATH_MAX:
                namable = False
            inner, inner_namable = key_paths(value, chain + (name,))
            paths.extend(inner)
            namable = namable and inner_namable
    return paths, namable


def not_carried_over(replaced, written):
    """Keys of the replaced body the written body does not carry, or None.

    `replaced` and `written` are the two bodies the save had in hand, as
    profile.get reported them. None is the answer for a pair the firmware's
    comparison has none for — a body that does not parse, or a key of either
    body it cannot name (key_paths) — so the caller SKIPs instead of scoring
    against a number nothing derives.
    """
    try:
        source = json.loads(replaced)
        carrier = json.loads(written)
    except (json.JSONDecodeError, TypeError):
        return None
    source_paths, source_namable = key_paths(source)
    carrier_paths, carrier_namable = key_paths(carrier)
    if not source_namable or not carrier_namable:
        return None
    carried = set(carrier_paths)
    return sum(1 for path in source_paths if path not in carried)


def drop_report_ok(reply, expected, key):
    """Whether a save reply reports exactly what the save did not carry over.

    `expected` is not_carried_over()'s count over the two bodies on the wire,
    and `key` is the name protocol.toml gives the field. The rule is the one the
    protocol states for it, and the whole of it: the key is written only when
    that number is not zero, and what it says is that number. Two failures meet
    here and neither is visible to the compiler or the generator, because both
    are about *when* and *what* a site writes rather than whether the field
    exists:

      * a site that keeps the field and loses the condition — writing the key
        unconditionally, which answers a save that left nothing behind with a
        `dropped_keys` of 0, and a save that carried every key over with the one
        reply this command has always sent;
      * a site that writes a count of its own — one taken against the wrong pair
        of bodies, or against only part of one — which passes any check that
        only asks whether the number is plausible and fails here.

    A save that carried every key over therefore answers without the key at all,
    byte for byte as this reply read before the field existed (HEAD's format
    string for config.save is `{cmd:%Q,queued:%d,status:%Q,persisted:%B}`, so
    the reply ends at `persisted`), and that is what the last clause below
    asserts.
    """
    if not isinstance(reply, dict) or reply.get("status") != "ok":
        return False
    if expected == 0:
        return key not in reply
    return int_value(reply.get(key)) and reply.get(key) == expected


def save_bodies(ctx, pid):
    """The body profile.get reports for one profile, or None.

    The reply is three frames: a `profile.start` header carrying the length, the
    body's raw bytes, and a `profile.end` trailer (profile_cmd_handler.cpp
    :220-248). The length rule is the one ProfileStore::readBody uses — the body
    read to the first 0x00 or 0xFF — and the address rule is the one the store's
    own readProfile(id) uses, the newest Address-Ring entry for that id, so what
    this returns is the body the firmware's own read got.

    None when the command did not answer with a body: an error reply, a timeout,
    or a header with no usable length. Every caller turns that into a SKIP — a
    board that does not answer profile.get has nothing here to compare against,
    which is a fact about the board and not a result of the check.
    """
    req = {"cmd": "profile.get", "queued": ctx.queued, "id": pid}
    line = json.dumps(req, separators=(",", ":"))
    print(f">>> {line}")
    os.write(ctx.fd, (line + "\r\n").encode())
    ctx.queued += 1
    while True:
        header = readline(ctx.fd)
        if header is None:
            print("!!! TIMEOUT")
            return None
        try:
            obj = json.loads(header)
        except json.JSONDecodeError:
            print(f"[garbage] {header}")
            continue
        if obj.get("status") == "async":
            continue
        break
    if obj.get("cmd") != "profile.start":
        print(f"[unexpected reply] {header}")
        return None
    length = obj.get("len")
    if not int_value(length) or length <= 0:
        return None
    raw = read_bytes(ctx.fd, length)
    trailer = readline(ctx.fd)  # the profile.end frame, consumed here
    print(f"<<< {header}")
    print(f"<<< [body {length} bytes] "
          f"{raw.decode('utf-8', 'replace') if raw else '(none)'}")
    print(f"<<< {trailer}")
    if raw is None:
        return None
    return raw.decode("utf-8", "replace")


def key_value_ok(value, low, high, count, accepts_unmapped):
    """A value read back for a key: inside the domain that key declares.

    A scalar key reports one number, an array key its elements. Every number has
    to be one the key accepts — inside low..high, or the unmapped sentinel on a
    key whose entry carries the flag that admits it. A value the key's own write
    path would refuse is not a value the key holds, so this is the read half of
    the round trip's domain rather than a range check for its own sake.
    """
    if count == 1:
        return int_value(value) and low <= value <= high
    if not isinstance(value, list) or len(value) != count:
        return False
    for element in value:
        if not int_value(element):
            return False
        if low <= element <= high:
            continue
        if accepts_unmapped and element == CONFIG_BTN_UNMAPPED:
            continue
        return False
    return True


def config_request(ctx, cmd, **kw):
    """TestContext.send() for a request whose answer may be an error reply.

    send() pairs a reply with its request by the `cmd` key, and the config
    domain's refusals carry no `cmd` at all: its sendError() writes status,
    error_code and reason and nothing else (config_cmd_handler.cpp:158-167),
    which is the shape [error_reply] declares. send() given such a request reads
    past its reply as a reply to something else and times out, so the negative
    checks would score every refusal as a missing answer — the same shape of
    mistake this suite already had to fix for test.flash_read's length guard.
    This reads the wire itself and takes back the first reply that either names
    the command or carries no `cmd` with a status of error.

    One request in flight: the reply to this one is read before the caller can
    send another, which is what the single TX slot requires (frame_layer.cpp:197
    -213, and the check in Stage 5 that measures it).
    """
    req = {"cmd": cmd, "queued": ctx.queued, **kw}
    line = json.dumps(req, separators=(",", ":")) + "\r\n"
    print(f">>> {line.strip()}")
    os.write(ctx.fd, line.encode())
    ctx.queued += 1
    while True:
        resp = readline(ctx.fd)
        if resp is None:
            print("!!! TIMEOUT")
            return None
        try:
            obj = json.loads(resp)
        except json.JSONDecodeError:
            print(f"[garbage] {resp}")
            continue
        if obj.get("status") == "async":
            print(f"[async] {resp}")
            continue
        if obj.get("cmd") != cmd and not (obj.get("status") == "error"
                                          and "cmd" not in obj):
            print(f"[unexpected reply] {resp}")
            continue
        print(f"<<< {resp}")
        return obj


def task_info_ok(info, fields):
    """Per-task invariants for one sys.get_task_info response.

    `fields` is the accounting subset the protocol declares: every one of them
    has to be present and integral, and the TASK_INFO_INVARIANTS among them have
    to agree with each other.
    """
    if not has_fields(info, fields):
        return False
    if info["avgExecUs"] > info["maxExecUs"]:
        return False
    if info["totalExecUs"] < info["avgExecUs"]:
        return False
    cycle = info["avgCycleUs"]
    hz = info["actualHz"]
    return hz == (1000000 // cycle if cycle > 0 else 0)


def task_info_sigma_us_per_s(tasks):
    """Σ(avgExecUs × actualHz) over the tasks, in microseconds per second.

    The sum is where the execution time of every taskFunc call would land if
    the scheduler did nothing else, so it is comparable against the CPU load
    the SYSTEM/LOAD task derives from the scheduler's own execution timer.
    """
    total = 0
    for info in tasks:
        exec_us = field_int(info, "avgExecUs")
        hz = field_int(info, "actualHz")
        if exec_us is None or hz is None:
            return None
        total += exec_us * hz
    return total


def main():
    # The expected sys.get_usage fields, from the generated manifest rather
    # than a tuple copied into this file. Read before the port is opened: a
    # missing manifest is an error, not a run with nothing to compare against.
    manifest = load_field_manifest()
    usage_fields = response_fields(manifest, "sys.get_usage")

    # The per-task accounting fields the response has to carry, marked in the
    # protocol source. The invariants above are stated in five of them, so a
    # marking that no longer covers those five would leave them compared
    # against a response that may not carry them at all: that is a change of
    # the input, so it stops the run instead of scoring it.
    task_fields = role_fields(manifest, "sys.get_task_info", "accounting")
    unmarked = [f for f in TASK_INFO_INVARIANTS if f not in task_fields]
    if unmarked:
        print(f"ERROR: protocol.toml no longer marks {unmarked} as "
              f"role=\"accounting\" on sys.get_task_info.", file=sys.stderr)
        print("       The per-task invariants are stated in those fields; "
              "without the marking they would be read from a subset that no "
              "longer has to contain them.", file=sys.stderr)
        sys.exit(2)

    # The same gate for the other marking the response carries. These fields are
    # written only in a build that compiles the task counters in, so the marking
    # is what makes the response table conditional; without it the table writes
    # both keys in every build, and a build that does not compile the counters
    # in answers them with zeros — values under keys that read as measured. That
    # is a loss in the source and not on the device, so it stops the run here,
    # before the port is opened, rather than being reported as something the
    # board did wrong.
    counter_fields = role_fields(manifest, "sys.get_task_info", "task_counters")
    unmarked_counters = [f for f in TASK_COUNTER_FIELDS if f not in counter_fields]
    if unmarked_counters:
        print(f"ERROR: protocol.toml no longer marks {unmarked_counters} as "
              f"role=\"task_counters\" on sys.get_task_info.", file=sys.stderr)
        print("       Those fields are reported only in a build that compiles "
              "the task counters in, and the response table reads that "
              "condition from the marking: without it the table writes both "
              "keys unconditionally, and a build without the counters answers "
              "them with zeros.", file=sys.stderr)
        sys.exit(2)

    # The config domain's six commands have to be in the manifest before the
    # port is opened, for the reason the gates above are here: Stage 5 reads
    # each command's declared response fields from it, and a manifest generated
    # from a protocol that no longer carries one of them would leave those
    # checks comparing against nothing. command_entry() exits 2 naming the fix.
    for cmd in CONFIG_COMMANDS:
        command_entry(manifest, cmd)

    # The name config.save reports its count of left-behind keys under. Which
    # field that is is a fact about the protocol, so it is read from the source
    # (the manifest's response fields for the command) and the stage requires
    # exactly one optional field: the check at the end of the save branch is
    # stated in that field's rule — written when the count is not zero, absent
    # when it is — so a protocol that marks two of them, or none, leaves the
    # check nothing to score, and that is a failure of the input rather than a
    # result on the device.
    save_optional = [f["json"] for f in command_entry(manifest, "config.save")["response"]
                     if f.get("optional")]
    if len(save_optional) != 1:
        print(f"ERROR: protocol.toml marks {save_optional or 'no'} response field "
              f"of config.save as optional.", file=sys.stderr)
        print("       The save branch's dropped-keys check is stated in exactly "
              "one such field: it holds the reply to the two bodies the save had "
              "in hand, and without the marking it has no key to look for.",
              file=sys.stderr)
        sys.exit(2)
    dropped_key = save_optional[0]

    # The reply shapes the protocol declares, read from the protocol source:
    # the manifest carries no envelope (T-35), and the keys and codes a reply is
    # held to are the ones protocol.toml declares — [envelope], [error_reply]
    # and [error_codes] — so a rename or a renumbering in the source moves these
    # checks with it rather than passing over a copy kept in this file.
    shape = load_protocol_shape()

    fd = open_serial()
    time.sleep(1)
    ctx = TestContext(fd)
    ok = ctx.check

    # ── Stage 1: sys domain ────────────────────────────────────────────
    print("\n=== Stage 1: sys domain ===")
    r = ctx.send("sys.ping")
    ok("sys.ping", r and r.get("status") == "ok")

    r = ctx.send("sys.get_fw_version")
    ok("sys.get_fw_version",
       r and r.get("status") == "ok" and "version" in r)

    r = ctx.send("test.no_such_cmd")
    ok("unknown cmd",
       r and r.get("status") == "error" and r.get("error_code") == 1)

    # ── Stage 2: test domain (flash/memory, read-only) ─────────────────
    print("\n=== Stage 2: test domain (read-only commands) ===")

    # Precondition: does this build carry the test domain at all? It is compiled
    # in by the test-API switch (THETAGP_CFG_BUILD_TEST_API), and test.mem_info is
    # the one command in it that no board-specific #ifdef removes, so it is the probe.
    # Without this precondition a build without the domain scores every check below as
    # FAIL, which reads like a regression instead of "this board is not the one this
    # stage is about".
    mem_probe = ctx.send("test.mem_info")
    mem = mem_probe if status_of(mem_probe) == "ok" else None
    if mem is not None:
        ok("test domain present", True, detail="test.mem_info answers")
    else:
        ctx.skip("test domain present",
                 "test.mem_info -> %s: this build has no test domain "
                 "(the test-API switch is off), so the whole of Stage 2 is out of "
                 "scope for it" % brief(mem_probe))

    # Board fact, read from the board itself rather than assumed: not every
    # board is fitted with the external SPI flash chip. When it is absent the
    # firmware reports ext_flash_total_sectors = 0 and compiles test.flash_info,
    # test.flash_read and test.spi_mode out of the build entirely
    # (#ifdef THETAGP_CFG_HAS_FLASH, test_cmd_handler.cpp:265-272), so they answer as
    # unknown commands. Those checks are then "this board does not have the
    # thing under test" — SKIP, with the reason — and stay exactly as strict as
    # before on a board that does have the chip.
    usage_probe = ctx.send("sys.get_usage")
    ext_sectors = field_int(usage_probe, "ext_flash_total_sectors")
    has_ext_flash = (mem is not None and ext_sectors is not None
                     and ext_sectors > 0)
    if mem is None:
        flash_reason = "test domain absent on this board (see above)"
    else:
        flash_reason = (
            "no external SPI flash on this board: sys.get_usage reports "
            "ext_flash_total_sectors=%s, and test.flash_* / test.spi_mode are "
            "compiled out (#ifdef THETAGP_CFG_HAS_FLASH), so they answer as "
            "unknown commands" % ext_sectors
        )

    def check_test(name, passed, detail=""):
        """A check of the test domain: SKIP when the domain is absent."""
        if mem is not None:
            ok(name, passed, detail=detail)
        else:
            ctx.skip(name, "test domain absent on this board")

    def check_flash(name, passed, detail=""):
        """A flash-dependent check: SKIP (with the board reason) when this
        board has no external SPI flash, otherwise scored as before."""
        if has_ext_flash:
            ok(name, passed, detail=detail)
        else:
            ctx.skip(name, flash_reason)

    # test.flash_info — read-only flash geometry
    r = ctx.send("test.flash_info")
    check_flash("flash_info",
                status_of(r) == "ok"
                and field(r, "sizeBytes") is not None
                and field(r, "sectorSize") is not None)

    # test.mem_info — raw linker-symbol readout of the six memory regions
    # (the response of the precondition above, read again as a value)
    check_test("mem_info fields",
               mem is not None
               and all(has_fields(mem, [f"{region}_{suffix}"
                                        for suffix in ("base", "end", "size", "used")])
                       for region in REGIONS)
               and has_fields(mem, ["ram_reserved_bytes", "stack_bytes",
                                    "heap_bytes", "ram_live_bytes"]))

    # Region capacities are board constants (build independent)
    check_test("mem_info region sizes",
               mem is not None
               and all(field_int(mem, f"{region}_size") == size
                       for region, size in REGION_SIZES.items()))

    # used == end - base and used <= size, for every region
    check_test("mem_info used arithmetic",
               mem is not None
               and all(region_used_ok(mem, region) for region in REGIONS))

    # reserved = stack + heap; live = sum(RAM used) - reserved
    check_test("mem_info reserve arithmetic", reserve_ok(mem))

    # test.flash_read — read 32 bytes from flash start (read-only). Two
    # separate facts, scored separately: the command is there at all, and it
    # honours the length it was asked for.
    read_ok = ctx.send("test.flash_read", addr=0, len=32)
    check_flash("flash_read domain present", cmd_routed(read_ok),
                detail="test.flash_read answers as a routed command, not as "
                       "the dispatcher's unknown-command error")
    check_flash("flash_read 32B@0",
                status_of(read_ok) == "ok" and field(read_ok, "lenRead") == 32)

    # test.flash_read — an over-long length has to be rejected. This is the
    # check that used to pass for the wrong reason: "length rejected" and
    # "command does not exist" are opposite results and both arrive as
    # {status:error,error_code:1} (test_cmd_handler.cpp:221-228 vs
    # dispatcher.cpp:95-102), so the domain assertion above is what makes this
    # one mean what it says — if the command is missing, the length guard has
    # not been demonstrated and this reports FAIL instead of a free PASS.
    read_bad = ctx.send("test.flash_read", addr=0, len=99999)
    check_flash("flash_read invalid len rejected",
                cmd_routed(read_bad)
                and status_of(read_bad) == "error"
                and field(read_bad, "lenRead") is None,
                detail="len=99999 -> %s" % brief(read_bad))

    # test.spi_mode — set DMA mode (default, non-destructive)
    r = ctx.send("test.spi_mode", mode=1)
    check_flash("spi_mode DMA",
                status_of(r) == "ok" and field(r, "mode") == 1)

    # ── Stage 3: sys.get_usage — four-item resource report ─────────────
    print("\n=== Stage 3: sys.get_usage (four-item scope) ===")

    usage = ctx.send("sys.get_usage")
    # The declared fields, read from the manifest. `regions` is the one of them
    # with no scalar form (the source declares it `any`), so it is required as
    # the array the protocol describes rather than as a number: the scalar
    # fields have to be present, the list has to hold one entry per RAM region,
    # name them in order, and region_count has to be how many there are.
    usage_regions = region_entries(usage)
    ok("get_usage fields",
       usage is not None and usage.get("status") == "ok"
       and has_fields(usage, [f for f in usage_fields if f != "regions"])
       and usage_regions is not None
       and field_int(usage, "region_count") == len(usage_regions)
       and [entry["name"] for entry in usage_regions] == list(RAM_REGIONS),
       detail=brief(usage))

    # The entry shape of the region list, held on its own. The reply is written
    # through the writer function the generator emits for this command
    # (ThetaGP::Resp::sysGetUsage, protocol/proto_resp.h), which takes one value
    # per element; what that function does not carry is the meaning of an entry,
    # which protocol.toml states in prose and the device fills in. So it is held
    # here: one object per region, exactly the four wire keys the record type
    # declares, a region name that is a non-empty string and three counts.
    region_list = field(usage, "regions")
    ok("get_usage regions entry shape",
       isinstance(region_list, list) and len(region_list) > 0
       and all(isinstance(entry, dict) and set(entry) == REGION_MEMBERS
               and isinstance(entry["name"], str) and entry["name"]
               and all(int_value(entry[member]) and entry[member] >= 0
                       for member in ("size", "used", "reserved"))
               for entry in region_list),
       detail=brief(field(usage, "regions")))

    # Aggregate consistency — build independent regression checks. The RAM sum
    # is the region list's own used bytes, so the aggregate is compared against
    # the parts the same reply reports.
    usage_ram_used = regions_sum(usage, "used")
    ok("get_usage ram sum",
       usage_ram_used is not None
       and field_int(usage, "ram_used_bytes") == usage_ram_used)

    ok("get_usage ram bounds",
       le_fields(usage, "ram_used_bytes", "ram_total_bytes")
       and le_fields(usage, "ram_reserved_bytes", "ram_used_bytes"))

    ok("get_usage flash bounds",
       le_fields(usage, "mcu_flash_used_bytes", "mcu_flash_total_bytes"))

    ok("get_usage ext_flash bounds", ext_flash_ok(usage))

    # Region values must match the raw readout of test.mem_info. These three
    # checks need both domains at once — sys.get_usage on one side, the raw
    # test.mem_info on the other — so a build without the test domain has
    # nothing to compare against and they are SKIP with the reason, exactly as
    # in Stage 2 (check_test), instead of three failures that read like a
    # regression of a comparison the build was never able to make. With the
    # domain present they stay as strict as before.
    check_test("get_usage == mem_info regions",
               regions_match_mem(usage, mem))

    check_test("get_usage flash == mem_info flash",
               eq_fields(usage, "mcu_flash_used_bytes", mem, "flash_used")
               and eq_fields(usage, "mcu_flash_total_bytes", mem, "flash_size"))

    check_test("get_usage ram totals == mem_info", ram_totals_match(usage, mem))

    # ── Stage 4: sys.get_task_info — per-task time accounting ──────────
    print("\n=== Stage 4: sys.get_task_info (per-task accounting) ===")

    bad_tid = ctx.send("sys.get_task_info", tid=99)
    ok("task_info invalid TID rejected",
       bad_tid is not None and bad_tid.get("status") == "error"
       and bad_tid.get("error_code") == 2)

    tasks = []
    for tid in TASK_IDS:
        info = ctx.send("sys.get_task_info", tid=tid)
        if info is not None and info.get("status") == "ok":
            tasks.append(info)
        ok(f"task_info tid {tid}", task_info_ok(info, task_fields))

    ok("task_info task count", len(tasks) >= 4,
       detail=f"{len(tasks)} of {len(TASK_IDS)} TIDs answered")

    # Time decomposition: Σ(avgExecUs × actualHz) is what the task functions
    # themselves consume per second, while cpu_load_percent is measured on the
    # very same taskFunc interval, so the two describe the same quantity. The
    # 1.3 ceiling is a sanity bound — a missing µs/10th-µs scaling factor
    # would overshoot it by 10×.
    load = field_int(ctx.send("sys.get_usage"), "cpu_load_percent")
    sigma = task_info_sigma_us_per_s(tasks)
    sigma_percent = None if sigma is None else sigma / 10000.0
    if sigma_percent is None:
        print("  Σ(avgExecUs×actualHz) unavailable")
    else:
        print(f"  Σ(avgExecUs×actualHz) = {sigma} µs/s = "
              f"{sigma_percent:.2f}% ; cpu_load_percent = {load}%")
    ok("task_info exec accounting bounds",
       sigma_percent is not None and sigma_percent <= 130.0,
       detail=f"Σ={sigma_percent}% vs load={load}%")

    # Destructive commands (chip_erase / erase_sector / compaction)
    # are intentionally NOT executed — they would wipe the SPI flash.
    # They are validated by the profile test suite instead.

    # ── Stage 5: config domain ─────────────────────────────────────────
    print("\n=== Stage 5: config domain ===")

    def cfg_send(cmd, **kw):
        """Send one config request; return the reply and the queued it carried."""
        sent = ctx.queued
        return config_request(ctx, cmd, **kw), sent

    def config_ok(resp, cmd, sent):
        """A success reply: the envelope, the declared fields, the queue echo.

        The three are one shape and are checked as one: the reply carries the
        envelope keys plus exactly the fields protocol.toml declares for the
        command (read from the manifest, so a field added to the protocol
        reaches this without an edit here), and the `queued` it echoes is the
        request's value plus one ([envelope].queued) — the one place a host can
        pair a reply with the request it answers.
        """
        return (status_of(resp) == "ok"
                and success_shape_ok(resp, cmd, manifest, shape["success"])
                and field_int(resp, "queued") == sent + 1)

    def config_err(resp, code):
        """A refusal by the config domain, in the code the protocol names.

        The shape settles two things at once: an error reply carries the three
        keys [error_reply] declares and no `cmd`, so a reply carrying `cmd` came
        from somewhere else — the dispatcher's answer to a command this build
        does not have, which answers ERR_UNKNOWN_CMD with `cmd` and `queued`
        beside it (dispatcher.cpp:95-102). A check that only asked whether an
        error came back would score a missing handler and a refused argument the
        same way, which is the mistake this suite already fixed once for
        test.flash_read's length guard.
        """
        reason = field(resp, "reason")
        return (status_of(resp) == "error"
                and field_int(resp, "error_code") == code
                and error_shape_ok(resp, shape["error"])
                and isinstance(reason, str) and reason != "")

    # Precondition: does this build carry the config domain at all? Its handlers
    # are registered by the test system, so a build without that system answers
    # every config command as unknown, and the stage would read as a list of
    # failures rather than as out of scope. config.list_keys is read-only and
    # needs no other command to have answered first, so it is the probe.
    list_probe, _sent = cfg_send("config.list_keys")
    config_present = status_of(list_probe) == "ok"
    if config_present:
        ok("config domain present", True, detail="config.list_keys answers")
    else:
        ctx.skip("config domain present",
                 "config.list_keys -> %s: this build carries no config domain "
                 "(the test system that registers it is compiled out), so the "
                 "whole of Stage 5 is out of scope for it" % brief(list_probe))

    def check_config(name, passed, detail=""):
        """A check of the config domain: SKIP when the domain is absent."""
        if config_present:
            ok(name, passed, detail=detail)
        else:
            ctx.skip(name, "config domain absent on this build")

    # config.list_keys — the key table this firmware carries, and the four
    # members a host reads each entry by (D1)
    keys = field(list_probe, "keys")
    check_config("list_keys count",
                 field_int(list_probe, "count") == len(CONFIG_KEYS)
                 and isinstance(keys, list) and len(keys) == len(CONFIG_KEYS),
                 detail="count=%s, %s entries" % (
                     field_int(list_probe, "count"),
                     len(keys) if isinstance(keys, list) else brief(keys)))

    check_config("list_keys entry members",
                 isinstance(keys, list)
                 and all(isinstance(entry, dict)
                         and set(entry) == {"key", "min", "max", "reboot"}
                         for entry in keys),
                 detail=brief(keys))

    # The entry shape of the key list, held on its own: the reply is written
    # through the writer function the generator emits for this command
    # (ThetaGP::Resp::configListKeys, protocol/proto_resp.h), which takes one
    # value per element, and the meaning of an entry is what the record type
    # ConfigKeyEntry declares. Every entry is an object carrying the key's name
    # and the range it accepts, told in that order.
    check_config("list_keys keys entry shape",
                 isinstance(keys, list) and len(keys) > 0
                 and all(isinstance(entry, dict)
                         and isinstance(entry.get("key"), str)
                         and entry["key"]
                         and int_value(entry.get("min"))
                         and int_value(entry.get("max"))
                         and entry["min"] <= entry["max"]
                         for entry in keys),
                 detail=brief(keys))

    def key_entry_ok(entry, expected):
        """One entry of the key list against the table this build carries."""
        if not isinstance(entry, dict):
            return False
        name, low, high, reboot, _count, _unmapped = expected
        return (entry.get("key") == name
                and int_value(entry.get("min")) and entry.get("min") == low
                and int_value(entry.get("max")) and entry.get("max") == high
                and isinstance(entry.get("reboot"), bool)
                and entry.get("reboot") is reboot)

    check_config("list_keys values match the key table",
                 isinstance(keys, list) and len(keys) == len(CONFIG_KEYS)
                 and all(key_entry_ok(entry, expected)
                         for entry, expected in zip(keys, CONFIG_KEYS)),
                 detail=brief(keys))

    btn_max = None
    if isinstance(keys, list):
        for entry in keys:
            if isinstance(entry, dict) and entry.get("key") == "map.btn_map":
                btn_max = entry.get("max")

    # config.get_key — what each key holds before this stage writes anything.
    # The values are kept: every write below is undone by writing one of them
    # back, and the last checks of the stage read them again to say the board
    # was left as it was found. This suite is re-run per step (D3, D4), and a
    # run that read what the previous one left behind would be reading its own
    # leftovers.
    snapshot = {}
    for key, low, high, _reboot, count, unmapped in CONFIG_KEYS:
        resp, sent = cfg_send("config.get_key", key=key)
        snapshot[key] = field(resp, "value")
        check_config(f"get_key {key}",
                     config_ok(resp, "config.get_key", sent)
                     and field(resp, "key") == key
                     and key_value_ok(snapshot[key], low, high, count, unmapped),
                     detail=brief(resp))

    # The value field itself, held on its own: the declaration is bound to the
    # code that writes the reply by the flag the generator derives for a command
    # with no table (THETAGP_RESP_NO_TABLE_CONFIG_GET_KEY, asserted in
    # config_cmd_handler.cpp), while what the field carries — the key read back
    # as one number or as an array of numbers — is prose in protocol.toml that
    # no flag reads. Both spellings are read here, because a reply that dropped
    # the key entirely would otherwise be scored only by the checks above, whose
    # subject is the value each key holds.
    scalar_resp, _sent = cfg_send("config.get_key", key="map.socd_mode")
    array_resp, _sent = cfg_send("config.get_key", key="map.btn_map")
    array_value = field(array_resp, "value")
    check_config("get_key value present",
                 status_of(scalar_resp) == "ok"
                 and int_value(field(scalar_resp, "value"))
                 and field(scalar_resp, "key") == "map.socd_mode"
                 and status_of(array_resp) == "ok"
                 and isinstance(array_value, list) and array_value
                 and all(int_value(element) for element in array_value),
                 detail="scalar -> %s ; array -> %s"
                        % (brief(scalar_resp), brief(array_resp)))

    # The negative cases the ADR's D2 states, plus the same refusal read through
    # the other command: a request naming a key the table does not carry is
    # refused by both, and every refusal has to carry the code the protocol
    # names for an invalid parameter rather than answering ok with no effect —
    # a set_key that answers ok and changes nothing is worse than one that
    # refuses, which is the ADR's decision 9 in the other direction.
    err_param = shape["codes"]["ERR_INVALID_PARAM"]
    err_state = shape["codes"]["ERR_INVALID_STATE"]
    negatives = (
        ("set_key unknown key",
         ("config.set_key", {"key": "led.bri", "value": 50})),
        ("set_key socd_mode out of range",
         ("config.set_key", {"key": "map.socd_mode", "value": 9})),
        ("set_key btn_map wrong element count",
         ("config.set_key", {"key": "map.btn_map", "value": [1, 2]})),
        ("set_key btn_map element outside the domain",
         ("config.set_key", {"key": "map.btn_map",
                             "value": [32] * CONFIG_BTN_SLOTS})),
        ("get_key unknown key",
         ("config.get_key", {"key": "no.such.key"})),
    )
    for name, (cmd, params) in negatives:
        resp, _sent = cfg_send(cmd, **params)
        check_config(f"D2 {name} rejected", config_err(resp, err_param),
                     detail=brief(resp))

    # A refusal has to leave the field it named as it was. map.btn_map is
    # counted and checked element by element before the first byte is written
    # (config_cmd_handler.cpp:200-222), so the two rejected writes above leave
    # the value the stage read; a write that refused after writing would pass a
    # check that only looked at the reply.
    resp, sent = cfg_send("config.get_key", key="map.btn_map")
    check_config("D2 rejected writes left btn_map alone",
                 config_ok(resp, "config.get_key", sent)
                 and field(resp, "value") == snapshot["map.btn_map"],
                 detail=brief(resp))

    # config.set_key and config.get_key, one key at a time, each write read back
    # and then undone. Every request waits for its reply before the next goes
    # out: the frame layer holds one TX slot, so a request written while another
    # reply is in flight costs that reply (frame_layer.cpp:197-213, and the last
    # check of this stage measures it).
    #
    # What a round trip shows, and what it does not: both ends of a key go
    # through the one offset the table gives it, so a value written and read
    # back equal shows the two ends agree. It does not by itself show that the
    # value reached the field the key names — a single offset added twice, once
    # by the caller and once by the element accessor, is a difference this cannot
    # see. That is why the two checks that can see it are stated apart from this
    # one: the btn_map write below, whose offset is 0, and the table a factory
    # reset leaves, which is compared against the table this board compiles in.
    for key, low, high, _reboot, count, _unmapped in CONFIG_KEYS:
        if count != 1:
            continue
        found = snapshot[key]
        if not (int_value(found) and low <= found <= high):
            # A value the key's own write path would refuse cannot be written
            # back, so a probe written to this key would leave the byte it
            # reaches holding something else. The read half is already checked
            # above; the write half is skipped rather than risked.
            ctx.skip(f"set_key/get_key round trip {key}",
                     "the value read back (%s) is outside the range the key "
                     "declares (%d..%d), so a probe written to the byte this "
                     "key reaches could not be written back to what is there"
                     % (brief(found), low, high))
            continue

        # A probe inside the range and not the value the key holds, so the read
        # after the write cannot pass by standing still.
        probe = low if found != low else high
        resp, sent = cfg_send("config.set_key", key=key, value=probe)
        write_ok = (config_ok(resp, "config.set_key", sent)
                    and field(resp, "key") == key)
        resp, sent = cfg_send("config.get_key", key=key)
        read_ok = (config_ok(resp, "config.get_key", sent)
                   and field(resp, "key") == key
                   and field(resp, "value") == probe)
        check_config(f"set_key/get_key round trip {key}", write_ok and read_ok,
                     detail=brief(resp))

        resp, sent = cfg_send("config.set_key", key=key, value=found)
        write_back = config_ok(resp, "config.set_key", sent)
        resp, sent = cfg_send("config.get_key", key=key)
        check_config(f"set_key restores {key}",
                     write_back and config_ok(resp, "config.get_key", sent)
                     and field(resp, "value") == found,
                     detail=brief(resp))

    # map.btn_map — the array key, and the one key whose offset is 0, so a round
    # trip on it is a statement about the whole field rather than about two ends
    # agreeing. The value written carries button bit indexes in its first slots
    # and the unmapped sentinel in the rest, which is the domain the key accepts.
    sentinel = list(CONFIG_BTN_SENTINEL)
    resp, sent = cfg_send("config.set_key", key="map.btn_map", value=sentinel)
    btn_write = (config_ok(resp, "config.set_key", sent)
                 and field(resp, "key") == "map.btn_map")
    resp, sent = cfg_send("config.get_key", key="map.btn_map")
    btn_read = config_ok(resp, "config.get_key", sent)
    btn_value = field(resp, "value")
    check_config("btn_map round trip",
                 btn_write and btn_read and btn_value == sentinel,
                 detail=brief(resp))

    # The sentinel the key table admits beside its range (T-36): map.btn_map
    # reports max 31, and a slot may also hold 255, the value standing for a
    # slot the board maps to no button. A table whose max was taken for the
    # whole writable domain would refuse the value the read path returns for
    # such a slot — a map holding unmapped slots could not be written back.
    check_config("btn_map accepts the unmapped sentinel 255",
                 btn_write and btn_read
                 and isinstance(btn_value, list)
                 and CONFIG_BTN_UNMAPPED in btn_value
                 and int_value(btn_max)
                 and CONFIG_BTN_UNMAPPED > btn_max,
                 detail="a slot of %d accepted though the key declares max %s "
                        "(T-41): %s"
                        % (CONFIG_BTN_UNMAPPED, btn_max, brief(resp)))

    resp, sent = cfg_send("config.set_key", key="map.btn_map",
                          value=snapshot["map.btn_map"])
    write_back = config_ok(resp, "config.set_key", sent)
    resp, sent = cfg_send("config.get_key", key="map.btn_map")
    check_config("set_key restores map.btn_map",
                 write_back and config_ok(resp, "config.get_key", sent)
                 and field(resp, "value") == snapshot["map.btn_map"],
                 detail=brief(resp))

    def btn_vector_not(*values):
        """A 32-slot vector that is none of the values named.

        Every write below has to be a change: a check that read back what was
        already there could not tell a round trip from a suite that never wrote.
        """
        for candidate in (tuple(CONFIG_BTN_SENTINEL), CONFIG_DEFAULT_BTN_MAP,
                          tuple(range(CONFIG_BTN_SLOTS))):
            if candidate not in values:
                return candidate
        return None

    # config.load — reads the profile the configuration in effect belongs to
    # back over it. The write before it is deliberately not saved, so the value
    # the load leaves has to be the one the stage found and not the one it just
    # wrote: a load that answered ok without reading the profile would leave the
    # probe in place and this check would see it. The profile still holds what
    # the board booted with — this stage undoes every write it makes and has
    # saved nothing yet.
    load_probe = list(btn_vector_not(tuple(snapshot["map.btn_map"])))
    resp, sent = cfg_send("config.set_key", key="map.btn_map", value=load_probe)
    written = config_ok(resp, "config.set_key", sent)
    resp, sent = cfg_send("config.load")
    loaded = config_ok(resp, "config.load", sent)
    resp, sent = cfg_send("config.get_key", key="map.btn_map")
    check_config("config.load discards an unsaved write",
                 written and loaded and config_ok(resp, "config.get_key", sent)
                 and field(resp, "value") == snapshot["map.btn_map"],
                 detail=brief(resp))

    # config.save — writes the configuration in effect to the profile it belongs
    # to, with a precondition: the factory profile is the board's baseline and
    # the configuration layer refuses to write it (ERR_INVALID_STATE, "the
    # active profile is the factory one"). Which branch a run meets is the
    # board's state, so the stage asks the board for it (profile.status, the
    # one read-only command that reports the active id) and holds the reply to
    # what that answer allows. A check that demanded persisted true regardless
    # would be reading the board's state as the device's failure; one that
    # accepted either answer would not be checking the precondition at all.
    active_reply = ctx.send("profile.status")
    active_id = field_int(active_reply, "active_profile_id")
    resp, sent = cfg_send("config.save")
    saved_ok = config_ok(resp, "config.save", sent) and field(resp, "persisted") is True
    if active_id is None:
        # No profile domain to ask: a board without storage keeps the
        # configuration in RAM and says so with persisted false rather than
        # refusing, and this is the one branch of the three the board cannot be
        # asked to choose between.
        check_config("config.save without storage reports persisted false",
                     config_ok(resp, "config.save", sent)
                     and field(resp, "persisted") is False,
                     detail=brief(resp))
    elif active_id == 0:
        check_config("config.save refused while the factory profile is active",
                     config_err(resp, err_state)
                     and "factory" in str(field(resp, "reason")),
                     detail="active_profile_id=0 -> %s" % brief(resp))
    else:
        check_config(f"config.save persists on profile {active_id}",
                     saved_ok, detail="active_profile_id=%d -> %s"
                                      % (active_id, brief(resp)))

        # What persisted true claims is that the write reached the profile, and
        # the only way to say so from the wire is to read it back through the
        # profile: write a value, save it, drop the in-RAM copy with a load, and
        # read the value again. A save that answered persisted true without
        # writing would leave the earlier value here.
        flush = btn_vector_not(tuple(snapshot["map.btn_map"]))
        resp, sent = cfg_send("config.set_key", key="map.btn_map",
                              value=list(flush))
        wrote = config_ok(resp, "config.set_key", sent)
        resp, sent = cfg_send("config.save")
        saved = (config_ok(resp, "config.save", sent)
                 and field(resp, "persisted") is True)
        resp, sent = cfg_send("config.load")
        reloaded = config_ok(resp, "config.load", sent)
        resp, sent = cfg_send("config.get_key", key="map.btn_map")
        check_config("config.save reached the profile",
                     wrote and saved and reloaded
                     and config_ok(resp, "config.get_key", sent)
                     and field(resp, "value") == list(flush),
                     detail=brief(resp))

        # And the profile is put back where it was, through the same two
        # commands, so a re-run of the suite starts from the same profile.
        resp, sent = cfg_send("config.set_key", key="map.btn_map",
                              value=snapshot["map.btn_map"])
        restored = config_ok(resp, "config.set_key", sent)
        resp, sent = cfg_send("config.save")
        saved_back = (config_ok(resp, "config.save", sent)
                      and field(resp, "persisted") is True)
        resp, sent = cfg_send("config.load")
        reloaded_back = config_ok(resp, "config.load", sent)
        resp, sent = cfg_send("config.get_key", key="map.btn_map")
        check_config("config.save restored the profile content",
                     restored and saved_back and reloaded_back
                     and config_ok(resp, "config.get_key", sent)
                     and field(resp, "value") == snapshot["map.btn_map"],
                     detail=brief(resp))

        # What the reply says the save left behind, held to the two bodies the
        # save had in hand rather than to the reply's own word: the body it
        # replaces (profile.get on the active id, the same body its own read
        # returns) and the body it writes in its place. The count is a statement
        # about that pair and nothing else (config_manager.h), so this is the one
        # judgement about it that does not rest on the device's arithmetic: the
        # expected number is derived here, from the bytes on the wire, and the
        # reply has to agree with it — absent when the two bodies carry the same
        # keys, equal when they do not.
        #
        # The save below is the stage's own undo made again: the in-effect
        # configuration is the snapshot the checks above restored, so the body
        # written in place of the one read is the body that was there, and this
        # check leaves nothing new to undo.
        replaced = save_bodies(ctx, active_id)
        resp, sent = cfg_send("config.save")
        reported = config_ok(resp, "config.save", sent)
        written = save_bodies(ctx, active_id)
        if replaced is None or written is None:
            ctx.skip("config.save reports exactly what it did not carry over",
                     "profile.get answered no body for profile %d, so the pair "
                     "the count is about is not readable here: replaced=%s, "
                     "written=%s" % (active_id, brief(replaced), brief(written)))
        else:
            expected = not_carried_over(replaced, written)
            if expected is None:
                ctx.skip("config.save reports exactly what it did not carry over",
                         "a key of the two bodies cannot be named the way the "
                         "count names keys (a name carrying '.', '[' or ']', a "
                         "chain deeper than %d names, a path longer than %d "
                         "bytes), which is the pair the firmware answers 0 for"
                         % (KEY_CHAIN_MAX, LOOKUP_PATH_MAX))
            else:
                check_config("config.save reports exactly what it did not "
                             "carry over",
                             reported and drop_report_ok(resp, expected,
                                                         dropped_key),
                             detail="the replaced body has %d key(s) the written "
                                    "body does not, so the reply %s; it read: %s"
                                    % (expected,
                                       "has no %s to report" % dropped_key
                                       if expected == 0 else
                                       "has to report %s=%d" % (dropped_key,
                                                                expected),
                                       brief(resp)))

    # config.factory_reset — replaces the configuration in effect with the
    # compiled defaults and writes nothing out: the reply says so with persisted
    # false, and no flash is touched, so undo is a load away. What this can hold
    # the command to on the wire is the reply's shape and that flag, and the
    # table it leaves: the compiled table is this board's key table converted to
    # button bit indexes at compile time, so a reset that left the table as it
    # found it, or wrote a table no board compiles from, reads differently. The
    # value written first is not that table, so the check after the reset says
    # the reset replaced it.
    before = btn_vector_not(tuple(snapshot["map.btn_map"]),
                            CONFIG_DEFAULT_BTN_MAP)
    if before is None:
        ctx.skip("factory_reset leaves the compiled default table",
                 "every table this stage could write is the compiled default on "
                 "this board, so the check could not tell the reset from a "
                 "command that left the table alone")
        ctx.skip("factory_reset: the table is not the default before the reset",
                 "same reason")
    else:
        resp, sent = cfg_send("config.set_key", key="map.btn_map",
                              value=list(before))
        staged = config_ok(resp, "config.set_key", sent)
        resp, sent = cfg_send("config.get_key", key="map.btn_map")
        check_config("factory_reset: the table is not the default before it",
                     staged and config_ok(resp, "config.get_key", sent)
                     and field(resp, "value") == list(before),
                     detail=brief(resp))

        resp, sent = cfg_send("config.factory_reset")
        check_config("factory_reset reply",
                     config_ok(resp, "config.factory_reset", sent)
                     and field(resp, "persisted") is False,
                     detail="the reset replaces the in-effect configuration and "
                            "does not save it: %s" % brief(resp))

        resp, sent = cfg_send("config.get_key", key="map.btn_map")
        check_config("factory_reset leaves the compiled default table",
                     config_ok(resp, "config.get_key", sent)
                     and field(resp, "value") == list(CONFIG_DEFAULT_BTN_MAP),
                     detail="expected %s, read %s"
                            % (list(CONFIG_DEFAULT_BTN_MAP), brief(resp)))

        # The reset is undone through the profile, which is what the load is
        # for: the profile holds what the board booted with, so reading it back
        # puts every field where it was. The keys the stage reads are checked
        # once more below, together with the rest of the left-as-found checks.
        resp, sent = cfg_send("config.load")
        check_config("factory_reset undone by config.load",
                     config_ok(resp, "config.load", sent),
                     detail=brief(resp))

    # The limit the checks above work around, stated as a check of its own: the
    # frame layer holds one TX slot (frame_layer.cpp:197-213) while the task that
    # dispatches commands drains a whole tick's queue at once, so requests
    # written back to back leave one reply on the wire — the last one, the
    # others overwritten before the host can read them. A stage whose requests
    # were pipelined would read a reply belonging to another request; this goes
    # red the day the pipeline grows a second slot, which is when that has to be
    # revisited rather than depended on.
    if config_present:
        first = ctx.queued
        burst = b"".join(
            json.dumps({"cmd": "sys.ping", "queued": first + i},
                       separators=(",", ":")).encode() + b"\r\n"
            for i in range(3))
        print(f">>> 3 x sys.ping in one write (queued {first}..{first + 2})")
        os.write(ctx.fd, burst)
        ctx.queued += 3
        replies = []
        while True:
            line = readline(ctx.fd, timeout=1.0)
            if line is None:
                break
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if obj.get("status") == "async":
                continue
            replies.append(obj)
        ok("one reply per request (single TX slot)",
           len(replies) == 1
           and field_int(replies[0], "queued") == first + 3,
           detail="%d repl%s for 3 requests: %s"
                  % (len(replies), "y" if len(replies) == 1 else "ies",
                     brief(replies[0] if replies else None)))

    # The stage's writes are undone one by one above; this is the check that
    # says so, and it is what a re-run of the suite reads instead of the
    # leftovers of the run before it.
    for key, _low, _high, _reboot, _count, _unmapped in CONFIG_KEYS:
        resp, sent = cfg_send("config.get_key", key=key)
        check_config(f"config stage left {key} as found",
                     config_ok(resp, "config.get_key", sent)
                     and field(resp, "value") == snapshot[key],
                     detail="found %s, now %s"
                            % (brief(snapshot[key]), brief(resp)))

    # ── Summary ────────────────────────────────────────────────────────
    ok_ = ctx.summary()
    os.close(fd)
    return 0 if ok_ else 1


if __name__ == "__main__":
    exit(main())
