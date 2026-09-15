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

namespace ThetaGP::Drivers::Device {
class FlashW25qxx;
}

namespace ThetaGP::Gamepad::Profile {

// ── Constants ──
// Layout: Sector 0 = BootMeta Ring (128×16B=2KB) + Address Ring (256×8B=2KB)
//         Sector 1 = Profile0 primary
//         Sector 2 = Profile0 backup
//         Sectors 3..N = User Ring (linear append area)

static constexpr uint32_t PROFILE_JSON_MAX =
    4096; /**< Max JSON body size per profile */

// One byte more than the largest body: a body of exactly PROFILE_JSON_MAX bytes
// still gets a NUL after it, which the scanners that read it as a string need.
// Every write into the staging buffer stays below this size.
static constexpr uint32_t PROFILE_STAGING_SIZE = PROFILE_JSON_MAX + 1;

static constexpr uint16_t PROFILE_MAX_ID = 15; /**< Maximum profile ID (0-15) */
static constexpr uint16_t PROFILE_ID_NONE =
    0xFFFF; /**< Sentinel for empty AddressEntry */
static constexpr uint16_t PROFILE_ID_ACTIVE =
    PROFILE_ID_NONE; /**< Selects the active profile in ProfileStore::readProfile
                      */

// The empty-slot sentinel must not be a profile id: readProfile() tests
// PROFILE_ID_ACTIVE (== PROFILE_ID_NONE) before the range check, so a sentinel
// that landed inside 0..PROFILE_MAX_ID would take the first branch and read a
// profile for a ring entry that marks a deleted one. The two constants are
// defined independently, so this compares them instead of restating one.
static_assert(PROFILE_ID_NONE > PROFILE_MAX_ID,
              "PROFILE_ID_NONE must stay outside the valid id range");

// readProfile() branches on PROFILE_ID_ACTIVE, not on PROFILE_ID_NONE, so pin
// that symbol too: it is the one the first branch compares.
static_assert(PROFILE_ID_ACTIVE > PROFILE_MAX_ID,
              "PROFILE_ID_ACTIVE must stay outside the valid id range");

// Body lengths travel as uint16_t (writeFactoryProfile, createProfile,
// modifyProfile all take one), so the staging size must fit that type.
static_assert(PROFILE_STAGING_SIZE <= 0xFFFF,
              "PROFILE_STAGING_SIZE must fit as a uint16_t body length");

static constexpr uint32_t BOOTMETA_BASE = 0x000000; /**< BootMeta Ring base */
static constexpr uint32_t BOOTMETA_SIZE =
    0x000800; /**< BootMeta Ring size (2048 bytes) */
static constexpr uint32_t ADDR_RING_BASE =
    0x000800; /**< Profile Address Ring base */
static constexpr uint32_t ADDR_RING_SIZE =
    0x000800; /**< Profile Address Ring size (2048 bytes) */
static constexpr uint32_t PROFILE0_ADDR =
    0x001000; /**< Profile 0 primary copy */
static constexpr uint32_t PROFILE0_BACKUP =
    0x002000; /**< Sector 2: Profile 0 backup */
static constexpr uint32_t USER_RING_BASE =
    0x003000; /**< User Ring start address */

static constexpr uint16_t BOOTMETA_MAGIC = 0x5442; /**< "TB" magic */

static constexpr uint16_t BOOTMETA_SLOTS = 128;  /**< BootMeta Ring capacity */
static constexpr uint16_t ADDR_RING_SLOTS = 256;  /**< Address Ring capacity */

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

/** Read-only view of a profile body. `data` is NUL-terminated and points into
 * the store's staging buffer, so the view is invalidated by the next call that
 * reads or writes a profile. The buffer is not writable through this view.
 * That buffer is PROFILE_STAGING_SIZE bytes, one more than the largest body, so
 * the terminator of a full-length body still lands inside it. */
struct ProfileText {
  const char *data = nullptr;
  uint16_t len = 0;
};

/** Runtime status of the profile system. */
struct ProfileStatus {
  uint16_t activeId = 0;       // currently active profile ID
  uint8_t profileCount = 0;    // number of valid profiles (excluding empty)
  uint32_t totalSectors = 0;   // total flash sectors
  uint32_t usedSectors = 0;    // sectors occupied by profile data
  uint32_t freeSectors = 0;    // sectors still available
  uint32_t nextAddr = 0;       // User Ring next write pointer
  uint16_t bootMetaSeq = 0;    // current BootMeta sequence number
  uint16_t addressRingSeq = 0; // current Address Ring sequence number
};

// ── ProfileStore class ──

/**
 * Profile configuration storage manager on external SPI flash (W25Q64).
 * Ring-based layout using monotonic sequence numbers for crash-safe updates.
 */
class ProfileStore {
public:
  ProfileStore() = default;
  ProfileStore(const ProfileStore &) = delete;
  ProfileStore &operator=(const ProfileStore &) = delete;

  static ProfileStore &getInstance();

  /** Scan BootMeta and Address Rings, determine active profile and next write
   * addr. Writes nothing: see needsFactoryProfile() for the flash that carries
   * no profile at all. */
  bool init();

  /** True when the flash carries no valid BootMeta and no Profile0 body: the
   * holder of the configuration writes the factory Profile0.
   *
   * A property of the flash as it stands right now, answered from the caches
   * init() filled plus a read of the Profile0 area — never remembered from an
   * earlier call, because an erase changes the answer without a reboot. Call it
   * after init(). */
  bool needsFactoryProfile() const;

  /** Write factory default Profile0 (only when BootMeta Ring is empty). */
  bool writeFactoryProfile(const char *json, uint16_t len);

  /** Create a new user profile (finds next free ID, appends to User Ring). */
  bool createProfile(const char *json, uint16_t len, uint16_t *newId);

  /** Append a new version of an existing profile to the User Ring. */
  bool modifyProfile(uint16_t id, const char *json, uint16_t len);

  /** Mark a profile as deleted via Address Ring entry (address=0). */
  bool deleteProfile(uint16_t id);

  /** Select a profile as active (appends BootMeta entry). */
  bool selectProfile(uint16_t id);

  /** Load the active profile JSON body into buf. Reports the body length (raw
   * JSON, no parse) in outLen. No terminator is written: the read copies
   * PROFILE_JSON_MAX bytes out of flash and the length scan stops at the first
   * 0x00 or 0xFF inside the window, so when it stops, buf[outLen] is that byte
   * rather than a NUL this function placed; a body that fills the window whole
   * ends at PROFILE_JSON_MAX with no stop byte at outLen.
   *
   * To use the result as a C string, terminate it yourself; the buffer then
   * needs room for that byte too (len + 1, i.e. PROFILE_STAGING_SIZE for a body
   * that could be any size). For the call itself, PROFILE_JSON_MAX bytes in buf
   * are enough — that is the most the read writes.
   *
   * Pass nullptr to read into the store's own staging buffer. */
  bool loadActive(uint8_t *buf, uint16_t *outLen);

  /** Read-only view of the body of profile `id` (PROFILE_ID_ACTIVE selects the
   * active profile). Returns false when there is no such profile. */
  bool readProfile(uint16_t id, ProfileText *out);

  /** Get runtime status of the profile system. */
  ProfileStatus getStatus() const;

  /** Compact User Ring: move all valid profiles to head, reset rings. */
  bool compaction();

private:
  // ── Internal helpers ──
  bool resetSector0();
  /** Read the raw body at `address` into buf, which must hold at least
   * PROFILE_JSON_MAX bytes (nullptr means the store's staging buffer), and
   * report its length in outLen. Writes no terminator: the read copies
   * PROFILE_JSON_MAX bytes over from flash and the length scan stops at the
   * first 0x00 or 0xFF among them. */
  bool readBody(uint32_t address, uint8_t *buf, uint16_t *outLen);
  uint16_t crc16BootMeta(const BootMeta *meta) const;
  void seqBeforeIncrement();
  bool scanBootMeta(uint16_t *outActiveId, uint32_t *outAddress);
  bool scanAddressRing();
  uint32_t findNextAddr() const;
  bool eraseSector0Range(uint32_t addr, uint32_t len);
  uint16_t ensureBootMetaSlot();
  uint16_t ensureAddressRingSlot();

  // ── Cached state (describes the flash as of the last scan) ──
  // init() refreshes every field here; writeFactoryProfile() refreshes the ones
  // a factory write invalidates. needsFactoryProfile() is derived from these on
  // demand rather than stored.
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

/** Staging buffer for profile bodies: large enough for a body of
 * PROFILE_JSON_MAX bytes plus the NUL the scanners read it by. */
COMMON_ZERO_INIT extern uint8_t s_staging[PROFILE_STAGING_SIZE];

} // namespace ThetaGP::Gamepad::Profile
