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
// Layout: Sector 0 = BootMeta Ring (64x16B) + Address Ring (128x8B) + Profile0
//         Sector 1 = Profile0 factory backup
//         Sectors 2..N = User Ring (linear append area)

static constexpr uint32_t PROFILE_JSON_MAX =
    2048; /**< Max JSON body size per profile */
static constexpr uint16_t PROFILE_MAX_ID = 15; /**< Maximum profile ID (0-15) */
static constexpr uint16_t PROFILE_ID_NONE =
    0xFFFF; /**< Sentinel for empty AddressEntry */

static constexpr uint32_t BOOTMETA_BASE = 0x000000; /**< BootMeta Ring base */
static constexpr uint32_t BOOTMETA_SIZE =
    0x000400; /**< BootMeta Ring size (1024 bytes) */
static constexpr uint32_t ADDR_RING_BASE =
    0x000400; /**< Profile Address Ring base */
static constexpr uint32_t ADDR_RING_SIZE =
    0x000400; /**< Profile Address Ring size (1024 bytes) */
static constexpr uint32_t PROFILE0_ADDR =
    0x000800; /**< Profile 0 primary copy */
static constexpr uint32_t PROFILE0_BACKUP =
    0x001000; /**< Sector 1: Profile 0 backup */
static constexpr uint32_t USER_RING_BASE =
    0x002000; /**< User Ring start address */

static constexpr uint16_t BOOTMETA_MAGIC = 0x5442; /**< "TB" magic */

static constexpr uint16_t BOOTMETA_SLOTS = 64;   /**< BootMeta Ring capacity */
static constexpr uint16_t ADDR_RING_SLOTS = 128; /**< Address Ring capacity */

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
   * addr. */
  bool init();

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

  /** Load the active profile JSON body into buf. Returns actual length (raw
   * JSON, no parse). */
  bool loadActive(uint8_t *buf, uint16_t *outLen);

  /** Get runtime status of the profile system. */
  ProfileStatus getStatus() const;

  /** Compact User Ring: move all valid profiles to head, reset rings. */
  bool compaction();

private:
  // ── Internal helpers ──
  bool resetSector0();
  uint16_t crc16BootMeta(const BootMeta *meta) const;
  void seqBeforeIncrement();
  bool scanBootMeta(uint16_t *outActiveId, uint32_t *outAddress);
  bool scanAddressRing();
  uint32_t findNextAddr() const;
  bool eraseSector0Range(uint32_t addr, uint32_t len);

  // ── Cached state (updated by init()) ──
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

/** DMA-safe staging buffer for profile operations (4KB). */
COMMON_ZERO_INIT extern uint8_t s_staging[4096];

} // namespace ThetaGP::Gamepad::Profile
