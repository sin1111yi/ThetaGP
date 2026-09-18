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
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

// Host harness for Json::missingKeyCount (src/utils/json/json.cpp), run by
// scripts/test/test_json_missing_key.py. It links the firmware's own json.cpp
// and lib/frozen/frozen.c unchanged and drives them on the host, so a case here
// says what the firmware's comparison does and not what a stand-in for it does.
//
// What the cases pin down: the walk carries the chain of names of the objects
// open at each key, and the name of the member holding an array joins that
// chain while the array is open. The key holding the array is named by
// ARRAY_START and its name closes at the array's end — an ARRAY_END that only
// counts the array down leaves that name on the chain, so every key behind the
// array is spelled as if it sat inside the array (`map.btn_map.<key>` instead
// of `<key>`) and the lookup answers Absent: the keys are counted as left
// behind, with the count rising with the number of keys behind the array.
//
// Hence the shape of the table: every case compares a body against itself,
// where the only right answer is 0, and the cases differ in what stands behind
// the array. The controls on either side keep a 0 from being read as a pass — a
// body with no array at all, and two pairs where keys really are missing and
// the count has to stay right.
//
// Run: python3 scripts/test/test_json_missing_key.py
//      (compiles this file with the two sources above into a temporary
//      directory and runs it; exit 0 = every case matched. The compiler flags
//      are the firmware's own language level, -std=gnu++20, so the sources are
//      built as the board builds them.)

#include "utils/json/json.h"

#include <cstdio>
#include <cstring>

namespace {

// The factory profile as serializeProfile (config_store.cpp:179-216) spells it:
// a `map` object whose btn_map array is the last member, then stick, trig, led
// and cal. The body is this side's own transcription of that shape — the
// board's key table writes other numbers into btn_map and its body is 447 bytes
// — but the shape is what the walk sees, and the count behind the array is the
// same 25 keys (stick 1+10, trig 1+2, led 1+5, cal 1+4) the reported
// measurement read off the device.
constexpr char kProfileBody[] =
    "{\"ver\":2,\"map\":{\"socd\":4,\"four_way\":0,\"dpad\":0,\"inv_x\":0,"
    "\"inv_y\":0,\"inv_rx\":0,\"inv_ry\":0,\"swap\":0,\"btn_map\":[0,1,2,3,4,5,"
    "6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,"
    "31]},\"stick\":{\"lx_dz\":512,\"ly_dz\":512,\"rx_dz\":512,\"ry_dz\":512,"
    "\"lx_sens\":128,\"ly_sens\":128,\"rx_sens\":128,\"ry_sens\":128,"
    "\"curve\":0,\"ema\":0},\"trig\":{\"lt_dz\":8,\"rt_dz\":8},"
    "\"led\":{\"bri\":128,\"mode\":0,\"hue\":180,\"sat\":255,\"spd\":128},"
    "\"cal\":{\"lx_c\":0,\"ly_c\":0,\"rx_c\":0,\"ry_c\":0}}";

// One row of the table: the body whose keys are asked about, the body they are
// looked up in, and how many of them that body does not carry.
struct Case {
  const char *name;
  const char *source;
  const char *carrier;
  uint32_t want;
};

const Case kCases[] = {
    // ── Controls: no array in the body, and keys that really are missing ──
    {"control: no array (objects only)", "{\"a\":{\"y\":1},\"z\":2}",
     "{\"a\":{\"y\":1},\"z\":2}", 0},
    {"control: a key the carrier lacks",
     "{\"a\":1,\"arr\":[1],\"b\":2,\"c\":9}", "{\"a\":1,\"arr\":[1],\"b\":2}",
     1},
    {"control: a key the carrier lacks, no array", "{\"a\":1,\"b\":2}",
     "{\"a\":1}", 1},

    // ── The array itself: behind it, in the middle, empty, nested ──
    {"array in the middle", "{\"a\":1,\"arr\":[1,2],\"b\":2}",
     "{\"a\":1,\"arr\":[1,2],\"b\":2}", 0},
    {"array is the last member", "{\"a\":1,\"b\":2,\"arr\":[1]}",
     "{\"a\":1,\"b\":2,\"arr\":[1]}", 0},
    {"empty array", "{\"a\":1,\"arr\":[],\"b\":2}",
     "{\"a\":1,\"arr\":[],\"b\":2}", 0},
    {"nested array", "{\"a\":1,\"arr\":[[1,2],[3]],\"b\":2}",
     "{\"a\":1,\"arr\":[[1,2],[3]],\"b\":2}", 0},
    {"nested array inside an object in an array",
     "{\"arr\":[{\"in\":[1,2]}],\"b\":2}", "{\"arr\":[{\"in\":[1,2]}],\"b\":2}",
     0},

    // ── What stands behind the array ──
    {"object behind the array", "{\"a\":1,\"arr\":[1,2],\"b\":{\"c\":3}}",
     "{\"a\":1,\"arr\":[1,2],\"b\":{\"c\":3}}", 0},
    {"object behind the array, two deep",
     "{\"arr\":[1],\"b\":{\"c\":{\"d\":4}}}",
     "{\"arr\":[1],\"b\":{\"c\":{\"d\":4}}}", 0},
    {"array of objects behind an array",
     "{\"arr\":[1],\"b\":[{\"x\":1}],\"d\":2}",
     "{\"arr\":[1],\"b\":[{\"x\":1}],\"d\":2}", 0},
    // The count is not zeroed by the fix: a key the carrier lacks behind an
    // array is still one key, and a name repeated behind an array is counted
    // once per occurrence, not once per name (json.h on missingKeyCount).
    {"a key the carrier lacks behind an array", "{\"arr\":[1],\"b\":2,\"c\":9}",
     "{\"arr\":[1],\"b\":2}", 1},
    {"a name repeated behind the array", "{\"arr\":[1],\"b\":1,\"b\":2}",
     "{\"arr\":[1],\"c\":1}", 2},

    // ── The shape the firmware writes: 25 keys behind btn_map ──
    {"profile-shaped body, self-compared", kProfileBody, kProfileBody, 0},
};

void run(const Case &c, int *failed) {
  Json source;
  source.parse(c.source);
  Json carrier;
  carrier.parse(c.carrier);
  const uint32_t got = carrier.missingKeyCount(source);
  const bool pass = got == c.want;
  if (!pass)
    (*failed)++;
  std::printf("  %-4s want %-2u got %-2u  %s\n", pass ? "ok" : "FAIL", c.want,
              got, c.name);
}

} // namespace

int main() {
  std::printf("Json::missingKeyCount — host cases (no board)\n");
  std::printf("  profile-shaped body: %zu bytes\n", std::strlen(kProfileBody));

  int failed = 0;
  for (const Case &c : kCases) {
    run(c, &failed);
  }

  std::printf("%zu cases, %d failed\n", sizeof(kCases) / sizeof(kCases[0]),
              failed);
  return failed == 0 ? 0 : 1;
}
