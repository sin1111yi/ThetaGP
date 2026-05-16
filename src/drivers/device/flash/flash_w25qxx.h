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

#include "BoardConfig.h"

#include "build_info.h"
#include "utils/utils.h"

#include "drivers/device/flash/flash_base.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/gpio.h"

namespace ThetaGP::Drivers::Device {

/**
 * @brief W25QXX family SPI flash driver (C++ implementation)
 *
 * Replaces the old C-style w25qxx.c/w25qxx.h driver.
 * Uses SpiBus::transfer() for all SPI communication (full-duplex).
 * Singleton pattern, gated by FLASH_SPI compile-time macro.
 */
class W25qxxFlash : public FlashBase {
public:
  static W25qxxFlash &getInstance() {
    static W25qxxFlash instance;
    return instance;
  }

  // ── FlashBase interface ─────────────────────────────────────
  void init() override;
  bool read(uint32_t addr, uint8_t *data, uint32_t len) override;
  bool write(uint32_t addr, const uint8_t *data, uint32_t len) override;
  bool eraseSector(uint32_t addr) override;
  bool eraseChip() override;
  uint32_t readId() override;
  const FlashInfo &getInfo() const override;
  bool isBusy() override;

private:
  W25qxxFlash();

  // ── W25QXX instruction set (private constexpr) ─────────────
  static constexpr uint8_t I_WRITE_EN = 0x06U;
  static constexpr uint8_t I_VSR_WRITE_EN = 0x50U;
  static constexpr uint8_t I_WRITE_DIS = 0x04U;

  static constexpr uint8_t I_RELEASE_PD = 0xABU;
  static constexpr uint8_t I_MANUF_DEV_ID = 0x90U;
  static constexpr uint8_t I_JEDEC_ID = 0x9FU;
  static constexpr uint8_t I_READ_UNIQ_ID = 0x4BU;

  static constexpr uint8_t I_READ_DATA = 0x03U;
  static constexpr uint8_t I_READ_DATA_4B = 0x13U;
  static constexpr uint8_t I_FAST_READ = 0x0BU;
  static constexpr uint8_t I_FAST_READ_4B = 0x0CU;

  static constexpr uint8_t I_PAGE_PGM = 0x02U;
  static constexpr uint8_t I_PAGE_PGM_4B = 0x12U;

  static constexpr uint8_t I_SECTOR_ERASE_4K = 0x20U;
  static constexpr uint8_t I_SECTOR_ERASE_4K_4B = 0x21U;
  static constexpr uint8_t I_BLOCK_ERASE_32K = 0x52U;
  static constexpr uint8_t I_BLOCK_ERASE_64K = 0xD8U;
  static constexpr uint8_t I_BLOCK_ERASE_64K_4B = 0xDCU;
  static constexpr uint8_t I_CHIP_ERASE = 0xC7U;

  static constexpr uint8_t I_READ_SR1 = 0x05U;
  static constexpr uint8_t I_READ_SR2 = 0x35U;
  static constexpr uint8_t I_READ_SR3 = 0x15U;
  static constexpr uint8_t I_WRITE_SR1 = 0x01U;
  static constexpr uint8_t I_WRITE_SR2 = 0x31U;
  static constexpr uint8_t I_WRITE_SR3 = 0x11U;

  static constexpr uint8_t I_READ_SFDP_REG = 0x5AU;
  static constexpr uint8_t I_ERASE_SECURITY_REG = 0x44U;
  static constexpr uint8_t I_PROGRAM_SECURITY_REG = 0x42U;
  static constexpr uint8_t I_READ_SECURITY_REG = 0x48U;

  static constexpr uint8_t I_GLOBAL_BLOCK_LOCK = 0x7EU;
  static constexpr uint8_t I_GLOBAL_BLOCK_UNLOCK = 0x98U;
  static constexpr uint8_t I_READ_BLOCK_LOCK = 0x3DU;
  static constexpr uint8_t I_INDIVIDUAL_BLOCK_LOCK = 0x36U;
  static constexpr uint8_t I_INDIVIDUAL_BLOCK_UNLOCK = 0x39U;

  static constexpr uint8_t I_ERASE_PGM_SUSPEND = 0x75U;
  static constexpr uint8_t I_ERASE_PGM_RESUME = 0x7AU;
  static constexpr uint8_t I_POWER_DOWN = 0xB9U;

  static constexpr uint8_t I_ENTER_4B_ADDR_MODE = 0xB7U;
  static constexpr uint8_t I_EXIT_4B_ADDR_MODE = 0xE9U;

  static constexpr uint8_t I_ENABLE_RESET = 0x66U;
  static constexpr uint8_t I_RESET_DEVICE = 0x99U;

  // ── Status Register bits ────────────────────────────────────
  static constexpr uint8_t SR1_BUSY = 0x01U;
  static constexpr uint8_t SR1_WEL = 0x02U;

  // ── W25QXX type IDs (manufacturer << 8 | device) ────────────
  static constexpr uint16_t TYPE_W25Q10 = 0xEF10U;
  static constexpr uint16_t TYPE_W25Q20 = 0xEF11U;
  static constexpr uint16_t TYPE_W25Q40 = 0xEF12U;
  static constexpr uint16_t TYPE_W25Q80 = 0xEF13U;
  static constexpr uint16_t TYPE_W25Q16 = 0xEF14U;
  static constexpr uint16_t TYPE_W25Q32 = 0xEF15U;
  static constexpr uint16_t TYPE_W25Q64 = 0xEF16U;
  static constexpr uint16_t TYPE_W25Q128 = 0xEF17U;
  static constexpr uint16_t TYPE_W25Q256 = 0xEF18U;
  static constexpr uint16_t TYPE_W25Q512 = 0xEF19U;

  // ── Internal helpers ────────────────────────────────────────
  uint8_t readStatusReg(uint8_t idx);
  void writeEnable();
  void waitWhileBusy();
  void set4ByteAddrMode(bool enable);
  void reset();

  // ── Members ─────────────────────────────────────────────────
  bool _addrMode4Byte = false;
};

} // namespace ThetaGP::Drivers::Device
