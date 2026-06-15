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
#include "drivers/device/flash/flash_w25qxx.h"
#include "utils/log/log.h"

#include "utils/json/json.h"
#include <cstring>

namespace ThetaGP::Gamepad::Profile {

COMMON_ZERO_INIT uint8_t s_staging[4096];

// ── CRC16 (CCITT, poly=0x1021) ──

static uint16_t crc16Ccitt(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t j = 0; j < 8; ++j) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// ── Slot index helpers (ring buffer wraparound) ──

static uint16_t bootMetaSlotIndex() {
  auto &flash = Drivers::Device::FlashW25qxx::getInstance();
  for (uint16_t i = 0; i < BOOTMETA_SLOTS; ++i) {
    uint32_t slotAddr = BOOTMETA_BASE + i * sizeof(BootMeta);
    BootMeta meta;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&meta),
                    sizeof(BootMeta))) {
      return 0;
    }
    if (meta.magic != BOOTMETA_MAGIC) {
      return i;
    }
  }
  return BOOTMETA_SLOTS;
}

static uint16_t addressRingSlotIndex() {
  auto &flash = Drivers::Device::FlashW25qxx::getInstance();
  for (uint16_t i = 0; i < ADDR_RING_SLOTS; ++i) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);
    AddressEntry entry;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&entry),
                    sizeof(AddressEntry))) {
      return 0;
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
  return crc16Ccitt(reinterpret_cast<const uint8_t *>(meta), 14);
}

// ── seqBeforeIncrement: check seq overflow before incrementing ──
// When BootMeta or Address Ring seq hits 65535, Sector 0 is erased and
// rebuilt with seq=1. This prevents uint16_t overflow that would cause
// the highest-seq lookup to pick the wrong entry.
void ProfileStore::seqBeforeIncrement() {
  if (_bootMetaSeq >= 65534 || _addressRingSeq >= 65534) {
    LOG_WARN("ProfileStore: seq near overflow (%u/%u), resetting Sector 0",
             _bootMetaSeq, _addressRingSeq);
    resetSector0();
  }
}

// ── ensureBootMetaSlot / ensureAddressRingSlot ──
// When the corresponding ring is full, trigger Sector 0 reset (which
// moves valid data back to slot 0), then return the fresh slot index.

uint16_t ProfileStore::ensureBootMetaSlot() {
  uint16_t slot = bootMetaSlotIndex();
  if (slot >= BOOTMETA_SLOTS) {
    LOG_INFO("ProfileStore: BootMeta Ring full, resetting Sector 0");
    resetSector0();
    slot = bootMetaSlotIndex();
  }
  return slot;
}

uint16_t ProfileStore::ensureAddressRingSlot() {
  uint16_t slot = addressRingSlotIndex();
  if (slot >= ADDR_RING_SLOTS) {
    LOG_INFO("ProfileStore: Address Ring full, resetting Sector 0");
    resetSector0();
    slot = addressRingSlotIndex();
  }
  return slot;
}

// ── init() ──

bool ProfileStore::init() {
  LOG_INFO("ProfileStore: init");

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();
  if (!flash.isInitialized()) {
    LOG_ERROR("ProfileStore: flash not initialized");
    return false;
  }

  uint16_t activeId = 0;
  uint32_t activeAddr = PROFILE0_ADDR;
  bool found = scanBootMeta(&activeId, &activeAddr);

  scanAddressRing();
  bool hasFactoryProfile = (_profileAddresses[0] != 0);

  // Check for fresh flash: no valid BootMeta AND the Profile0 area is erased
  if (!found) {
    bool profile0Erased = true;
    flash.read(PROFILE0_ADDR, s_staging, PROFILE_JSON_MAX);
    for (uint16_t i = 0; i < 16; ++i) {
      if (s_staging[i] != 0xFF) {
        profile0Erased = false;
        break;
      }
    }

    if (profile0Erased && !hasFactoryProfile) {
      LOG_INFO("ProfileStore: fresh flash detected, writing default Profile0");
      // Build minimal default config JSON using frozen printf
      Json doc;
      doc.beginWrite(reinterpret_cast<char *>(s_staging), PROFILE_JSON_MAX);
      doc.printf("{ver:1,map:{socd:%d,four_way:%d,dpad:%d,"
                 "inv_x:%d,inv_y:%d,inv_rx:%d,inv_ry:%d,swap:%d,btn_map:[",
                 0, 0, 0, 0, 0, 0, 0, 0);
      for (int i = 0; i < 32; ++i) {
        if (i > 0) doc.printf(",");
        doc.printf("%d", i);
      }
      doc.printf("],stick:{lx_dz:%d,ly_dz:%d,rx_dz:%d,ry_dz:%d,"
                 "lx_sens:%d,ly_sens:%d,rx_sens:%d,ry_sens:%d,curve:%d,ema:%d},"
                 "trig:{lt_dz:%d,rt_dz:%d},usb:{poll:%d},"
                 "led:{bri:%d,mode:%d,hue:%d,sat:%d,spd:%d},"
                 "sys:{log:%d,deb_samp:%d,deb_thr:%d},"
                 "cal:{lx_c:%d,ly_c:%d,rx_c:%d,ry_c:%d}}",
                 0, 0, 0, 0,
                 128, 128, 128, 128, 0, 0,
                 0, 0,
                 1,
                 50, 1, 0, 255, 50,
                 1, 3, 5,
                 0, 0, 0, 0);
      uint16_t jsonLen = static_cast<uint16_t>(doc.end());
      if (jsonLen > 0 && jsonLen < PROFILE_JSON_MAX) {
        writeFactoryProfile(reinterpret_cast<const char *>(s_staging), jsonLen);
        // Re-scan after writing factory Profile0 — init is complete
        scanBootMeta(&activeId, &activeAddr);
        scanAddressRing();
        _activeProfileId = activeId;
        _activeAddress = activeAddr;
        _nextAddr = findNextAddr();
        _profileCount = 0;
        for (uint16_t i = 0; i <= PROFILE_MAX_ID; ++i) {
          if (_profileAddresses[i] != 0)
            _profileCount++;
        }
        LOG_INFO("ProfileStore: auto-init done, active=%u, addr=0x%06lX, "
                 "next=0x%06lX, count=%u",
                 _activeProfileId, _activeAddress, _nextAddr, _profileCount);
        return true;
      }
    }
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

bool ProfileStore::scanBootMeta(uint16_t *outActiveId, uint32_t *outAddress) {
  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

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
  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

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
  auto &flash = Drivers::Device::FlashW25qxx::getInstance();
  const auto &info = flash.getInfo();
  uint32_t flashSize = info.sizeBytes;

  uint32_t maxAddr = USER_RING_BASE;
  for (uint16_t i = 0; i <= PROFILE_MAX_ID; ++i) {
    if (_profileAddresses[i] != 0 && _profileAddresses[i] > maxAddr) {
      maxAddr = _profileAddresses[i];
    }
  }

  if (maxAddr < USER_RING_BASE) {
    return USER_RING_BASE;
  }

  uint32_t next = (maxAddr & ~0xFFF) + 0x1000;

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

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

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

  _activeProfileId = 0;
  _activeAddress = PROFILE0_ADDR;
  _bootMetaSeq = 1;
  _profileAddresses[0] = PROFILE0_ADDR;
  _profileSeqs[0] = 0;
  _profileCount = 1;

  LOG_INFO("ProfileStore: factory profile written");

  // Also write Address Ring entry for profile0 so scanAddressRing finds it
  AddressEntry addrEntry;
  addrEntry.profileId = 0;
  addrEntry.address = PROFILE0_ADDR;
  addrEntry.seq = 1;
  _addressRingSeq = 1;
  if (!flash.write(ADDR_RING_BASE,
                   reinterpret_cast<const uint8_t *>(&addrEntry),
                   sizeof(AddressEntry))) {
    LOG_WARN("ProfileStore: failed to write Address entry for profile0");
  }

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

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();
  const auto &info = flash.getInfo();

  if (_nextAddr >= info.sizeBytes) {
    LOG_WARN("ProfileStore: User Ring full, compaction needed");
    if (!compaction()) {
      LOG_ERROR("ProfileStore: compaction failed");
      return false;
    }
  }

  uint32_t dataAddr = _nextAddr;
  if (!flash.write(dataAddr, reinterpret_cast<const uint8_t *>(json), len)) {
    LOG_ERROR("ProfileStore: failed to write profile data");
    return false;
  }

  _nextAddr = ((dataAddr & ~0xFFF) + 0x1000);

  seqBeforeIncrement();
  _addressRingSeq++;
  AddressEntry addrEntry;
  addrEntry.profileId = id;
  addrEntry.address = dataAddr;
  addrEntry.seq = _addressRingSeq;

  uint32_t addrSlot =
      ADDR_RING_BASE + (ensureAddressRingSlot() * sizeof(AddressEntry));
  if (!flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&addrEntry),
                   sizeof(AddressEntry))) {
    LOG_ERROR("ProfileStore: failed to write Address entry");
    return false;
  }

  seqBeforeIncrement();
  _bootMetaSeq++;
  BootMeta bootMeta;
  bootMeta.magic = BOOTMETA_MAGIC;
  bootMeta.seq = _bootMetaSeq;
  bootMeta.profileId = id;
  bootMeta.address = dataAddr;
  bootMeta.reserved = 0xFFFFFFFF;
  bootMeta.crc16 = 0;
  bootMeta.crc16 = crc16BootMeta(&bootMeta);

  uint32_t bootMetaSlot =
      BOOTMETA_BASE + (ensureBootMetaSlot() * sizeof(BootMeta));
  if (!flash.write(bootMetaSlot, reinterpret_cast<const uint8_t *>(&bootMeta),
                   sizeof(BootMeta))) {
    LOG_ERROR("ProfileStore: failed to write BootMeta");
    return false;
  }

  _profileAddresses[id] = dataAddr;
  _profileSeqs[id] = _addressRingSeq;
  _profileCount++;
  _activeProfileId = id;
  _activeAddress = dataAddr;

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

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();
  const auto &info = flash.getInfo();

  if (_nextAddr >= info.sizeBytes) {
    LOG_WARN("ProfileStore: User Ring full, compaction needed");
    if (!compaction()) {
      LOG_ERROR("ProfileStore: compaction failed");
      return false;
    }
  }

  uint32_t dataAddr = _nextAddr;
  if (!flash.write(dataAddr, reinterpret_cast<const uint8_t *>(json), len)) {
    LOG_ERROR("ProfileStore: failed to write modified profile");
    return false;
  }

  _nextAddr = ((dataAddr & ~0xFFF) + 0x1000);

  seqBeforeIncrement();
  _addressRingSeq++;
  AddressEntry addrEntry;
  addrEntry.profileId = id;
  addrEntry.address = dataAddr;
  addrEntry.seq = _addressRingSeq;

  uint32_t addrSlot =
      ADDR_RING_BASE + (ensureAddressRingSlot() * sizeof(AddressEntry));
  if (!flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&addrEntry),
                   sizeof(AddressEntry))) {
    LOG_ERROR("ProfileStore: failed to write Address entry for modify");
    return false;
  }

  _profileAddresses[id] = dataAddr;
  _profileSeqs[id] = _addressRingSeq;

  if (_activeProfileId == id) {
    _activeAddress = dataAddr;
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

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

  // Mark deleted: write Address Ring entry with address=0, seq=high
  seqBeforeIncrement();
  _addressRingSeq++;
  AddressEntry addrEntry;
  addrEntry.profileId = id;
  addrEntry.address = 0;
  addrEntry.seq = _addressRingSeq;

  uint32_t addrSlot =
      ADDR_RING_BASE + (ensureAddressRingSlot() * sizeof(AddressEntry));
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

    // Write BootMeta entry for fallback
    seqBeforeIncrement();
    _bootMetaSeq++;
    BootMeta bootMeta;
    bootMeta.magic = BOOTMETA_MAGIC;
    bootMeta.seq = _bootMetaSeq;
    bootMeta.profileId = 0;
    bootMeta.address = _activeAddress;
    bootMeta.reserved = 0xFFFFFFFF;
    bootMeta.crc16 = 0;
    bootMeta.crc16 = crc16BootMeta(&bootMeta);

    uint32_t bootMetaSlot =
        BOOTMETA_BASE + (ensureBootMetaSlot() * sizeof(BootMeta));
    if (!flash.write(bootMetaSlot, reinterpret_cast<const uint8_t *>(&bootMeta),
                     sizeof(BootMeta))) {
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

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

  uint32_t address = (id == 0) ? PROFILE0_ADDR : _profileAddresses[id];

  // Write BootMeta entry
  seqBeforeIncrement();
  _bootMetaSeq++;
  BootMeta bootMeta;
  bootMeta.magic = BOOTMETA_MAGIC;
  bootMeta.seq = _bootMetaSeq;
  bootMeta.profileId = id;
  bootMeta.address = address;
  bootMeta.reserved = 0xFFFFFFFF;
  bootMeta.crc16 = 0;
  bootMeta.crc16 = crc16BootMeta(&bootMeta);

  uint32_t bootMetaSlot =
      BOOTMETA_BASE + (ensureBootMetaSlot() * sizeof(BootMeta));
  if (!flash.write(bootMetaSlot, reinterpret_cast<const uint8_t *>(&bootMeta),
                   sizeof(BootMeta))) {
    LOG_ERROR("ProfileStore: failed to write BootMeta for select");
    return false;
  }

  _activeProfileId = id;
  _activeAddress = address;

  LOG_INFO("ProfileStore: selected profile %u at 0x%06lX", id, address);
  return true;
}

// ── loadActive() ──
// Reads raw JSON into buf. Caller (ConfigManager) is responsible for parsing.

bool ProfileStore::loadActive(uint8_t *buf, uint16_t *outLen) {
  LOG_DEBUG("ProfileStore: loadActive");

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

  if (!buf) {
    buf = s_staging;
  }

  if (!flash.read(_activeAddress, buf, PROFILE_JSON_MAX)) {
    LOG_ERROR("ProfileStore: failed to read active profile at 0x%06lX",
              _activeAddress);
    return false;
  }

  uint16_t actualLen = 0;
  for (uint16_t i = 0; i < PROFILE_JSON_MAX; ++i) {
    if (buf[i] == 0 || buf[i] == 0xFF) {
      break;
    }
    actualLen = i + 1;
  }

  // No parse here — caller (ConfigManager) drives the parse

  if (outLen) {
    *outLen = actualLen;
  }

  LOG_DEBUG("ProfileStore: loaded active profile, %u bytes", actualLen);
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

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();
  const auto &info = flash.getInfo();

  status.totalSectors = info.sizeBytes / 4096;
  status.usedSectors = (_nextAddr - USER_RING_BASE) / 4096;
  status.freeSectors = status.totalSectors - 2 - status.usedSectors;

  return status;
}

// ── compaction() ──
// Moves all valid profiles to the head of the User Ring, resets Address Ring
// and BootMeta. Needed when the User Ring runs out of space.

bool ProfileStore::compaction() {
  LOG_INFO("ProfileStore: compaction start");

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

  // 1. Collect valid profiles (address != 0), ordered by profileId
  struct ValidProfile {
    uint16_t id;
    uint32_t oldAddr;
  };
  ValidProfile valid[16];
  uint8_t validCount = 0;

  for (uint16_t i = 0; i <= PROFILE_MAX_ID; ++i) {
    if (_profileAddresses[i] != 0) {
      valid[validCount].id = i;
      valid[validCount].oldAddr = _profileAddresses[i];
      validCount++;
    }
  }

  if (validCount == 0) {
    LOG_INFO("ProfileStore: no valid profiles to compact");
    return true;
  }

  // 2. Rewrite each valid profile at User Ring head (S2 onward)
  uint32_t writeAddr = USER_RING_BASE;
  _addressRingSeq = 0;

  for (uint8_t i = 0; i < validCount; ++i) {
    memset(s_staging, 0xFF, PROFILE_JSON_MAX);
    if (!flash.read(valid[i].oldAddr, s_staging, PROFILE_JSON_MAX)) {
      LOG_ERROR("ProfileStore: compaction read fail at 0x%06lX",
                valid[i].oldAddr);
      return false;
    }

    if (!flash.write(writeAddr, s_staging, PROFILE_JSON_MAX)) {
      LOG_ERROR("ProfileStore: compaction write fail at 0x%06lX", writeAddr);
      return false;
    }

    seqBeforeIncrement();
    _addressRingSeq++;
    AddressEntry addrEntry;
    addrEntry.profileId = valid[i].id;
    addrEntry.address = writeAddr;
    addrEntry.seq = _addressRingSeq;

    uint32_t addrSlot = ADDR_RING_BASE + i * sizeof(AddressEntry);
    if (!flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&addrEntry),
                     sizeof(AddressEntry))) {
      LOG_ERROR("ProfileStore: compaction Address write fail");
      return false;
    }

    _profileAddresses[valid[i].id] = writeAddr;
    _profileSeqs[valid[i].id] = _addressRingSeq;

    writeAddr += 0x1000;
  }

  // 3. Reset BootMeta for the active profile
  _bootMetaSeq = 0;
  seqBeforeIncrement();
  _bootMetaSeq++;
  BootMeta bootMeta;
  bootMeta.magic = BOOTMETA_MAGIC;
  bootMeta.seq = _bootMetaSeq;
  bootMeta.profileId = _activeProfileId;
  bootMeta.address = _profileAddresses[_activeProfileId];
  bootMeta.reserved = 0xFFFFFFFF;
  bootMeta.crc16 = 0;
  bootMeta.crc16 = crc16BootMeta(&bootMeta);

  if (!eraseSector0Range(BOOTMETA_BASE, BOOTMETA_SIZE)) {
    LOG_ERROR("ProfileStore: compaction BootMeta erase fail");
    return false;
  }

  if (!flash.write(BOOTMETA_BASE, reinterpret_cast<const uint8_t *>(&bootMeta),
                   sizeof(BootMeta))) {
    LOG_ERROR("ProfileStore: compaction BootMeta write fail");
    return false;
  }

  // 4. Clear unused Address Ring slots
  for (uint8_t i = validCount; i < ADDR_RING_SLOTS; ++i) {
    AddressEntry empty;
    empty.profileId = PROFILE_ID_NONE;
    empty.address = 0;
    empty.seq = 0;
    uint32_t addrSlot = ADDR_RING_BASE + i * sizeof(AddressEntry);
    (void)flash.write(addrSlot, reinterpret_cast<const uint8_t *>(&empty),
                      sizeof(AddressEntry));
  }

  _nextAddr = writeAddr;

  LOG_INFO("ProfileStore: compaction done, nextAddr=0x%06lX", _nextAddr);
  return true;
}

// ── resetSector0() ──

bool ProfileStore::resetSector0() {
  LOG_INFO("ProfileStore: resetSector0");

  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

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
      validEntries[entryCount].seq = _profileSeqs[i];
      entryCount++;
    }
  }

  // Erase Sector 0
  if (!flash.eraseSector(0x000000)) {
    LOG_ERROR("ProfileStore: Sector 0 erase failed");
    return false;
  }

  // Restore Profile0 from Sector 1 backup
  memset(s_staging, 0, PROFILE_JSON_MAX);
  if (!flash.read(PROFILE0_BACKUP, s_staging, PROFILE_JSON_MAX)) {
    LOG_WARN("ProfileStore: failed to read Profile0 backup, writing minimal "
             "default");
    const char *emptyProfile = "{\"ver\":1}";
    (void)flash.write(PROFILE0_ADDR,
                      reinterpret_cast<const uint8_t *>(emptyProfile),
                      strlen(emptyProfile));
  } else {
    (void)flash.write(PROFILE0_ADDR, s_staging, PROFILE_JSON_MAX);
  }

  // Write BootMeta slot 0
  if (!flash.write(BOOTMETA_BASE,
                   reinterpret_cast<const uint8_t *>(&activeBootMeta),
                   sizeof(BootMeta))) {
    LOG_ERROR("ProfileStore: failed to write BootMeta after Sector 0 reset");
    return false;
  }

  // Write valid Address entries (best-effort)
  for (uint8_t i = 0; i < entryCount; ++i) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);
    if (!flash.write(slotAddr,
                     reinterpret_cast<const uint8_t *>(&validEntries[i]),
                     sizeof(AddressEntry))) {
      LOG_WARN("ProfileStore: Address entry restore failed at slot %u", i);
    }
  }

  LOG_INFO("ProfileStore: Sector 0 reset done");

  // Update cached seq counters to match what was just written to flash
  _bootMetaSeq = 1;
  // Address Ring seq: use the max of restored entries, or 0 if none
  _addressRingSeq = 0;
  for (uint8_t i = 0; i < entryCount; ++i) {
    if (validEntries[i].seq > _addressRingSeq) {
      _addressRingSeq = validEntries[i].seq;
    }
  }

  return true;
}

// ── erase a region within Sector 0 ──

bool ProfileStore::eraseSector0Range(uint32_t addr, uint32_t len) {
  (void)len;
  auto &flash = Drivers::Device::FlashW25qxx::getInstance();

  if (addr < 0x1000) {
    return flash.eraseSector(0x000000);
  }

  LOG_ERROR("ProfileStore: eraseSector0Range out of range: 0x%06lX", addr);
  return false;
}

} // namespace ThetaGP::Gamepad::Profile
