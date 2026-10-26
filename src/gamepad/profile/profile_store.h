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

#pragma once

#include "build_info.h"

#include <cstdint>

namespace ThetaGP::Gamepad::Profile {

// ── Constants ──
// Layout: sector 0 holds the BootMeta ring (128 x 16 B) and the address ring
// (256 x 8 B), sector 1 the primary Profile 0 and sector 2 its backup, sectors
// 3 and up the user ring.

// The largest body a profile carries.
static constexpr uint32_t PROFILE_JSON_MAX = 4096;

// The staging buffer: one byte more than the largest body, so a body of exactly
// PROFILE_JSON_MAX bytes still has a NUL after it.
static constexpr uint32_t PROFILE_STAGING_SIZE = PROFILE_JSON_MAX + 1;

// The highest profile id.
static constexpr uint16_t PROFILE_MAX_ID = 15;
// The sentinel an empty address entry carries.
static constexpr uint16_t PROFILE_ID_NONE = 0xFFFF;
// The id readProfile() takes for the active profile.
static constexpr uint16_t PROFILE_ID_ACTIVE = PROFILE_ID_NONE;

// readProfile() tests the sentinels before the id range, so neither may land
// inside it.
static_assert(PROFILE_ID_NONE > PROFILE_MAX_ID,
              "PROFILE_ID_NONE must stay outside the valid id range");
static_assert(PROFILE_ID_ACTIVE > PROFILE_MAX_ID,
              "PROFILE_ID_ACTIVE must stay outside the valid id range");

// Body lengths travel as uint16_t.
static_assert(PROFILE_STAGING_SIZE <= 0xFFFF,
              "PROFILE_STAGING_SIZE must fit as a uint16_t body length");

// The erase sector of the external SPI flash. The layout below and the sector
// accounting in getStatus() both count in these units.
static constexpr uint32_t PROFILE_SECTOR_SIZE = 0x1000;

// The reserved region ahead of the user ring, a sector apart: the BootMeta ring,
// the address ring, Profile 0's two copies, then the user profiles.
static constexpr uint32_t BOOTMETA_BASE = 0x000000;
static constexpr uint32_t BOOTMETA_SIZE = 0x000800;
static constexpr uint32_t ADDR_RING_BASE = 0x000800;
static constexpr uint32_t ADDR_RING_SIZE = 0x000800;
static constexpr uint32_t PROFILE0_ADDR = 0x001000;   // Profile 0, primary
static constexpr uint32_t PROFILE0_BACKUP = 0x002000; // Profile 0, backup
static constexpr uint32_t USER_RING_BASE = 0x003000;

// The sectors the reserved region ahead of the user ring takes.
static constexpr uint32_t RESERVED_SECTORS =
    USER_RING_BASE / PROFILE_SECTOR_SIZE;
static_assert(USER_RING_BASE % PROFILE_SECTOR_SIZE == 0,
              "the User Ring must start on a sector boundary, or the reserved "
              "sector count would round down");

static constexpr uint16_t BOOTMETA_MAGIC = 0x5442; // "TB"

static constexpr uint16_t BOOTMETA_SLOTS = 128;   // entries in the BootMeta ring
static constexpr uint16_t ADDR_RING_SLOTS = 256;  // entries in the address ring

// ── Structs ──

#pragma pack(push, 1)
struct BootMeta {
  uint16_t magic;     // 0x5442 ("TB")
  uint16_t seq;       // monotonic, highest = active
  uint16_t profileId; // 0=Profile0, 1~15=user
  uint32_t address;   // User Ring physical address
  uint32_t reserved;  // 0xFF
  uint16_t crc16;     // CRC16(first 14 bytes)
};
#pragma pack(pop)

static_assert(sizeof(BootMeta) == 16, "BootMeta must be 16 bytes");

#pragma pack(push, 1)
struct AddressEntry {
  uint16_t profileId; // 0~15, 0xFFFF = empty
  uint32_t address;   // User Ring physical address
  uint16_t seq;       // monotonic, highest = latest for this profileId
};
#pragma pack(pop)

static_assert(sizeof(AddressEntry) == 8, "AddressEntry must be 8 bytes");

// A read-only view of a profile body. `data` is NUL-terminated and points into
// the staging buffer, so the next read or write of a profile invalidates it.
struct ProfileText {
  const char *data = nullptr;
  uint16_t len = 0;
};

// Runtime status of the profile system.
struct ProfileStatus {
  uint16_t activeId = 0;        // the profile in effect
  uint8_t profileCount = 0;     // valid profiles, empty entries left out
  uint32_t totalSectors = 0;    // flash sectors
  uint32_t usedSectors = 0;     // sectors profile data occupies
  uint32_t freeSectors = 0;     // sectors left in the user ring
  uint32_t reservedSectors = 0; // sectors ahead of the user ring; the three add up
  uint32_t nextAddr = 0;        // the user ring's next write pointer
  uint16_t bootMetaSeq = 0;     // the BootMeta ring's sequence number
  uint16_t addressRingSeq = 0;  // the address ring's sequence number
};

// ── ProfileStore class ──

// Profile storage on the external SPI flash. Both rings append, and the highest
// sequence number is the entry in effect, so a write cut short by a reset leaves
// the entry before it standing.
class ProfileStore {
public:
  ProfileStore() = default;
  ProfileStore(const ProfileStore &) = delete;
  ProfileStore &operator=(const ProfileStore &) = delete;

  static ProfileStore &getInstance();

  // Scans both rings for the active profile and the next write address. Writes
  // nothing.
  bool init();

  // Whether the flash carries no valid BootMeta and no Profile 0 body: the
  // answer is read now, from the caches init() filled plus the Profile 0 area, and
  // never remembered, because an erase changes it without a reboot.
  bool needsFactoryProfile() const;

  // Writes the factory Profile 0, only where the BootMeta ring is empty.
  bool writeFactoryProfile(const char *json, uint16_t len);
  // Writes the factory Profile 0 over the one the flash holds: the body sits at a
  // fixed address of its own, so a replace is the same write as the first one.
  bool replaceFactoryProfile(const char *json, uint16_t len);

  // Creates a user profile at the first free id, appended to the user ring.
  bool createProfile(const char *json, uint16_t len, uint16_t *newId);

  // Appends a new version of an existing profile to the user ring.
  bool modifyProfile(uint16_t id, const char *json, uint16_t len);

  // Marks a profile deleted with an address-ring entry holding address 0.
  bool deleteProfile(uint16_t id);

  // Selects a profile as active by appending a BootMeta entry.
  bool selectProfile(uint16_t id);

  // Reserved for a later entry into the store: not implemented, so it reads
  // nothing and answers that it did not.
  bool loadActive(uint8_t *buf, uint16_t *outLen);

  // The body of profile `id`, as a view into the staging buffer;
  // PROFILE_ID_ACTIVE reads the active one. Answers false when there is none.
  bool readProfile(uint16_t id, ProfileText *out);

  // Whether the store holds an id: the factory address for 0, the active
  // profile's address for PROFILE_ID_ACTIVE, and the address ring's entry for
  // every other id. Answered from the address map the last scan filled, without a
  // read of the flash.
  bool carriesProfile(uint16_t id) const;

  // Counts the sectors, the profiles and the sequence numbers as of the last scan.
  ProfileStatus getStatus() const;

  // Moves the valid profiles to the head of the user ring and resets the rings.
  bool compaction();

private:
  // ── Internal helpers ──
  bool resetSector0();
  // Reads the body at `address` into buf (nullptr for the staging buffer) and
  // reports its length in outLen. No terminator is written: the length stops at
  // the first 0x00 or 0xFF.
  bool readBody(uint32_t address, uint8_t *buf, uint16_t *outLen);
  bool writeBootMeta(uint16_t id, uint32_t address);
  uint16_t crc16BootMeta(const BootMeta *meta) const;
  void seqBeforeIncrement();
  bool scanBootMeta(uint16_t *outActiveId, uint32_t *outAddress);
  bool scanAddressRing();
  uint32_t findNextAddr() const;
  bool eraseSector0Range(uint32_t addr, uint32_t len);
  uint16_t ensureBootMetaSlot();
  uint16_t ensureAddressRingSlot();

  // ── Cached state, as of the last scan ──
  // init() refreshes every field here, a factory write the ones it invalidates.
  uint16_t _activeProfileId = 0;
  uint32_t _activeAddress = PROFILE0_ADDR;
  uint32_t _nextAddr = USER_RING_BASE;
  uint16_t _bootMetaSeq = 0;
  uint16_t _addressRingSeq = 0;
  uint8_t _profileCount = 0;

  // Cached address map: index = profileId, value = {address, seq}
  uint32_t _profileAddresses[16] = {0};
  uint16_t _profileSeqs[16] = {0};
};

// The buffer bodies are read into and written out of: a body of PROFILE_JSON_MAX
// bytes plus the NUL the scanners read it by.
COMMON_ZERO_INIT extern uint8_t s_staging[PROFILE_STAGING_SIZE];

} // namespace ThetaGP::Gamepad::Profile
