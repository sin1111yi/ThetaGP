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

#include "gamepad/profile/profile_store.h"
#include "conf/ThetaGP_Config.h" // THETAGP_CFG_HAS_FLASH, the flash switch this file branches on
#include "drivers/device/flash/flash_base.h"
#include "utils/crc.h"
#include "utils/log/log.h"

#include <cstring>

#if THETAGP_CFG_HAS_FLASH

namespace ThetaGP::Gamepad::Profile {

COMMON_ZERO_INIT uint8_t s_staging[PROFILE_STAGING_SIZE];
static COMMON_ZERO_INIT uint8_t s_compaction[PROFILE_JSON_MAX];

// ── Slot index helpers (ring buffer wraparound) ──

static uint16_t bootMetaSlotIndex() {
  auto &flash = Drivers::Device::FlashBase::getInstance();
  for (uint16_t i = 0; i < BOOTMETA_SLOTS; ++i) {
    uint32_t slotAddr = BOOTMETA_BASE + i * sizeof(BootMeta);
    BootMeta meta;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&meta),
                     sizeof(BootMeta))) {
      return UINT16_MAX;
    }
    if (meta.magic != BOOTMETA_MAGIC) {
      return i;
    }
  }
  return BOOTMETA_SLOTS;
}

static uint16_t addressRingSlotIndex() {
  auto &flash = Drivers::Device::FlashBase::getInstance();
  for (uint16_t i = 0; i < ADDR_RING_SLOTS; ++i) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);
    AddressEntry entry;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&entry),
                     sizeof(AddressEntry))) {
      return UINT16_MAX;
    }
    if (entry.profileId == PROFILE_ID_NONE) {
      return i;
    }
  }
  return ADDR_RING_SLOTS;
}

// ── Singleton ──

ProfileStore &ProfileStore::getInstance() {
  static ProfileStore instance;
  return instance;
}

// ── CRC16 for BootMeta ──

uint16_t ProfileStore::crc16BootMeta(const BootMeta *meta) const {
  return Util::Crc::crc16Ccitt(reinterpret_cast<const uint8_t *>(meta), 14);
}

// ── writeBootMeta() ──
// Records the profile a reboot reads its configuration from. init() takes the
// newest entry of the ring, so a body written without one is read back only until
// the next reboot.

bool ProfileStore::writeBootMeta(uint16_t id, uint32_t address) {
  const uint16_t slot = ensureBootMetaSlot();
  if (slot >= BOOTMETA_SLOTS) {
    return false;
  }

  BootMeta bootMeta;
  bootMeta.magic = BOOTMETA_MAGIC;
  bootMeta.seq = ++_bootMetaSeq;
  bootMeta.profileId = id;
  bootMeta.address = address;
  bootMeta.reserved = 0xFFFFFFFF;
  bootMeta.crc16 = 0;
  bootMeta.crc16 = crc16BootMeta(&bootMeta);

  const uint32_t bootMetaSlot =
      BOOTMETA_BASE + (slot * sizeof(BootMeta));
  auto &flash = Drivers::Device::FlashBase::getInstance();
  if (!flash.write(bootMetaSlot, reinterpret_cast<const uint8_t *>(&bootMeta),
                   sizeof(BootMeta))) {
    return false;
  }

  _activeProfileId = id;
  _activeAddress = address;
  return true;
}

// ── seqBeforeIncrement() ──
// A ring whose sequence number reaches 65535 has sector 0 erased and rebuilt at
// sequence 1, so the wrap never makes the lookup pick the older entry.
bool ProfileStore::seqBeforeIncrement() {
  if (_bootMetaSeq >= 65534 || _addressRingSeq >= 65534) {
    LOG_WARN("ProfileStore: seq near overflow (%u/%u), resetting Sector 0",
             _bootMetaSeq, _addressRingSeq);
    return resetSector0();
  }
  return true;
}

// ── ensureBootMetaSlot / ensureAddressRingSlot ──
// A full ring resets sector 0, which leaves its entries at the head of the ring;
// the fresh slot is the one after them.

uint16_t ProfileStore::ensureBootMetaSlot() {
  if (!seqBeforeIncrement()) return BOOTMETA_SLOTS;
  uint16_t slot = bootMetaSlotIndex();
  if (slot == UINT16_MAX) return BOOTMETA_SLOTS;
  if (slot >= BOOTMETA_SLOTS) {
    LOG_INFO("ProfileStore: BootMeta Ring full, resetting Sector 0");
    if (!resetSector0()) return BOOTMETA_SLOTS;
    slot = bootMetaSlotIndex();
  }
  return slot;
}

uint16_t ProfileStore::ensureAddressRingSlot() {
  if (!seqBeforeIncrement()) return ADDR_RING_SLOTS;
  uint16_t slot = addressRingSlotIndex();
  if (slot == UINT16_MAX) return ADDR_RING_SLOTS;
  if (slot >= ADDR_RING_SLOTS) {
    LOG_INFO("ProfileStore: Address Ring full, resetting Sector 0");
    if (!resetSector0()) return ADDR_RING_SLOTS;
    slot = addressRingSlotIndex();
  }
  return slot;
}

// ── init() ──
// Scans both rings of sector 0 and leaves every cache field describing the flash
// as it is now. It writes nothing.

bool ProfileStore::init() {
  LOG_INFO("ProfileStore: init");

  auto &flash = Drivers::Device::FlashBase::getInstance();
  if (!flash.isInitialized()) {
    LOG_ERROR("ProfileStore: flash not initialized");
    return false;
  }

  uint16_t activeId = 0;
  uint32_t activeAddr = PROFILE0_ADDR;
  bool found = scanBootMeta(&activeId, &activeAddr);

  scanAddressRing();
  bool hasFactoryProfile = (_profileAddresses[0] != 0);

  if (!found) {
    LOG_WARN("ProfileStore: no valid BootMeta, fallback to Profile0");
    activeId = 0;
    activeAddr = PROFILE0_ADDR;
    if (hasFactoryProfile) {
      activeAddr = _profileAddresses[0];
    }
  }

  _activeProfileId = activeId;
  _activeAddress = activeAddr;
  _nextAddr = findNextAddr();

  _profileCount = 0;
  for (uint16_t i = 0; i <= PROFILE_MAX_ID; ++i) {
    if (_profileAddresses[i] != 0) {
      _profileCount++;
    }
  }

  LOG_INFO("ProfileStore: init done, active=%u, addr=0x%06lX, next=0x%06lX, "
           "count=%u",
           _activeProfileId, _activeAddress, _nextAddr, _profileCount);
  return true;
}

// ── needsFactoryProfile() ──
// Whether the flash holds a profile at all, answered from the caches init() filled
// plus a read of the Profile 0 area. An erase between two calls changes the
// answer, so it is never remembered.

bool ProfileStore::needsFactoryProfile() const {
  // A BootMeta entry names an active profile: the flash holds configuration.
  if (_bootMetaSeq != 0) {
    return false;
  }
  // A ring entry points at a Profile0 body even when no BootMeta survived.
  if (_profileAddresses[0] != 0) {
    return false;
  }

  auto &flash = Drivers::Device::FlashBase::getInstance();
  if (!flash.isInitialized()) {
    return false;
  }

  // No ring entry, so the Profile 0 body is read directly, into a probe of its
  // own: the answer must not disturb what the caller has staged.
  uint8_t probe[16];
  if (!flash.read(PROFILE0_ADDR, probe, sizeof(probe))) {
    return false;
  }
  for (uint16_t i = 0; i < sizeof(probe); ++i) {
    if (probe[i] != 0xFF) {
      return false;
    }
  }

  return true;
}

bool ProfileStore::scanBootMeta(uint16_t *outActiveId, uint32_t *outAddress) {
  auto &flash = Drivers::Device::FlashBase::getInstance();

  uint16_t maxSeq = 0;
  uint16_t bestId = 0;
  uint32_t bestAddr = PROFILE0_ADDR;

  for (uint16_t i = 0; i < BOOTMETA_SLOTS; ++i) {
    uint32_t slotAddr = BOOTMETA_BASE + i * sizeof(BootMeta);

    BootMeta meta;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&meta),
                    sizeof(BootMeta))) {
      LOG_ERROR("ProfileStore: BootMeta read fail at slot %u", i);
      continue;
    }

    if (meta.magic != BOOTMETA_MAGIC) {
      continue;
    }

    uint16_t expectedCrc = crc16BootMeta(&meta);
    if (meta.crc16 != expectedCrc) {
      LOG_WARN("ProfileStore: BootMeta CRC mismatch at slot %u", i);
      continue;
    }

    if (meta.seq > maxSeq) {
      maxSeq = meta.seq;
      bestId = meta.profileId;
      bestAddr = meta.address;
    }
  }

  _bootMetaSeq = maxSeq;

  if (maxSeq == 0) {
    LOG_INFO("ProfileStore: no valid BootMeta entries found");
    return false;
  }

  *outActiveId = bestId;
  *outAddress = bestAddr;
  LOG_INFO("ProfileStore: BootMeta active=%u, addr=0x%06lX, seq=%u", bestId,
           bestAddr, maxSeq);
  return true;
}

bool ProfileStore::scanAddressRing() {
  auto &flash = Drivers::Device::FlashBase::getInstance();

  for (uint16_t i = 0; i <= PROFILE_MAX_ID; ++i) {
    _profileAddresses[i] = 0;
    _profileSeqs[i] = 0;
  }

  uint16_t maxSeq = 0;

  for (uint16_t i = 0; i < ADDR_RING_SLOTS; ++i) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);

    AddressEntry entry;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&entry),
                    sizeof(AddressEntry))) {
      LOG_ERROR("ProfileStore: Address entry read fail at slot %u", i);
      continue;
    }

    if (entry.profileId == PROFILE_ID_NONE) {
      continue;
    }

    if (entry.profileId > PROFILE_MAX_ID) {
      LOG_WARN("ProfileStore: invalid profileId %u at slot %u", entry.profileId,
               i);
      continue;
    }

    if (entry.seq > _profileSeqs[entry.profileId]) {
      _profileSeqs[entry.profileId] = entry.seq;
      _profileAddresses[entry.profileId] =
          (entry.address == 0) ? 0 : entry.address;
    }

    if (entry.seq > maxSeq) {
      maxSeq = entry.seq;
    }
  }

  _addressRingSeq = maxSeq;
  LOG_INFO("ProfileStore: AddressRing scan done, seq=%u", maxSeq);
  return true;
}

uint32_t ProfileStore::findNextAddr() const {
  auto &flash = Drivers::Device::FlashBase::getInstance();
  const auto &info = flash.getInfo();
  uint32_t flashSize = info.sizeBytes;

  // The highest address the map places inside the user ring, and 0 while it holds
  // none: an address below USER_RING_BASE lies outside the ring.
  uint32_t ringUsedUpTo = 0;
  for (uint16_t i = 0; i <= PROFILE_MAX_ID; ++i) {
    if (_profileAddresses[i] >= USER_RING_BASE &&
        _profileAddresses[i] > ringUsedUpTo) {
      ringUsedUpTo = _profileAddresses[i];
    }
  }

  // Nothing occupies the ring: its head is free, and the first user profile
  // goes there.
  if (ringUsedUpTo < USER_RING_BASE) {
    return USER_RING_BASE;
  }

  uint32_t next = (ringUsedUpTo & ~0xFFF) + 0x1000;

  if (next >= flashSize) {
    return flashSize;
  }

  return next;
}

// ── writeFactoryProfile() ──

bool ProfileStore::writeFactoryProfile(const char *json, uint16_t len) {
  LOG_INFO("ProfileStore: writeFactoryProfile, len=%u", len);

  if (len == 0 || len > PROFILE_JSON_MAX) {
    LOG_ERROR("ProfileStore: invalid profile length %u", len);
    return false;
  }

  if (_bootMetaSeq > 0) {
    LOG_WARN("ProfileStore: factory profile already exists");
    return false;
  }

  auto &flash = Drivers::Device::FlashBase::getInstance();

  if (!flash.write(PROFILE0_ADDR, reinterpret_cast<const uint8_t *>(json),
                   len)) {
    LOG_ERROR("ProfileStore: failed to write Profile0 primary");
    return false;
  }

  if (!flash.eraseSector(PROFILE0_BACKUP)) {
    LOG_ERROR("ProfileStore: failed to erase Profile0 backup sector");
    return false;
  }
  if (!flash.write(PROFILE0_BACKUP, reinterpret_cast<const uint8_t *>(json),
                   len)) {
    LOG_ERROR("ProfileStore: failed to write Profile0 backup");
    return false;
  }

  BootMeta meta;
  meta.magic = BOOTMETA_MAGIC;
  meta.seq = 1;
  meta.profileId = 0;
  meta.address = PROFILE0_ADDR;
  meta.reserved = 0xFFFFFFFF;
  meta.crc16 = 0;
  meta.crc16 = crc16BootMeta(&meta);

  if (!flash.write(BOOTMETA_BASE, reinterpret_cast<const uint8_t *>(&meta),
                   sizeof(BootMeta))) {
    LOG_ERROR("ProfileStore: failed to write BootMeta slot 0");
    return false;
  }

  // ── the cache as a fresh scan would produce it ──
  // Each field below holds what scanBootMeta() and scanAddressRing() over this
  // flash would report, so the cache and the flash do not drift apart.
  _bootMetaSeq = 1;               // BootMeta slot 0: seq
  _activeProfileId = 0;           // BootMeta slot 0: profileId
  _activeAddress = PROFILE0_ADDR; // BootMeta slot 0: address

  // The Address Ring entry that makes the body findable (scanAddressRing()
  // reads the rings, not the fixed PROFILE0_ADDR).
  AddressEntry addrEntry;
  addrEntry.profileId = 0;
  addrEntry.address = PROFILE0_ADDR;
  addrEntry.seq = 1;

  if (!flash.write(ADDR_RING_BASE,
                   reinterpret_cast<const uint8_t *>(&addrEntry),
                   sizeof(AddressEntry))) {
    // The ring holds no entry for id 0, which is what a rescan of this flash
    // reports. The body stays readable: readProfile(0) reads PROFILE0_ADDR.
    LOG_WARN("ProfileStore: failed to write Address entry for profile0");
    _addressRingSeq = 0;
    _profileAddresses[0] = 0;
    _profileSeqs[0] = 0;
  } else {
    _addressRingSeq = 1;               // Address Ring slot 0: seq
    _profileAddresses[0] = PROFILE0_ADDR; // Address Ring slot 0: address
    _profileSeqs[0] = 1;               // Address Ring slot 0: seq
  }

  // profileCount is a property of the address map, recounted by the rule init()
  // uses: the failed branch above clears id 0 only.
  _profileCount = 0;
  for (uint16_t i = 0; i <= PROFILE_MAX_ID; ++i) {
    if (_profileAddresses[i] != 0) {
      _profileCount++;
    }
  }

  _nextAddr = findNextAddr();        // no flash field of its own: derived from
                                     // the address map (Profile0 sits below
                                     // USER_RING_BASE, so the head is free)

  LOG_INFO("ProfileStore: factory profile written");
  return true;
}

bool ProfileStore::replaceFactoryProfile(const char *json, uint16_t len) {
  LOG_INFO("ProfileStore: writeFactoryProfile, len=%u", len);

  if (!json || len == 0 || len > PROFILE_JSON_MAX) {
    LOG_ERROR("ProfileStore: invalid profile length %u", len);
    return false;
  }

  // The caller is replacing the body the flash holds: the store's record of it
  // already exists and stays where it is, and the sector it sits in is erased
  // first -- a NOR cell takes a write onto an erased one and nothing else.
  auto &flash = Drivers::Device::FlashBase::getInstance();

  if (!flash.eraseSector(PROFILE0_ADDR)) {
    LOG_ERROR("ProfileStore: failed to erase Profile0 before replace");
    return false;
  }

  if (!flash.write(PROFILE0_ADDR, reinterpret_cast<const uint8_t *>(json),
                   len)) {
    LOG_ERROR("ProfileStore: failed to write Profile0 primary");
    return false;
  }

  if (!flash.eraseSector(PROFILE0_BACKUP)) {
    LOG_ERROR("ProfileStore: failed to erase Profile0 backup sector");
    return false;
  }
  if (!flash.write(PROFILE0_BACKUP, reinterpret_cast<const uint8_t *>(json),
                   len)) {
    LOG_ERROR("ProfileStore: failed to write Profile0 backup");
    return false;
  }

  // A body the flash carries no record of is recorded here, so a store that
  // lost its rings still lists it: the body is reachable through the fixed
  // address either way, but the ring entry is what a scan reports as a profile.
  if (_profileAddresses[0] == 0) {
    const uint16_t slot = ensureAddressRingSlot();
    if (slot >= ADDR_RING_SLOTS) {
      LOG_ERROR("ProfileStore: no room to record the factory profile");
      return false;
    }

    _addressRingSeq++;
    AddressEntry entry;
    entry.profileId = 0;
    entry.address = PROFILE0_ADDR;
    entry.seq = _addressRingSeq;

    if (!flash.write(ADDR_RING_BASE + slot * sizeof(AddressEntry),
                     reinterpret_cast<const uint8_t *>(&entry),
                     sizeof(AddressEntry))) {
      LOG_ERROR("ProfileStore: failed to record the factory profile");
      return false;
    }

    _profileAddresses[0] = PROFILE0_ADDR;
    _profileSeqs[0] = _addressRingSeq;
    _profileCount++;

    // Nothing named an active profile, so the one recorded is the one init()
    // falls back to.
    if (_bootMetaSeq == 0 && !writeBootMeta(0, PROFILE0_ADDR)) {
      return false;
    }
  }

  LOG_INFO("ProfileStore: factory profile written");
  return true;
}

// ── createProfile() ──

bool ProfileStore::createProfile(const char *json, uint16_t len,
                                 uint16_t *newId) {
  LOG_INFO("ProfileStore: createProfile, len=%u", len);

  if (!json || len == 0 || len > PROFILE_JSON_MAX) {
    LOG_ERROR("ProfileStore: invalid profile data");
    return false;
  }

  uint16_t id = PROFILE_MAX_ID + 1;
  for (uint16_t i = 1; i <= PROFILE_MAX_ID; ++i) {
    if (_profileAddresses[i] == 0) {
      id = i;
      break;
    }
  }

  if (id > PROFILE_MAX_ID) {
    LOG_ERROR("ProfileStore: no free profile slots (max %u)", PROFILE_MAX_ID);
    return false;
  }

  auto &flash = Drivers::Device::FlashBase::getInstance();
  const auto &info = flash.getInfo();

  if (_nextAddr >= info.sizeBytes) {
    LOG_WARN("ProfileStore: User Ring full, compaction needed");
    if (!compaction()) {
      LOG_ERROR("ProfileStore: compaction failed");
      return false;
    }
  }

  uint32_t dataAddr = _nextAddr;
  if (!flash.eraseSector(dataAddr)) {
    LOG_ERROR("ProfileStore: failed to erase sector before write");
    return false;
  }
  if (!flash.write(dataAddr, reinterpret_cast<const uint8_t *>(json), len)) {
    LOG_ERROR("ProfileStore: failed to write profile data");
    return false;
  }

  _nextAddr = ((dataAddr & ~0xFFF) + 0x1000);

  const uint16_t slot = ensureAddressRingSlot();
  if (slot >= ADDR_RING_SLOTS) return false;
  _addressRingSeq++;
  AddressEntry addrEntry;
  addrEntry.profileId = id;
  addrEntry.address = dataAddr;
  addrEntry.seq = _addressRingSeq;

  uint32_t addrSlot =
      ADDR_RING_BASE + (slot * sizeof(AddressEntry));
  if (!flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&addrEntry),
                   sizeof(AddressEntry))) {
    LOG_ERROR("ProfileStore: failed to write Address entry");
    return false;
  }

  _profileAddresses[id] = dataAddr;
  _profileSeqs[id] = _addressRingSeq;
  _profileCount++;
  if (!writeBootMeta(id, dataAddr)) {
    LOG_ERROR("ProfileStore: failed to write BootMeta");
    return false;
  }

  if (newId) {
    *newId = id;
  }

  LOG_INFO("ProfileStore: created profile %u at 0x%06lX", id, dataAddr);
  return true;
}

// ── modifyProfile() ──

bool ProfileStore::modifyProfile(uint16_t id, const char *json, uint16_t len) {
  LOG_INFO("ProfileStore: modifyProfile id=%u, len=%u", id, len);

  if (id == 0 || id > PROFILE_MAX_ID) {
    LOG_ERROR("ProfileStore: invalid profile ID %u", id);
    return false;
  }

  if (_profileAddresses[id] == 0) {
    LOG_ERROR("ProfileStore: profile %u not found", id);
    return false;
  }

  if (!json || len == 0 || len > PROFILE_JSON_MAX) {
    LOG_ERROR("ProfileStore: invalid profile data");
    return false;
  }

  auto &flash = Drivers::Device::FlashBase::getInstance();
  const auto &info = flash.getInfo();

  if (_nextAddr >= info.sizeBytes) {
    LOG_WARN("ProfileStore: User Ring full, compaction needed");
    if (!compaction()) {
      LOG_ERROR("ProfileStore: compaction failed");
      return false;
    }
  }

  uint32_t dataAddr = _nextAddr;
  if (!flash.eraseSector(dataAddr)) {
    LOG_ERROR("ProfileStore: failed to erase sector before modify");
    return false;
  }
  if (!flash.write(dataAddr, reinterpret_cast<const uint8_t *>(json), len)) {
    LOG_ERROR("ProfileStore: failed to write modified profile");
    return false;
  }

  _nextAddr = ((dataAddr & ~0xFFF) + 0x1000);

  const uint16_t slot = ensureAddressRingSlot();
  if (slot >= ADDR_RING_SLOTS) return false;
  _addressRingSeq++;
  AddressEntry addrEntry;
  addrEntry.profileId = id;
  addrEntry.address = dataAddr;
  addrEntry.seq = _addressRingSeq;

  uint32_t addrSlot =
      ADDR_RING_BASE + (slot * sizeof(AddressEntry));
  if (!flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&addrEntry),
                   sizeof(AddressEntry))) {
    LOG_ERROR("ProfileStore: failed to write Address entry for modify");
    return false;
  }

  _profileAddresses[id] = dataAddr;
  _profileSeqs[id] = _addressRingSeq;

  // The body the device boots from moved, so the record that names it moves
  // with it: without this a reboot reads the body this write replaced.
  if (_activeProfileId == id && !writeBootMeta(id, dataAddr)) {
    LOG_ERROR("ProfileStore: failed to write BootMeta for modify");
    return false;
  }

  LOG_INFO("ProfileStore: modified profile %u at 0x%06lX", id, dataAddr);
  return true;
}

// ── deleteProfile() ──

bool ProfileStore::deleteProfile(uint16_t id) {
  LOG_INFO("ProfileStore: deleteProfile id=%u", id);

  if (id == 0 || id > PROFILE_MAX_ID) {
    LOG_ERROR("ProfileStore: invalid profile ID %u", id);
    return false;
  }

  if (_profileAddresses[id] == 0) {
    LOG_WARN("ProfileStore: profile %u already deleted or not found", id);
    return false;
  }

  auto &flash = Drivers::Device::FlashBase::getInstance();

  // Mark deleted: write Address Ring entry with address=0, seq=high
  const uint16_t slot = ensureAddressRingSlot();
  if (slot >= ADDR_RING_SLOTS) return false;
  _addressRingSeq++;
  AddressEntry addrEntry;
  addrEntry.profileId = id;
  addrEntry.address = 0;
  addrEntry.seq = _addressRingSeq;

  uint32_t addrSlot =
      ADDR_RING_BASE + (slot * sizeof(AddressEntry));
  if (!flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&addrEntry),
                   sizeof(AddressEntry))) {
    LOG_ERROR("ProfileStore: failed to write deletion Address entry");
    return false;
  }

  _profileAddresses[id] = 0;
  _profileSeqs[id] = _addressRingSeq;
  _profileCount--;

  // If deleting active profile, fallback to Profile0
  if (_activeProfileId == id) {
    LOG_WARN("ProfileStore: deleting active profile %u, fallback to Profile0",
             id);
    _activeProfileId = 0;
    _activeAddress = PROFILE0_ADDR;

    if (_profileAddresses[0] != 0) {
      _activeAddress = _profileAddresses[0];
    }

    if (!writeBootMeta(0, _activeAddress)) {
      LOG_WARN("ProfileStore: fallback BootMeta write failed (non-critical)");
    }
  }

  LOG_INFO("ProfileStore: deleted profile %u", id);
  return true;
}

// ── selectProfile() ──

bool ProfileStore::selectProfile(uint16_t id) {
  LOG_INFO("ProfileStore: selectProfile id=%u", id);

  if (id > PROFILE_MAX_ID) {
    LOG_ERROR("ProfileStore: invalid profile ID %u", id);
    return false;
  }

  if (_profileAddresses[id] == 0 && id != 0) {
    LOG_ERROR("ProfileStore: profile %u does not exist", id);
    return false;
  }

  const uint32_t address = (id == 0) ? PROFILE0_ADDR : _profileAddresses[id];

  if (!writeBootMeta(id, address)) {
    LOG_ERROR("ProfileStore: failed to write BootMeta for select");
    return false;
  }

  LOG_INFO("ProfileStore: selected profile %u at 0x%06lX", id, address);
  return true;
}

// ── readBody() ──
// Reads raw JSON from a flash address into buf; caller drives the parse.

bool ProfileStore::readBody(uint32_t address, uint8_t *buf, uint16_t *outLen) {
  auto &flash = Drivers::Device::FlashBase::getInstance();

  if (!buf) {
    buf = s_staging;
  }

  if (!flash.read(address, buf, PROFILE_JSON_MAX)) {
    LOG_ERROR("ProfileStore: failed to read profile at 0x%06lX", address);
    return false;
  }

  uint16_t actualLen = 0;
  for (uint16_t i = 0; i < PROFILE_JSON_MAX; ++i) {
    if (buf[i] == 0 || buf[i] == 0xFF) {
      break;
    }
    actualLen = i + 1;
  }

  if (outLen) {
    *outLen = actualLen;
  }

  LOG_DEBUG("ProfileStore: read profile at 0x%06lX, %u bytes", address,
            actualLen);
  return true;
}

// Reserved: not implemented, so it reads nothing and answers that it did not.
bool ProfileStore::loadActive(uint8_t *, uint16_t *) { return false; }

// ── carriesProfile() ──
// The two ids readProfile() resolves without the address map, and the map itself
// for every other id. Nothing here reads the flash.

bool ProfileStore::carriesProfile(uint16_t id) const {
  if (id == PROFILE_ID_ACTIVE || id == 0) {
    return true;
  }
  return id <= PROFILE_MAX_ID && _profileAddresses[id] != 0;
}

// ── readProfile() ──

bool ProfileStore::readProfile(uint16_t id, ProfileText *out) {
  if (!out) {
    return false;
  }
  out->data = nullptr;
  out->len = 0;

  uint32_t address = 0;
  if (id == PROFILE_ID_ACTIVE) {
    address = _activeAddress;
  } else if (id == 0) {
    address = PROFILE0_ADDR;
  } else if (id <= PROFILE_MAX_ID && _profileAddresses[id] != 0) {
    address = _profileAddresses[id];
  } else {
    return false;
  }

  uint16_t len = 0;
  if (!readBody(address, s_staging, &len)) {
    return false;
  }

  // readBody reports at most PROFILE_JSON_MAX bytes, one below the size of the
  // buffer, so the terminator lands inside it.
  s_staging[len] = '\0';
  out->data = reinterpret_cast<const char *>(s_staging);
  out->len = len;
  return true;
}

// ── getStatus() ──

ProfileStatus ProfileStore::getStatus() const {
  ProfileStatus status;
  status.activeId = _activeProfileId;
  status.profileCount = _profileCount;
  status.nextAddr = _nextAddr;
  status.bootMetaSeq = _bootMetaSeq;
  status.addressRingSeq = _addressRingSeq;

  auto &flash = Drivers::Device::FlashBase::getInstance();
  const auto &info = flash.getInfo();

  status.totalSectors = info.sizeBytes / PROFILE_SECTOR_SIZE;
  status.usedSectors = (_nextAddr - USER_RING_BASE) / PROFILE_SECTOR_SIZE;
  status.reservedSectors = RESERVED_SECTORS;
  status.freeSectors =
      status.totalSectors - status.reservedSectors - status.usedSectors;

  return status;
}

// ── compaction() ──
// Moves the valid profiles to the head of the user ring and resets both rings,
// which is what a ring short of space needs.

bool ProfileStore::compaction() {
  LOG_INFO("ProfileStore: compaction start");

  auto &flash = Drivers::Device::FlashBase::getInstance();

  // 1. The valid user profiles: id 0 stays at PROFILE0_ADDR and its backup is
  //    never touched.
  struct ValidProfile {
    uint16_t id;
    uint32_t oldAddr;
  };
  ValidProfile valid[16];
  uint8_t validCount = 0;

  for (uint16_t i = 1; i <= PROFILE_MAX_ID; ++i) {
    if (_profileAddresses[i] != 0) {
      valid[validCount].id = i;
      valid[validCount].oldAddr = _profileAddresses[i];
      validCount++;
    }
  }

  if (validCount == 0) {
    _nextAddr = USER_RING_BASE;
    LOG_INFO("ProfileStore: no valid profiles to compact");
    return true;
  }

  // Sorted by address, so the rewrite below never erases a sector holding a
  // profile it has not read yet.
  for (uint8_t i = 0; i < validCount; ++i) {
    for (uint8_t j = i + 1; j < validCount; ++j) {
      if (valid[j].oldAddr < valid[i].oldAddr) {
        ValidProfile tmp = valid[i];
        valid[i] = valid[j];
        valid[j] = tmp;
      }
    }
  }

  if (_activeProfileId != 0 && _profileAddresses[_activeProfileId] == 0) {
    LOG_ERROR("ProfileStore: active profile is absent during compaction");
    return false;
  }

  // 2. Rewrite each valid profile at User Ring head (S3 onward)
  uint32_t writeAddr = USER_RING_BASE;

  for (uint8_t i = 0; i < validCount; ++i) {
    memset(s_compaction, 0xFF, PROFILE_JSON_MAX);
    // read old
    if (!flash.read(valid[i].oldAddr, s_compaction, PROFILE_JSON_MAX)) {
      LOG_ERROR("ProfileStore: compaction read fail at 0x%06lX",
                valid[i].oldAddr);
      return false;
    }

    // erase target
    if (!flash.eraseSector(writeAddr)) {
      LOG_ERROR("ProfileStore: compaction erase fail at 0x%06lX", writeAddr);
      return false;
    }
    // write target
    if (!flash.write(writeAddr, s_compaction, PROFILE_JSON_MAX)) {
      LOG_ERROR("ProfileStore: compaction write fail at 0x%06lX", writeAddr);
      return false;
    }

    _profileAddresses[valid[i].id] = writeAddr;
    writeAddr += 0x1000;
  }

  // 3. Reset BootMeta for the active profile
  BootMeta bootMeta;
  bootMeta.magic = BOOTMETA_MAGIC;
  bootMeta.seq = 1;
  bootMeta.profileId = _activeProfileId;
  bootMeta.address = _profileAddresses[_activeProfileId];
  bootMeta.reserved = 0xFFFFFFFF;
  bootMeta.crc16 = 0;
  bootMeta.crc16 = crc16BootMeta(&bootMeta);

  // 4. Erases both rings of sector 0; they are rebuilt below.
  if (!eraseSector0Range(BOOTMETA_BASE, BOOTMETA_SIZE)) {
    LOG_ERROR("ProfileStore: compaction BootMeta erase fail");
    return false;
  }
  if (!flash.write(BOOTMETA_BASE, reinterpret_cast<const uint8_t *>(&bootMeta),
                   sizeof(BootMeta))) {
    LOG_ERROR("ProfileStore: compaction BootMeta write fail");
    return false;
  }

  // 5. Rebuild Address Ring: all compacted user profiles + profile0
  _addressRingSeq = 0;
  for (uint8_t i = 0; i < validCount; ++i) {
    _addressRingSeq++;
    AddressEntry addrEntry;
    addrEntry.profileId = valid[i].id;
    addrEntry.address = _profileAddresses[valid[i].id];
    addrEntry.seq = _addressRingSeq;

    uint32_t addrSlot = ADDR_RING_BASE + i * sizeof(AddressEntry);
    if (!flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&addrEntry),
                     sizeof(AddressEntry))) {
      LOG_ERROR("ProfileStore: compaction Address write fail");
      return false;
    }
    _profileSeqs[valid[i].id] = _addressRingSeq;
  }

  // Profile0 Address Ring entry (fixed address, never moved by compaction)
  {
    _addressRingSeq++;
    AddressEntry addrEntry;
    addrEntry.profileId = 0;
    addrEntry.address = PROFILE0_ADDR;
    addrEntry.seq = _addressRingSeq;

    uint32_t addrSlot =
        ADDR_RING_BASE + validCount * sizeof(AddressEntry);
    if (!flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&addrEntry),
                      sizeof(AddressEntry))) return false;
    _profileAddresses[0] = PROFILE0_ADDR;
    _profileSeqs[0] = _addressRingSeq;
  }

  _activeAddress = bootMeta.address;
  _bootMetaSeq = bootMeta.seq;
  _nextAddr = writeAddr;

  return true;
}

// ── resetSector0() ──

bool ProfileStore::resetSector0() {
  LOG_INFO("ProfileStore: resetSector0");

  auto &flash = Drivers::Device::FlashBase::getInstance();

  // Backup current BootMeta
  BootMeta activeBootMeta;
  memset(&activeBootMeta, 0, sizeof(activeBootMeta));
  activeBootMeta.magic = BOOTMETA_MAGIC;
  activeBootMeta.seq = 1;
  activeBootMeta.profileId = _activeProfileId;
  activeBootMeta.address = _activeAddress;
  activeBootMeta.reserved = 0xFFFFFFFF;
  activeBootMeta.crc16 = crc16BootMeta(&activeBootMeta);

  // Backup valid Address entries
  AddressEntry validEntries[16];
  uint8_t entryCount = 0;
  for (uint16_t i = 0; i <= PROFILE_MAX_ID; ++i) {
    if (_profileAddresses[i] != 0) {
      validEntries[entryCount].profileId = i;
      validEntries[entryCount].address = _profileAddresses[i];
      validEntries[entryCount].seq = entryCount + 1;
      entryCount++;
    }
  }

  // Erase Sector 0
  if (!flash.eraseSector(0x000000)) {
    LOG_ERROR("ProfileStore: Sector 0 erase failed");
    return false;
  }

  // Write BootMeta slot 0
  if (!flash.write(BOOTMETA_BASE,
                   reinterpret_cast<const uint8_t *>(&activeBootMeta),
                   sizeof(BootMeta))) {
    LOG_ERROR("ProfileStore: failed to write BootMeta after Sector 0 reset");
    return false;
  }

  // Restore valid Address entries.
  for (uint8_t i = 0; i < entryCount; ++i) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);
    if (!flash.write(slotAddr,
                     reinterpret_cast<const uint8_t *>(&validEntries[i]),
                     sizeof(AddressEntry))) {
      LOG_WARN("ProfileStore: Address entry restore failed at slot %u", i);
      return false;
    }
  }

  LOG_INFO("ProfileStore: Sector 0 reset done");

  // Update cached seq counters to match what was just written to flash
  _bootMetaSeq = 1;
  // Address Ring seq: use the max of restored entries, or 0 if none
  _addressRingSeq = 0;
  memset(_profileSeqs, 0, sizeof(_profileSeqs));
  for (uint8_t i = 0; i < entryCount; ++i) {
    _profileSeqs[validEntries[i].profileId] = validEntries[i].seq;
    if (validEntries[i].seq > _addressRingSeq) {
      _addressRingSeq = validEntries[i].seq;
    }
  }

  return true;
}

// ── erase a region within Sector 0 ──

bool ProfileStore::eraseSector0Range(uint32_t addr, uint32_t len) {
  (void)len;
  auto &flash = Drivers::Device::FlashBase::getInstance();

  if (addr < 0x1000) {
    return flash.eraseSector(0x000000);
  }

  LOG_ERROR("ProfileStore: eraseSector0Range out of range: 0x%06lX", addr);
  return false;
}

} // namespace ThetaGP::Gamepad::Profile

#endif // THETAGP_CFG_HAS_FLASH
