/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file flash_wear_level.cpp
 * @brief FlashBase::WearLevel implementation — full-flash ring buffer
 *
 * Layout:
 *   Sector 0:       Meta (written once at first boot, never erased)
 *   Sectors 1..N:   Log sectors (dynamic count = ceil(dataSectors / 512))
 *   Sectors N+1..M: Data sectors (full flash ring buffer)
 *
 * Log entries track write order. Each write produces one log entry.
 * When the log fills all log sectors, all log sectors are erased and
 * restart from entry 0 (ring reset matches data ring wrap).
 */

#include "drivers/device/flash/flash_base.h"

#include "utils/log/log.h"

#include <cstring>

namespace ThetaGP::Drivers::Device {

using WL = FlashBase::WearLevel;

// ── DMA_BSS Slot Buffer ──

DMA_BSS static WL::SlotInfo s_slotBuffer[WL::SLOTS_MAX];

// ── CRC32 (software, ISO 3309 / MPEG-2 polynomial 0x04C11DB7) ──

static constexpr uint32_t CRC32_POLY = 0x04C11DB7;

static uint32_t crc32Table[256] = {0};
static bool crc32TableInitialized = false;

static void initCrc32Table() {
  if (crc32TableInitialized) return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t crc = i << 24;
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x80000000) ? ((crc << 1) ^ CRC32_POLY) : (crc << 1);
    }
    crc32Table[i] = crc;
  }
  crc32TableInitialized = true;
}

uint32_t WL::computeCrc(const uint8_t *data, uint16_t len) {
  initCrc32Table();
  uint32_t crc = 0xFFFFFFFF;
  for (uint16_t i = 0; i < len; i++) {
    uint8_t idx = static_cast<uint8_t>((crc >> 24) ^ data[i]);
    crc = (crc << 8) ^ crc32Table[idx];
  }
  return ~crc;
}

// ── Key Hash (CRC16 of key name) ──

uint32_t WL::keyHash(const char *key) {
  if (!key) return 0;
  uint32_t hash = 0xFFFF;
  size_t len = strlen(key);
  if (len > MAX_KEY_LEN) len = MAX_KEY_LEN;
  for (size_t i = 0; i < len; i++) {
    hash ^= static_cast<uint8_t>(key[i]) << 8;
    for (int j = 0; j < 8; j++) {
      hash = (hash & 0x8000) ? ((hash << 1) ^ 0x8005) : (hash << 1);
    }
  }
  return hash & 0xFFFF;
}

// ── nextSectorAddr ──

uint32_t WL::nextSectorAddr(uint32_t currentAddr) const {
  uint32_t next = currentAddr + _flash.getInfo().sectorSize;
  uint32_t dataEnd = _dataBaseAddr + (_dataSectorCount * _flash.getInfo().sectorSize);
  if (next >= dataEnd) {
    next = _dataBaseAddr;
  }
  return next;
}

// ── markSectorStale ──

void WL::markSectorStale(uint32_t sectorAddr) {
  uint8_t staleVal = STALE;
  _flash.write(sectorAddr + 2, &staleVal, 1);
}

// ── appendLogEntry ──

void WL::appendLogEntry(uint32_t dataAddr, uint32_t dataSize) {
  if (_logSectorCount == 0) return;

  uint32_t maxEntries = _logSectorCount * _flash.getInfo().sectorSize / ENTRY_SIZE;

  if (_entryCount >= maxEntries) {
    LOG_INFO("WL: log full (%lu entries), erasing all log sectors", _entryCount);
    for (uint32_t s = 0; s < _logSectorCount; s++) {
      uint32_t sectorAddr = _logBaseAddr + (s * _flash.getInfo().sectorSize);
      _flash.eraseSector(sectorAddr);
    }
    _entryCount = 0;
  }

  uint32_t logSectorIndex = _entryCount / (_flash.getInfo().sectorSize / ENTRY_SIZE);
  uint32_t entryOffset = (_entryCount % (_flash.getInfo().sectorSize / ENTRY_SIZE)) * ENTRY_SIZE;
  uint32_t logAddr = _logBaseAddr + (logSectorIndex * _flash.getInfo().sectorSize) + entryOffset;

  LogEntry entry;
  entry.addr = dataAddr;
  entry.size = dataSize;
  _flash.write(logAddr, reinterpret_cast<const uint8_t *>(&entry), sizeof(LogEntry));

  _entryCount++;
}

// ── scanLog ──

void WL::scanLog() {
  _entryCount = 0;
  _headAddr = _dataBaseAddr;

  LogEntry lastEntry = {0, 0};

  for (uint32_t s = 0; s < _logSectorCount; s++) {
    uint32_t sectorAddr = _logBaseAddr + (s * _flash.getInfo().sectorSize);
    for (uint32_t e = 0; e < _flash.getInfo().sectorSize / ENTRY_SIZE; e++) {
      uint32_t entryAddr = sectorAddr + (e * ENTRY_SIZE);
      LogEntry entry;
      _flash.read(entryAddr, reinterpret_cast<uint8_t *>(&entry), sizeof(LogEntry));

      if (entry.addr == 0xFFFFFFFF || entry.addr == 0) {
        break;
      }

      _entryCount++;
      lastEntry = entry;
    }
  }

  if (_entryCount > 0 && lastEntry.addr != 0) {
    _headAddr = lastEntry.addr;
    LOG_INFO("WL: log scan: %lu entries, head=0x%08lX", _entryCount, _headAddr);
  } else {
    _headAddr = _dataBaseAddr;
    LOG_INFO("WL: log empty, head=0x%08lX", _headAddr);
  }
}

// ── scanDataSectors ──

void WL::scanDataSectors() {
  _slotCount = 0;
  for (uint32_t i = 0; i < SLOTS_MAX; i++) {
    _slots[i] = SlotInfo();
  }

  for (uint32_t i = 0; i < _dataSectorCount && _slotCount < SLOTS_MAX; i++) {
    uint32_t addr = _dataBaseAddr + (i * _flash.getInfo().sectorSize);

    SlotHeader hdr;
    if (!_flash.read(addr, reinterpret_cast<uint8_t *>(&hdr), sizeof(SlotHeader))) {
      continue;
    }

    if (hdr.magic != MAGIC) continue;
    if (hdr.valid != VALID) continue;
    if (hdr.dataLen > PAYLOAD_MAX) continue;

    uint8_t crcBuf[11 + PAYLOAD_MAX];
    std::memcpy(crcBuf, &hdr.magic, 2);
    crcBuf[2] = hdr.reserved;
    std::memcpy(crcBuf + 3, &hdr.keyHash, 4);
    std::memcpy(crcBuf + 7, &hdr.dataLen, 2);
    std::memcpy(crcBuf + 9, &hdr.wearCount, 2);
    if (hdr.dataLen > 0) {
      if (!_flash.read(addr + sizeof(SlotHeader), crcBuf + 11, hdr.dataLen)) {
        continue;
      }
    }
    uint32_t computed = computeCrc(crcBuf, 11 + hdr.dataLen);

    if (computed != hdr.crc32) {
      continue;
    }

    _slots[_slotCount].physAddr = addr;
    _slots[_slotCount].header = hdr;
    _slots[_slotCount].valid = true;
    _slotCount++;
  }

  LOG_INFO("WL: data scan: %lu valid entries cached", _slotCount);
}

// ── init ──

Result WL::init() {
  if (_initialized) return Result::Ok;

  _slots = s_slotBuffer;

  const FlashInfo &info = _flash.getInfo();
  _flashSize = info.sizeBytes;
  if (_flashSize == 0) {
    LOG_ERROR("WL: flash not initialized");
    return Result::NotReady;
  }
  if (_flashSize % _flash.getInfo().sectorSize != 0) {
    LOG_WARN("WL: flash size %lu not multiple of sector size %lu",
             _flashSize, _flash.getInfo().sectorSize);
    _flashSize = (_flashSize / _flash.getInfo().sectorSize) * _flash.getInfo().sectorSize;
  }

  uint32_t totalSectors = _flashSize / _flash.getInfo().sectorSize;
  if (totalSectors < 3) {
    LOG_ERROR("WL: flash too small (%lu sectors, need >= 3)", totalSectors);
    return Result::Error;
  }

  _metaBaseAddr = 0;
  _logBaseAddr = _flash.getInfo().sectorSize;

  uint32_t dataSectors = totalSectors - 1;
  _logSectorCount = (dataSectors + _flash.getInfo().sectorSize / ENTRY_SIZE - 1) / (_flash.getInfo().sectorSize / ENTRY_SIZE);
  for (int iter = 0; iter < 4; iter++) {
    dataSectors = totalSectors - 1 - _logSectorCount;
    uint32_t newLogSectors = (dataSectors + _flash.getInfo().sectorSize / ENTRY_SIZE - 1) / (_flash.getInfo().sectorSize / ENTRY_SIZE);
    if (newLogSectors == _logSectorCount) break;
    _logSectorCount = newLogSectors;
  }
  dataSectors = totalSectors - 1 - _logSectorCount;
  _dataSectorCount = dataSectors;
  _dataBaseAddr = _logBaseAddr + (_logSectorCount * _flash.getInfo().sectorSize);

  LOG_INFO("WL: flash=%lu sectors, meta=0x%08lX, logBase=0x%08lX, logCnt=%lu, "
           "dataBase=0x%08lX, dataCnt=%lu",
           totalSectors, _metaBaseAddr, _logBaseAddr, _logSectorCount,
           _dataBaseAddr, _dataSectorCount);

  // ── Meta Sector ──
  MetaSector meta;
  std::memset(&meta, 0, sizeof(meta));
  _flash.read(_metaBaseAddr, reinterpret_cast<uint8_t *>(&meta), sizeof(MetaSector));

  if (meta.magic == META_MAGIC && meta.valid == VALID) {
    _metaWritten = true;
    LOG_INFO("WL: meta sector valid, version=%u", meta.version);
  } else {
    uint8_t erasedCheck[16];
    _flash.read(_metaBaseAddr, erasedCheck, 16);
    bool isErased = true;
    for (int i = 0; i < 16; i++) {
      if (erasedCheck[i] != 0xFF) {
        isErased = false;
        break;
      }
    }

    if (isErased) {
      MetaSector newMeta;
      std::memset(&newMeta, 0, sizeof(newMeta));
      newMeta.magic = META_MAGIC;
      newMeta.valid = VALID;
      newMeta.version = 1;
      uint8_t metaCrcBuf[1020];
      std::memcpy(metaCrcBuf, &newMeta.magic, 2);
      std::memcpy(metaCrcBuf + 2, &newMeta.valid, 1);
      std::memcpy(metaCrcBuf + 3, &newMeta.version, 1);
      std::memcpy(metaCrcBuf + 4, newMeta.reserved, 8);
      std::memcpy(metaCrcBuf + 12, newMeta.firmwareMeta, 1008);
      newMeta.crc32 = computeCrc(metaCrcBuf, 1020);

      _flash.write(_metaBaseAddr, reinterpret_cast<const uint8_t *>(&newMeta), sizeof(MetaSector));
      _metaWritten = true;
      LOG_INFO("WL: meta written (first boot on erased flash)");
    } else {
      LOG_WARN("WL: meta sector invalid (magic=0x%04X), erasing and rewriting", meta.magic);
      _flash.eraseSector(_metaBaseAddr);
      MetaSector newMeta;
      std::memset(&newMeta, 0, sizeof(newMeta));
      newMeta.magic = META_MAGIC;
      newMeta.valid = VALID;
      newMeta.version = 1;
      uint8_t metaCrcBuf[1020];
      std::memcpy(metaCrcBuf, &newMeta.magic, 2);
      std::memcpy(metaCrcBuf + 2, &newMeta.valid, 1);
      std::memcpy(metaCrcBuf + 3, &newMeta.version, 1);
      std::memcpy(metaCrcBuf + 4, newMeta.reserved, 8);
      std::memcpy(metaCrcBuf + 12, newMeta.firmwareMeta, 1008);
      newMeta.crc32 = computeCrc(metaCrcBuf, 1020);
      _flash.write(_metaBaseAddr, reinterpret_cast<const uint8_t *>(&newMeta), sizeof(MetaSector));
      _metaWritten = true;
      LOG_INFO("WL: meta erased and rewritten");
    }
  }

  scanLog();
  scanDataSectors();

  _totalWrites = 0;
  for (uint32_t i = 0; i < _slotCount; i++) {
    _totalWrites += _slots[i].header.wearCount;
  }

  _initialized = true;

  LOG_INFO("WL: init complete: %lu cached slots, %lu total writes, head=0x%08lX",
           _slotCount, _totalWrites, _headAddr);

  return Result::Ok;
}

// ── findSlot ──

int32_t WL::findSlot(const char *key) {
  if (!key || !_initialized) return -1;
  uint32_t searchHash = keyHash(key);

  for (uint32_t i = 0; i < _slotCount; i++) {
    if (_slots[i].valid &&
        _slots[i].header.keyHash == searchHash) {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

// ── storeConfig ──

Result WL::storeConfig(const char *key, const uint8_t *data, uint16_t len) {
  if (!key || !data) return Result::InvalidParam;
  if (!_initialized) return Result::NotReady;
  if (len > PAYLOAD_MAX) return Result::InvalidParam;

  uint32_t hash = keyHash(key);

  int32_t existingIdx = findSlot(key);
  uint16_t prevWearCount = 0;
  if (existingIdx >= 0) {
    SlotInfo &existing = _slots[existingIdx];
    if (existing.header.dataLen == len) {
      uint8_t existingData[PAYLOAD_MAX];
      if (_flash.read(existing.physAddr + sizeof(SlotHeader),
                      existingData, len)) {
        if (std::memcmp(existingData, data, len) == 0) {
          return Result::Ok;
        }
      }
    }
    prevWearCount = existing.header.wearCount;
    markSectorStale(existing.physAddr);
    existing.valid = false;
    std::memset(&existing.header, 0, sizeof(existing.header));
  }

  uint32_t nextAddr;
  if (_entryCount == 0 && _slotCount == 0) {
    nextAddr = _dataBaseAddr;
  } else {
    nextAddr = nextSectorAddr(_headAddr);
  }

  if (!_flash.eraseSector(nextAddr)) {
    LOG_ERROR("WL: sector erase at 0x%08lX failed", nextAddr);
    return Result::Error;
  }

  if (len > 0) {
    if (!_flash.write(nextAddr + sizeof(SlotHeader), data, len)) {
      LOG_ERROR("WL: payload write at 0x%08lX failed", nextAddr);
      return Result::Error;
    }
  }

  SlotHeader newHeader;
  newHeader.magic = MAGIC;
  newHeader.valid = VALID;
  newHeader.reserved = 0;
  newHeader.keyHash = hash;
  newHeader.dataLen = len;
  newHeader.wearCount = (existingIdx >= 0)
    ? prevWearCount + 1
    : 1;
  newHeader.crc32 = 0;

  uint8_t crcBuf[11 + PAYLOAD_MAX];
  std::memcpy(crcBuf, &newHeader.magic, 2);
  crcBuf[2] = newHeader.reserved;
  std::memcpy(crcBuf + 3, &newHeader.keyHash, 4);
  std::memcpy(crcBuf + 7, &newHeader.dataLen, 2);
  std::memcpy(crcBuf + 9, &newHeader.wearCount, 2);
  if (len > 0) {
    std::memcpy(crcBuf + 11, data, len);
  }
  newHeader.crc32 = computeCrc(crcBuf, 11 + len);

  if (!_flash.write(nextAddr, reinterpret_cast<const uint8_t *>(&newHeader), sizeof(SlotHeader))) {
    LOG_ERROR("WL: header write at 0x%08lX failed", nextAddr);
    return Result::Error;
  }

  appendLogEntry(nextAddr, len);
  _headAddr = nextAddr;

  if (existingIdx >= 0) {
    _slots[existingIdx].physAddr = nextAddr;
    _slots[existingIdx].header = newHeader;
    _slots[existingIdx].valid = true;
  } else {
    if (_slotCount < SLOTS_MAX) {
      _slots[_slotCount].physAddr = nextAddr;
      _slots[_slotCount].header = newHeader;
      _slots[_slotCount].valid = true;
      _slotCount++;
    } else {
      uint32_t freeIdx = SLOTS_MAX;
      for (uint32_t i = 0; i < SLOTS_MAX; i++) {
        if (!_slots[i].valid) {
          freeIdx = i;
          break;
        }
      }

      if (freeIdx < SLOTS_MAX) {
        _slots[freeIdx].physAddr = nextAddr;
        _slots[freeIdx].header = newHeader;
        _slots[freeIdx].valid = true;
      } else {
        uint32_t evictIdx = 0;
        uint16_t lowestWear = 0xFFFF;
        for (uint32_t i = 0; i < SLOTS_MAX; i++) {
          if (_slots[i].valid && _slots[i].header.wearCount < lowestWear) {
            lowestWear = _slots[i].header.wearCount;
            evictIdx = i;
          }
        }
        _slots[evictIdx].physAddr = nextAddr;
        _slots[evictIdx].header = newHeader;
        _slots[evictIdx].valid = true;
      }
    }
  }

  return Result::Ok;
}

// ── loadConfig ──

Result WL::loadConfig(const char *key, uint8_t *data,
                      uint16_t maxLen, uint16_t *outLen) {
  if (!key || !data) return Result::InvalidParam;
  if (!_initialized) return Result::NotReady;

  int32_t idx = findSlot(key);
  if (idx < 0) {
    return Result::NotReady;
  }

  SlotInfo &slot = _slots[idx];

  uint8_t validCheck;
  _flash.read(slot.physAddr + 2, &validCheck, 1);
  if (validCheck != VALID) {
    slot.valid = false;
    return Result::NotReady;
  }

  uint16_t dataLen = slot.header.dataLen;
  if (outLen) *outLen = dataLen;

  if (dataLen == 0) {
    return Result::Ok;
  }

  uint16_t readLen = (maxLen < dataLen) ? maxLen : dataLen;
  if (!_flash.read(slot.physAddr + sizeof(SlotHeader), data, readLen)) {
    LOG_ERROR("WL: read from 0x%08lX failed", slot.physAddr);
    return Result::Error;
  }

  uint8_t crcBuf[11 + PAYLOAD_MAX];
  std::memcpy(crcBuf, &slot.header.magic, 2);
  crcBuf[2] = slot.header.reserved;
  std::memcpy(crcBuf + 3, &slot.header.keyHash, 4);
  std::memcpy(crcBuf + 7, &slot.header.dataLen, 2);
  std::memcpy(crcBuf + 9, &slot.header.wearCount, 2);
  if (dataLen > 0) {
    if (!_flash.read(slot.physAddr + sizeof(SlotHeader), crcBuf + 11, dataLen)) {
      LOG_ERROR("WL: CRC read from 0x%08lX failed", slot.physAddr);
      return Result::Error;
    }
  }
  uint32_t computed = computeCrc(crcBuf, 11 + dataLen);
  if (computed != slot.header.crc32) {
    LOG_WARN("WL: CRC mismatch on load for key='%s'", key);
    return Result::Error;
  }

  return Result::Ok;
}

// ── eraseConfig ──

Result WL::eraseConfig(const char *key) {
  if (!key) return Result::InvalidParam;
  if (!_initialized) return Result::NotReady;

  int32_t idx = findSlot(key);
  if (idx < 0) {
    return Result::Ok;
  }

  markSectorStale(_slots[idx].physAddr);

  _slots[idx].valid = false;
  std::memset(&_slots[idx].header, 0, sizeof(_slots[idx].header));

  LOG_INFO("WL: erased key='%s' at 0x%08lX", key, _slots[idx].physAddr);
  return Result::Ok;
}

// ── getStatus ──

WL::Status WL::getStatus() const {
  Status status;
  status.totalSlots = _dataSectorCount;
  status.totalWrites = _totalWrites;
  status.dataBaseAddr = _dataBaseAddr;
  status.dataSectorCount = _dataSectorCount;
  status.logSectorCount = _logSectorCount;
  status.headAddr = _headAddr;
  status.flashSize = _flashSize;
  status.metaWritten = _metaWritten;

  uint16_t minWear = 0xFFFF;
  uint16_t maxWear = 0;
  uint32_t wearSum = 0;
  uint32_t wearCount = 0;

  for (uint32_t i = 0; i < _slotCount; i++) {
    if (_slots[i].valid) {
      status.usedSlots++;
      uint16_t wc = _slots[i].header.wearCount;
      if (wc < minWear) minWear = wc;
      if (wc > maxWear) maxWear = wc;
      wearSum += wc;
      wearCount++;
    }
  }

  status.staleSlots = 0;

  status.minWear = (minWear != 0xFFFF) ? minWear : 0;
  status.maxWear = maxWear;
  status.avgWear = (wearCount > 0) ? static_cast<uint16_t>(wearSum / wearCount) : 0;

  return status;
}

// ── readMeta ──

Result WL::readMeta(MetaSector &meta) const {
  if (!_initialized) return Result::NotReady;
  std::memset(&meta, 0, sizeof(meta));
  _flash.read(_metaBaseAddr, reinterpret_cast<uint8_t *>(&meta), sizeof(MetaSector));
  return Result::Ok;
}

// ── writeMeta ──

Result WL::writeMeta(const MetaSector &meta) {
  if (!_initialized) return Result::NotReady;

  uint8_t checkBuf[16];
  _flash.read(_metaBaseAddr, checkBuf, 16);
  for (int i = 0; i < 16; i++) {
    if (checkBuf[i] != 0xFF) {
      LOG_ERROR("WL: meta sector not erased, cannot write");
      return Result::Error;
    }
  }

  uint8_t metaCrcBuf[1020];
  std::memcpy(metaCrcBuf, &meta.magic, 2);
  std::memcpy(metaCrcBuf + 2, &meta.valid, 1);
  std::memcpy(metaCrcBuf + 3, &meta.version, 1);
  std::memcpy(metaCrcBuf + 4, meta.reserved, 8);
  std::memcpy(metaCrcBuf + 12, meta.firmwareMeta, 1008);

  MetaSector newMeta = meta;
  newMeta.crc32 = computeCrc(metaCrcBuf, 1020);

  _flash.write(_metaBaseAddr, reinterpret_cast<const uint8_t *>(&newMeta), sizeof(MetaSector));
  _metaWritten = true;

  LOG_INFO("WL: meta written (test)");
  return Result::Ok;
}

// ── fullScan ──

Result WL::fullScan() {
  if (!_initialized) {
    const FlashInfo &info = _flash.getInfo();
    _flashSize = info.sizeBytes;
    if (_flashSize == 0) return Result::NotReady;
    _metaBaseAddr = 0;
    _logBaseAddr = _flash.getInfo().sectorSize;
    uint32_t totalSectors = _flashSize / _flash.getInfo().sectorSize;
    if (totalSectors < 3) return Result::Error;
    uint32_t dataSectors = totalSectors - 1;
    _logSectorCount = (dataSectors + _flash.getInfo().sectorSize / ENTRY_SIZE - 1) / (_flash.getInfo().sectorSize / ENTRY_SIZE);
    for (int iter = 0; iter < 4; iter++) {
      dataSectors = totalSectors - 1 - _logSectorCount;
      uint32_t newLogSectors = (dataSectors + _flash.getInfo().sectorSize / ENTRY_SIZE - 1) / (_flash.getInfo().sectorSize / ENTRY_SIZE);
      if (newLogSectors == _logSectorCount) break;
      _logSectorCount = newLogSectors;
    }
    _dataSectorCount = totalSectors - 1 - _logSectorCount;
    _dataBaseAddr = _logBaseAddr + (_logSectorCount * _flash.getInfo().sectorSize);
    _slots = s_slotBuffer;
  }

  scanDataSectors();

  LOG_INFO("WL: full scan complete: %lu valid slots", _slotCount);
  return Result::Ok;
}

// ── getInfo ──

WL::Status WL::getInfo() const {
  Status info;
  info.dataBaseAddr = _dataBaseAddr;
  info.dataSectorCount = _dataSectorCount;
  info.logSectorCount = _logSectorCount;
  info.headAddr = _headAddr;
  info.flashSize = _flashSize;
  info.metaWritten = _metaWritten;
  info.totalSlots = _dataSectorCount;
  info.usedSlots = _slotCount;
  info.totalWrites = _totalWrites;
  return info;
}

} // namespace ThetaGP::Drivers::Device
