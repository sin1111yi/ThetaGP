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
#include "utils/utils.h"

#include "drivers/device/device.h"
#include "drivers/peripherals/peripheralsmgr.h"

#include <cstdint>

namespace ThetaGP::Drivers::Device {

/**
 * @brief Generic flash memory information structure
 */
struct FlashInfo {
  uint32_t sizeBytes = 0;      /**< Total flash size in bytes */
  uint16_t pageSize = 0;       /**< Page program size in bytes */
  uint32_t sectorSize = 0;     /**< Erase sector size in bytes */
  uint32_t blockSize = 0;      /**< Erase block size in bytes */
  uint8_t manufacturerId = 0;  /**< JEDEC manufacturer ID */
  uint16_t deviceId = 0;       /**< Device ID */
};

/**
 * @brief Abstract base class for SPI flash devices
 *
 * Inherits Device and defines the common interface for flash memory
 * operations: read, write, erase, and identification.
 * SPI bus reference (_spi) is injected via constructor from
 * PeripheralsManager, avoiding inline bus construction.
 *
 * WearLevel (nested) provides full-flash ring buffer wear-leveling.
 * Since there is only one flash chip and FlashBase is a singleton,
 * WearLevel lives here as a member rather than a separate singleton.
 */
class FlashBase : public Device {
public:
  FlashBase(const char *name, Drivers::Peripheral::BUS::SpiBus &spi)
    : Device(name), _spi(spi) {}
  ~FlashBase() override = default;

  static FlashBase &getInstance();

  // ── Pure virtual interface ──────────────────────────────────

  /** @brief Read len bytes from addr into data buffer */
  [[nodiscard]] virtual bool read(uint32_t addr, uint8_t *data, uint32_t len) = 0;

  /** @brief Write len bytes from data buffer to addr */
  [[nodiscard]] virtual bool write(uint32_t addr, const uint8_t *data, uint32_t len) = 0;

  /** @brief Erase a 4KB sector starting at addr */
  [[nodiscard]] virtual bool eraseSector(uint32_t addr) = 0;

  /** @brief Erase entire chip (may take many seconds) */
  [[nodiscard]] virtual bool eraseChip() = 0;

  /** @brief Read the chip identification (manufacturer + device) */
  [[nodiscard]] virtual uint32_t readId() = 0;

  /** @brief Get const reference to flash information struct */
  [[nodiscard]] virtual const FlashInfo &getInfo() const = 0;

  /** @brief Check if the flash is busy (erase/program in progress) */
  [[nodiscard]] virtual bool isBusy() = 0;

  // ── Wear-Leveling (nested class) ────────────────────────────

  /**
   * @brief Full-flash ring buffer wear-leveling layer
   *
   * Layout:
   *   Sector 0:       Meta (written once at first boot, never erased)
   *   Sectors 1..N:   Log sectors (dynamic count = ceil(dataSectors / 512))
   *   Sectors N+1..M: Data sectors (full flash ring buffer)
   *
   * Each data sector has a 16-byte header with a separate valid byte
   * (outside CRC scope) for stale marking via single-byte program.
   */
  class WearLevel {
  public:
    // ── Constants ──

    static constexpr uint16_t MAGIC        = 0x5447; // "TG"
    static constexpr uint16_t META_MAGIC   = 0x544D; // "TM"
    static constexpr uint16_t PAYLOAD_MAX  = 512;
    static constexpr uint8_t  MAX_KEY_LEN  = 64;
    static constexpr uint32_t SLOTS_MAX    = 32;
    static constexpr uint32_t ENTRY_SIZE   = 8;    // {addr: uint32_t, size: uint32_t}
    static constexpr uint32_t META_TYPE_SIZE = 1024;

    static constexpr uint8_t VALID = 0xFF;  // erased state, no program needed
    static constexpr uint8_t STALE = 0x00;  // marks stale via single-byte program

    // ── Structs ──

    #pragma pack(push, 1)
    struct MetaSector {
      uint16_t magic;               // META_MAGIC
      uint8_t  valid;               // VALID if written, STALE if stale
      uint8_t  version;             // meta layout version
      uint32_t crc32;               // CRC(magic + valid + version + reserved + firmwareMeta)
      uint8_t  reserved[8];
      uint8_t  firmwareMeta[1008];
    };
    #pragma pack(pop)

    static_assert(sizeof(MetaSector) == 1024, "MetaSector must be 1024 bytes");

    #pragma pack(push, 1)
    struct SlotHeader {
      uint16_t magic;       // MAGIC
      uint8_t  valid;       // VALID/STALE — NOT in CRC scope
      uint8_t  reserved;
      uint32_t keyHash;     // CRC16 of key name
      uint16_t dataLen;     // payload length
      uint16_t wearCount;   // number of times this sector was erased+written
      uint32_t crc32;       // CRC(magic + reserved + keyHash + dataLen + wearCount + payload)
    };
    #pragma pack(pop)

    static_assert(sizeof(SlotHeader) == 16, "SlotHeader must be 16 bytes");

    struct LogEntry {
      uint32_t addr = 0;
      uint32_t size = 0;
    };

    struct SlotInfo {
      uint32_t   physAddr = 0;
      SlotHeader header;
      bool       valid = false;
    };

    struct Status {
      uint32_t totalSlots = 0;
      uint32_t usedSlots = 0;
      uint32_t staleSlots = 0;
      uint32_t totalWrites = 0;
      uint16_t minWear = 0xFFFF;
      uint16_t maxWear = 0;
      uint16_t avgWear = 0;
      uint32_t dataBaseAddr = 0;
      uint32_t dataSectorCount = 0;
      uint32_t logSectorCount = 0;
      uint32_t headAddr = 0;
      uint32_t flashSize = 0;
      bool     metaWritten = false;
    };

    // ── Core API ──

    Result init();
    Result storeConfig(const char *key, const uint8_t *data, uint16_t len);
    Result loadConfig(const char *key, uint8_t *data,
                      uint16_t maxLen, uint16_t *outLen);
    Result eraseConfig(const char *key);
    Status getStatus() const;

    // ── Test/Diagnostic API ──

    Result readMeta(MetaSector &meta) const;
    Result writeMeta(const MetaSector &meta);
    Result fullScan();
    Status getInfo() const;

  private:
    friend class FlashBase;
    explicit WearLevel(FlashBase &flash) : _flash(flash) {}

    FlashBase &_flash;  // back-reference to parent flash driver

    uint32_t _flashSize = 0;
    uint32_t _metaBaseAddr = 0;
    uint32_t _logBaseAddr = 0;
    uint32_t _logSectorCount = 0;
    uint32_t _dataBaseAddr = 0;
    uint32_t _dataSectorCount = 0;
    uint32_t _headAddr = 0;
    uint32_t _entryCount = 0;
    uint32_t _totalWrites = 0;
    SlotInfo *_slots = nullptr;
    uint32_t _slotCount = 0;
    bool _metaWritten = false;
    bool _initialized = false;

    uint32_t keyHash(const char *key);
    uint32_t computeCrc(const uint8_t *data, uint16_t len);
    int32_t findSlot(const char *key);
    uint32_t nextSectorAddr(uint32_t currentAddr) const;
    void appendLogEntry(uint32_t dataAddr, uint32_t dataSize);
    void markSectorStale(uint32_t sectorAddr);
    void scanLog();
    void scanDataSectors();
  };

  /** @brief Access the wear-leveling controller */
  WearLevel &wearLevel() { return _wl; }

protected:
  Drivers::Peripheral::BUS::SpiBus &_spi;
  FlashInfo _info;
  WearLevel _wl{*this};  // constructed with back-reference to this FlashBase
};

} // namespace ThetaGP::Drivers::Device
