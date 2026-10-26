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

#include "gamepad/config/config_store.h"
#include "configs/config_keys.gen.h"
#include "gamepad/config/key_table.h"
#include "utils/log/log.h"

// PROFILE_JSON_MAX: the body length the flash layer accepts. The serializer has
// to refuse anything longer here rather than at the write, so that a body can
// never be truncated into a well-formed-looking prefix.
#include "gamepad/profile/profile_store.h"

// Compiles the defaults into this translation unit, which is what runs its
// static_asserts: the defaults header holds the compile-time checks on the
// board key table, and a header no translation unit includes is never checked.
#include "gamepad/config/config_defaults.h"
#include "pb/enums.pb.h"
#include "utils/json/json.h"

#include <cstdio>
#include <cstring>

namespace ThetaGP::Gamepad::Config {

// An unmapped btn_map slot is 0xFF on the wire, and kBtnMapUnmapped is the byte
// config_defaults.h gives a key the board leaves out. The two spellings live in
// different files, so this holds them together.
static_assert(detail::kBtnMapUnmapped == 0xFF,
              "kBtnMapUnmapped: the unmapped btn_map slot is 0xFF on the wire");

// ── Profile versions ──
// Version 1 kept every domain object inside `map`; from version 2 a field sits
// inside the object its name starts with. Version 3 is the first whose `led.mode`
// the strip honours, so a body below 3 reads that field as the rainbow.
//
// A body of any other version is not read at all.
// The value a field reads as when the body carries no such field. No row of the
// key table accepts it as a value, which that table holds at compile time.
constexpr int32_t kNoProfileValue = INT32_MIN;

// The version that first carries an LED effect the strip honours.
constexpr int kLedEffectVersion = 3;

// ── Paths ──

// True when the entry names a field of the map domain, the one object a body of
// version 1 already kept its own fields in.
static bool inMapDomain(const KeyEntry &entry) {
  return std::strncmp(entry.key, "map.", 4) == 0;
}

// The path a body of `version` carries the entry's field under. The field's
// name is the path from version 2 on; a body of version 1 wrapped every other
// domain inside map, so those fields take the "map." prefix.
static void profilePath(const KeyEntry &entry, int version, char *dst,
                        size_t cap) {
  if (version > kOldestReadableVersion || inMapDomain(entry)) {
    std::snprintf(dst, cap, "%s", entry.key);
    return;
  }
  std::snprintf(dst, cap, "map.%s", entry.key);
}

// The object a field is carried in: the head of its name, up to the last dot.
// Every name in the key table has the shape "<domain>.<leaf>", so the object is
// what sits before the dot.
static void profileDomain(const KeyEntry &entry, char *dst, size_t cap) {
  const char *leaf = profileLeafName(entry);
  const size_t len =
      leaf > entry.key ? static_cast<size_t>(leaf - entry.key) - 1 : 0;
  const size_t copy = len < cap - 1 ? len : cap - 1;
  std::memcpy(dst, entry.key, copy);
  dst[copy] = '\0';
}

// How many segments a domain has: `led` names one object, `led.rgb` names one
// inside it.
static size_t domainDepth(const char *domain) {
  size_t depth = domain[0] != '\0' ? 1 : 0;
  for (const char *at = domain; *at != '\0'; ++at) {
    if (*at == '.') {
      ++depth;
    }
  }
  return depth;
}

// The segments from the front that two domains share, counted in whole
// segments: `led` and `led.rgb` share one, `led` and `ledx` none.
static size_t sharedSegments(const char *a, const char *b) {
  size_t at = 0;
  while (a[at] != '\0' && b[at] != '\0' && a[at] == b[at]) {
    ++at;
  }
  if (at == 0 || (a[at] != '\0' && a[at] != '.')) {
    return 0;
  }
  size_t shared = 1;
  for (size_t i = 0; i < at; ++i) {
    if (a[i] == '.') {
      ++shared;
    }
  }
  return shared;
}

// The segment of a domain at `index`, copied into dst.
static void domainSegment(const char *domain, size_t index, char *dst,
                          size_t cap) {
  const char *start = domain;
  for (size_t seen = 0; seen < index; ++seen) {
    start = std::strchr(start, '.');
    if (start == nullptr) {
      dst[0] = '\0';
      return;
    }
    ++start;
  }
  const char *end = std::strchr(start, '.');
  const size_t len =
      end != nullptr ? static_cast<size_t>(end - start) : std::strlen(start);
  const size_t copy = len < cap - 1 ? len : cap - 1;
  std::memcpy(dst, start, copy);
  dst[copy] = '\0';
}

// ── parseProfile() ──
// Reads a profile body into the store and reports whether it was read. A refused
// body leaves the store as it was.

bool parseProfile(const char *json, uint32_t len, ConfigStore *cfg) {
  if (!json || !cfg) {
    LOG_ERROR("parseProfile: null args");
    return false;
  }

  // The body's own length is the parse window: no terminator is looked for past
  // its last byte.
  Util::Json doc;
  doc.parse(json, static_cast<int>(len));

  // The version decides how every field's path is spelled, so it is read before
  // the first field is. A body that carries no version predates the field and is
  // the oldest version this firmware reads.
  const int version = doc.getInt("ver", kOldestReadableVersion);
  if (version < kOldestReadableVersion || version > kProfileVersion) {
    LOG_ERROR("parseProfile: body version %d is not one this firmware reads "
              "(%d..%d) — the configuration is left as it stands",
              version, kOldestReadableVersion, kProfileVersion);
    return false;
  }

  const KeyEntry *const table = keyTable();
  const uint8_t count = keyTableCount();

  // Every field is read through its row, which carries the name the body writes
  // it under and the bytes it occupies. A number outside the row's range leaves
  // the field at its compiled default rather than being narrowed into it.
  for (uint8_t i = 0; i < count; ++i) {
    const KeyEntry &entry = table[i];
    char path[48];
    profilePath(entry, version, path, sizeof(path));

    if (entry.type == ThetaGP_ConfigKeyType_CONFIG_KEY_U8_ARRAY) {
      // A body that carries the array fills every element; the ones it does not
      // reach take the unmapped sentinel, so no stale tail is left behind.
      const int elems = doc.getArrLen(path);
      if (elems <= 0) {
        continue;
      }
      for (uint8_t index = 0; index < entry.count; ++index) {
        const int32_t value = index < elems ? doc.getArrInt(path, index, -1) : -1;
        storeKeyElement(*cfg, entry, index,
                        keyElementAccepted(entry, value)
                            ? value
                            : static_cast<int32_t>(detail::kBtnMapUnmapped));
      }
      continue;
    }

    const int32_t value = doc.getInt(path, static_cast<int>(kNoProfileValue));
    if (value == kNoProfileValue) {
      continue; // the body carries no such field
    }
    // A body written before the strip honoured its effect carries a mode
    // nothing acted on: the strip showed the rainbow then, so that is what the
    // field reads as, whatever number the body wrote there.
    if (version < kLedEffectVersion && std::strcmp(entry.key, "led.mode") == 0) {
      storeKeyElement(*cfg, entry, 0,
                      static_cast<int32_t>(Enums::LEDMode::Rainbow));
      continue;
    }
    storeKeyElement(*cfg, entry, 0,
                    keyElementAccepted(entry, value)
                        ? value
                        : loadKeyElement(kConfigDefaults, entry, 0));
  }

  LOG_DEBUG("parseProfile: done, version %d", version);
  return true;
}

// ── serializeProfile() ──
// Writes the store as a body of the current version, walking the key table: an
// object opens at the first row that names it and closes at a row of another,
// so the rows of one object are held adjacent in that table.

uint16_t serializeProfile(const ConfigStore &cfg, const char *name, char *dst,
                          uint16_t cap) {
  if (!dst || cap == 0) {
    return 0;
  }

  Util::Json doc;
  doc.beginWrite(dst, cap);
  doc.printf("{%Q:%d", "ver", kProfileVersion);
  // The profile's name travels with its body: a save keeps the one the body it
  // replaces carried, and a host renames a profile by writing a body back.
  if (name != nullptr && name[0] != '\0') {
    doc.printf(",%Q:%Q", "name", name);
  }

  const KeyEntry *const table = keyTable();
  const uint8_t count = keyTableCount();

  // The objects being written, by the domain of the fields before this one: a
  // field of `led` and one of `led.rgb` share the `led` object.
  constexpr size_t kMaxDomain = 24;
  char previous[kMaxDomain] = "";
  size_t open = 0;
  bool firstInObject = true;

  for (uint8_t i = 0; i < count; ++i) {
    const KeyEntry &entry = table[i];
    char domain[kMaxDomain];
    profileDomain(entry, domain, sizeof(domain));

    const size_t shared = sharedSegments(previous, domain);
    const size_t depth = domainDepth(domain);

    for (size_t segment = open; segment > shared; --segment) {
      doc.printf("}");
    }
    for (size_t segment = shared; segment < depth; ++segment) {
      char name[kMaxDomain];
      domainSegment(domain, segment, name, sizeof(name));
      doc.printf(",%Q:{", name);
      firstInObject = true;
    }
    std::snprintf(previous, sizeof(previous), "%s", domain);
    open = depth;

    if (!firstInObject) {
      doc.printf(",");
    }
    firstInObject = false;

    const char *leaf = profileLeafName(entry);
    if (entry.type == ThetaGP_ConfigKeyType_CONFIG_KEY_U8_ARRAY) {
      doc.printf("%Q:[", leaf);
      for (uint8_t index = 0; index < entry.count; ++index) {
        if (index > 0) {
          doc.printf(",");
        }
        doc.printf("%ld", static_cast<long>(loadKeyElement(cfg, entry, index)));
      }
      doc.printf("]");
    } else {
      doc.printf("%Q:%ld", leaf,
                 static_cast<long>(loadKeyElement(cfg, entry, 0)));
    }
  }

  for (size_t segment = open; segment > 0; --segment) {
    doc.printf("}");
  }
  doc.printf("}"); // close root

  const uint16_t len = static_cast<uint16_t>(doc.end());

  // 0 means no body: the text was cut at the end of dst, or the body is longer
  // than the flash layer stores. dst is blanked so it cannot be read as one.
  if (doc.overflowed() || len == 0 || len > Profile::PROFILE_JSON_MAX) {
    LOG_ERROR("serializeProfile: no usable body (cap=%u, len=%u%s)", cap, len,
              doc.overflowed() ? ", truncated" : "");
    dst[0] = '\0';
    return 0;
  }

  return len;
}

} // namespace ThetaGP::Gamepad::Config
