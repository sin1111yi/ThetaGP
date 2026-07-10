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

#include "drivers/device/flash/flash_w25qxx.h"

#include "drivers/device/devmem.h"
#include "drivers/peripherals/systick.h"
#include "utils/log/log.h"
#include "utils/mempool/mempoolmanager.h"

#include <cstring>

using namespace ThetaGP::Drivers::Peripheral::BUS;

namespace ThetaGP::Drivers::Device {

FlashW25qxx::FlashW25qxx()
    : FlashBase("w25qxx",
                Drivers::Peripheral::PeripheralsManager::getInstance().spiBus(
                    FLASH_SPI)) {}

void FlashW25qxx::reset() {
  uint8_t tx[2] = {I_ENABLE_RESET, I_RESET_DEVICE};
  _spi.enable();
  (void)_spi.transmit(tx, sizeof(tx));
  _spi.disable();
}

uint8_t FlashW25qxx::readStatusReg(uint8_t idx) {
  uint8_t cmd = 0;
  switch (idx) {
  case 1:
    cmd = I_READ_SR1;
    break;
  case 2:
    cmd = I_READ_SR2;
    break;
  case 3:
    cmd = I_READ_SR3;
    break;
  default:
    return 0;
  }

  uint8_t status = 0;
  _spi.enable();
  (void)_spi.transmit(&cmd, 1);
  (void)_spi.receive(&status, 1);
  _spi.disable();
  return status;
}

void FlashW25qxx::writeEnable() {
  uint8_t tx[1] = {I_WRITE_EN};
  _spi.enable();
  (void)_spi.transmit(tx, sizeof(tx));
  _spi.disable();
  waitWhileBusy();
}

void FlashW25qxx::waitWhileBusy(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (isBusy()) {
    if (millis() - start >= timeoutMs) {
      LOG_WARN("FLASH: waitWhileBusy timeout after %ums", timeoutMs);
      break;
    }
  }
}

void FlashW25qxx::set4ByteAddrMode(bool enable) {
  uint8_t tx[1] = {enable ? I_ENTER_4B_ADDR_MODE : I_EXIT_4B_ADDR_MODE};
  _spi.enable();
  (void)_spi.transmit(tx, sizeof(tx));
  _spi.disable();
  _addrMode4Byte = enable;
}

bool FlashW25qxx::isBusy() { return (readStatusReg(1) & SR1_BUSY) != 0; }

uint32_t FlashW25qxx::readId() {
  uint8_t cmd[4] = {I_MANUF_DEV_ID, 0x00, 0x00, 0x00};
  uint8_t id[2] = {0};
  _spi.enable();
  (void)_spi.transmit(cmd, sizeof(cmd));
  (void)_spi.receive(id, sizeof(id));
  _spi.disable();
  return (static_cast<uint32_t>(id[0]) << 8) | static_cast<uint32_t>(id[1]);
}

void FlashW25qxx::init() {
  // Allocate DMA-safe buffers from DevMem pool via MempoolManager
  constexpr uint32_t BUF_SIZE = 512;
  _txBuf = static_cast<uint8_t *>(ThetaGP::Mempool::MempoolManager::alloc(
      ThetaGP::Drivers::Device::DevMem::getInstance().poolId(), BUF_SIZE));
  _rxBuf = static_cast<uint8_t *>(ThetaGP::Mempool::MempoolManager::alloc(
      ThetaGP::Drivers::Device::DevMem::getInstance().poolId(), BUF_SIZE));

  if (_txBuf == nullptr || _rxBuf == nullptr) {
    LOG_ERROR("FLASH: buffer allocation failed (tx=%p rx=%p)",
              static_cast<void *>(_txBuf), static_cast<void *>(_rxBuf));
    _initialized = false;
    return;
  }

  _spi.setBuffers(_txBuf, _rxBuf, BUF_SIZE);
  _spi.init();
  LOG_INFO("FLASH: SPI bus initialized");

  reset();
  LOG_INFO("FLASH: reset sent, waiting 10ms...");
  delay_ms(10);
  LOG_INFO("FLASH: delay done, reading ID...");

  // Retry chip ID read up to 3 times (warm reset may leave the SPI
  // peripheral in an intermediate state on some STM32H7 systems)
  uint16_t chipId = 0;
  for (int attempt = 0; attempt < 3; attempt++) {
    chipId = static_cast<uint16_t>(readId() & 0xFFFF);
    if (chipId != 0 && chipId != 0xFFFF) {
      break;
    }
    LOG_WARN("FLASH: chip ID read attempt %d returned 0x%04X, retrying...",
             attempt + 1, chipId);
    // Re-init SPI bus and reset the flash before retrying
    _spi.init();
    reset();
    delay_ms(10);
  }
  LOG_INFO("FLASH: chip ID = 0x%04X", chipId);

  // Determine flash size from chip ID
  uint32_t sizeMb = 0;

  switch (chipId) {
  case TYPE_W25Q10:
    sizeMb = 1;
    break;
  case TYPE_W25Q20:
    sizeMb = 2;
    break;
  case TYPE_W25Q40:
    sizeMb = 4;
    break;
  case TYPE_W25Q80:
    sizeMb = 8;
    break;
  case TYPE_W25Q16:
    sizeMb = 16;
    break;
  case TYPE_W25Q32:
    sizeMb = 32;
    break;
  case TYPE_W25Q64:
    sizeMb = 64;
    break;
  case TYPE_W25Q128:
    sizeMb = 128;
    break;
  case TYPE_W25Q256:
    sizeMb = 256;
    set4ByteAddrMode(true);
    break;
  case TYPE_W25Q512:
    sizeMb = 512;
    set4ByteAddrMode(true);
    break;
  default:
    _initialized = false;
    return;
  }

  _info.sizeBytes = sizeMb * 1024UL * 1024UL / 8UL; // megabits -> bytes
  _info.pageSize = 256;
  _info.sectorSize = 4096;
  _info.blockSize = 65536;
  _info.manufacturerId = static_cast<uint8_t>(chipId >> 8);
  _info.deviceId = chipId & 0xFF;

  _initialized = true;
}

bool FlashW25qxx::read(uint32_t addr, uint8_t *data, uint32_t len) {
  if (!_initialized || data == nullptr || len == 0) {
    return false;
  }

  if (addr >= _info.sizeBytes || (addr + len) > _info.sizeBytes) {
    return false;
  }

  uint8_t addrBytes = _addrMode4Byte ? 4 : 3;
  uint8_t cmdLen = 1 + addrBytes;

  uint8_t cmdBuf[5];
  cmdBuf[0] = _addrMode4Byte ? I_READ_DATA_4B : I_READ_DATA;
  if (_addrMode4Byte) {
    cmdBuf[1] = static_cast<uint8_t>((addr >> 24) & 0xFF);
    cmdBuf[2] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    cmdBuf[3] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    cmdBuf[4] = static_cast<uint8_t>(addr & 0xFF);
  } else {
    cmdBuf[1] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    cmdBuf[2] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    cmdBuf[3] = static_cast<uint8_t>(addr & 0xFF);
  }

  _spi.enable();
  (void)_spi.transmit(cmdBuf, cmdLen);
  (void)_spi.receive(data, len);
  _spi.disable();

  return true;
}

bool FlashW25qxx::write(uint32_t addr, const uint8_t *data, uint32_t len) {
  if (!_initialized || data == nullptr || len == 0) {
    return false;
  }

  if (addr >= _info.sizeBytes || (addr + len) > _info.sizeBytes) {
    return false;
  }

  uint32_t remaining = len;
  uint32_t currentAddr = addr;
  const uint8_t *currentData = data;

  while (remaining > 0) {
    uint32_t pageOffset = currentAddr % 256;
    uint32_t pageRemaining = 256 - pageOffset;
    uint32_t writeLen = (remaining < pageRemaining) ? remaining : pageRemaining;

    writeEnable();

    uint8_t addrBytes = _addrMode4Byte ? 4 : 3;
    uint8_t cmdLen = 1 + addrBytes;

    // Build command + address
    uint8_t cmdBuf[5];
    cmdBuf[0] = _addrMode4Byte ? I_PAGE_PGM_4B : I_PAGE_PGM;
    if (_addrMode4Byte) {
      cmdBuf[1] = static_cast<uint8_t>((currentAddr >> 24) & 0xFF);
      cmdBuf[2] = static_cast<uint8_t>((currentAddr >> 16) & 0xFF);
      cmdBuf[3] = static_cast<uint8_t>((currentAddr >> 8) & 0xFF);
      cmdBuf[4] = static_cast<uint8_t>(currentAddr & 0xFF);
    } else {
      cmdBuf[1] = static_cast<uint8_t>((currentAddr >> 16) & 0xFF);
      cmdBuf[2] = static_cast<uint8_t>((currentAddr >> 8) & 0xFF);
      cmdBuf[3] = static_cast<uint8_t>(currentAddr & 0xFF);
    }

    _spi.enable();
    (void)_spi.transmit(cmdBuf, cmdLen);
    (void)_spi.transmit(currentData, writeLen);
    _spi.disable();

    waitWhileBusy();

    currentAddr += writeLen;
    currentData += writeLen;
    remaining -= writeLen;
  }

  return true;
}

bool FlashW25qxx::eraseSector(uint32_t addr) {
  if (!_initialized) {
    return false;
  }

  if (addr >= _info.sizeBytes) {
    return false;
  }

  writeEnable();

  uint8_t addrBytes = _addrMode4Byte ? 4 : 3;

  uint8_t cmdBuf[5];
  cmdBuf[0] = _addrMode4Byte ? I_SECTOR_ERASE_4K_4B : I_SECTOR_ERASE_4K;
  if (_addrMode4Byte) {
    cmdBuf[1] = static_cast<uint8_t>((addr >> 24) & 0xFF);
    cmdBuf[2] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    cmdBuf[3] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    cmdBuf[4] = static_cast<uint8_t>(addr & 0xFF);
  } else {
    cmdBuf[1] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    cmdBuf[2] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    cmdBuf[3] = static_cast<uint8_t>(addr & 0xFF);
  }

  _spi.enable();
  (void)_spi.transmit(cmdBuf, 1 + addrBytes);
  _spi.disable();

  waitWhileBusy();
  return true;
}

bool FlashW25qxx::eraseChip() {
  if (!_initialized) {
    return false;
  }

  writeEnable();

  uint8_t tx[1] = {I_CHIP_ERASE};
  _spi.enable();
  (void)_spi.transmit(tx, sizeof(tx));
  _spi.disable();

  waitWhileBusy();
  return true;
}

const FlashInfo &FlashW25qxx::getInfo() const { return _info; }

void FlashW25qxx::setSpiBusMode(Mode mode) {
  _spi.setMode(mode);
}

FlashBase &FlashBase::getInstance() {
#if defined(FLASH_CHIP_W25QXX)
  return FlashW25qxx::getInstance();
#else
  #error "No flash chip selected. Add [flash] chip = 'w25qxx' to BoardConfig.toml (see configs/CONFIGURATION.md)"
#endif
}

} // namespace ThetaGP::Drivers::Device
