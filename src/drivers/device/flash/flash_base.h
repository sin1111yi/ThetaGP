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

#include "drivers/device/device.h"

#include <cstdint>

namespace ThetaGP::Drivers::Device {

/**
 * @brief Generic flash memory information structure
 */
struct FlashInfo {
  uint32_t sizeBytes = 0;      /**< Total flash size in bytes */
  uint16_t pageSize = 256;     /**< Page program size in bytes */
  uint32_t sectorSize = 4096;  /**< Erase sector size in bytes */
  uint32_t blockSize = 65536;  /**< Erase block size in bytes */
  uint8_t manufacturerId = 0;  /**< JEDEC manufacturer ID */
  uint16_t deviceId = 0;       /**< Device ID */
};

/**
 * @brief Abstract base class for SPI flash devices
 *
 * Inherits Device and defines the common interface for flash memory
 * operations: read, write, erase, and identification.
 */
class FlashBase : public Device {
public:
  FlashBase(const char *name) : Device(name) {}
  ~FlashBase() override = default;

  // ── Pure virtual interface ──────────────────────────────────

  /** @brief Read len bytes from addr into data buffer */
  virtual bool read(uint32_t addr, uint8_t *data, uint32_t len) = 0;

  /** @brief Write len bytes from data buffer to addr */
  virtual bool write(uint32_t addr, const uint8_t *data, uint32_t len) = 0;

  /** @brief Erase a 4KB sector starting at addr */
  virtual bool eraseSector(uint32_t addr) = 0;

  /** @brief Erase entire chip (may take many seconds) */
  virtual bool eraseChip() = 0;

  /** @brief Read the chip identification (manufacturer + device) */
  virtual uint32_t readId() = 0;

  /** @brief Get const reference to flash information struct */
  virtual const FlashInfo &getInfo() const = 0;

  /** @brief Check if the flash is busy (erase/program in progress) */
  virtual bool isBusy() = 0;
};

} // namespace ThetaGP::Drivers::Device
