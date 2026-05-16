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

#include "drivers/peripherals/systick.h"

#include <cstring>

using namespace ThetaGP::Drivers::Peripheral::BUS;

namespace ThetaGP::Drivers::Device {

// ── Constructor ───────────────────────────────────────────────

W25qxxFlash::W25qxxFlash()
    : FlashBase("w25qxx") {
}

// ── Internal helpers ──────────────────────────────────────────

void W25qxxFlash::reset() {
  // Enable Reset + Reset Device sequence
  uint8_t tx[2] = {I_ENABLE_RESET, I_RESET_DEVICE};
  _spi.transfer(tx, nullptr, sizeof(tx));
}

uint8_t W25qxxFlash::readStatusReg(uint8_t idx) {
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

  uint8_t tx[2] = {cmd, 0x00};
  uint8_t rx[2] = {0};
  _spi.transfer(tx, rx, sizeof(tx));
  return rx[1];
}

void W25qxxFlash::writeEnable() {
  uint8_t tx[1] = {I_WRITE_EN};
  _spi.transfer(tx, nullptr, sizeof(tx));
  waitWhileBusy();
}

void W25qxxFlash::waitWhileBusy() {
  while (isBusy()) {
  }
}

void W25qxxFlash::set4ByteAddrMode(bool enable) {
  uint8_t tx[1] = {enable ? I_ENTER_4B_ADDR_MODE : I_EXIT_4B_ADDR_MODE};
  _spi.transfer(tx, nullptr, sizeof(tx));
  _addrMode4Byte = enable;
}

// ── isBusy ────────────────────────────────────────────────────

bool W25qxxFlash::isBusy() {
  return (readStatusReg(1) & SR1_BUSY) != 0;
}

// ── readId ────────────────────────────────────────────────────

uint32_t W25qxxFlash::readId() {
  // Manufacturer/Device ID command (0x90): cmd + 3-byte address (0x000000)
  uint8_t tx[6] = {I_MANUF_DEV_ID, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t rx[6] = {0};
  _spi.transfer(tx, rx, sizeof(tx));
  // rx[4] = manufacturer ID, rx[5] = device ID
  return (static_cast<uint32_t>(rx[4]) << 8) | static_cast<uint32_t>(rx[5]);
}

// ── init ──────────────────────────────────────────────────────

void W25qxxFlash::init() {
#if defined(FLASH_SPI)
  // Increase SPI buffer to handle page-size transfers (cmd + addr + up to 256
  // bytes of data)
  _spi.configBufSize(512, 512);
  _spi.init();
#endif

  reset();
  delay_ms(10);

  uint16_t chipId = static_cast<uint16_t>(readId() & 0xFFFF);

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

// ── read ──────────────────────────────────────────────────────

bool W25qxxFlash::read(uint32_t addr, uint8_t *data, uint32_t len) {
  if (!_initialized || data == nullptr || len == 0) {
    return false;
  }

  if (addr >= _info.sizeBytes || (addr + len) > _info.sizeBytes) {
    return false;
  }

  // Build transfer buffer: command + address + dummy bytes for data clock
  // We send command+addr, then dummy bytes to clock out the data
  uint8_t addrBytes = _addrMode4Byte ? 4 : 3;
  uint8_t cmdLen = 1 + addrBytes; // command byte + address

  // For full-duplex, send command+addr and then (len) dummy bytes,
  // simultaneously receiving data into rx buffer
  // We need a tx buffer of cmdLen + len bytes and rx buffer of same size
  // Due to SpiBus buffer constraints, read in chunks if needed
  uint32_t remaining = len;
  uint32_t currentAddr = addr;
  uint8_t *currentData = data;

  // Use a reasonable chunk size that fits in the SPI buffer
  const uint32_t chunkSize = 256;

  while (remaining > 0) {
    uint32_t chunkLen = (remaining < chunkSize) ? remaining : chunkSize;

    // Build tx buffer: cmd + addr + dummy bytes
    // We'll use a local buffer and call transfer multiple times if the chunk
    // is small enough. For larger reads, we rely on the SPI buffer being
    // configured large enough (512 bytes in init).
    uint8_t totalLen = cmdLen + chunkLen;

    // Allocate on stack if small enough, otherwise... use a pattern
    // Since cmdLen <= 5 and chunkLen <= 256, totalLen <= 261 fits in configured
    // buffer of 512
    uint8_t txBuf[261];
    uint8_t rxBuf[261];

    txBuf[0] = _addrMode4Byte ? I_READ_DATA_4B : I_READ_DATA;
    if (_addrMode4Byte) {
      txBuf[1] = static_cast<uint8_t>((currentAddr >> 24) & 0xFF);
      txBuf[2] = static_cast<uint8_t>((currentAddr >> 16) & 0xFF);
      txBuf[3] = static_cast<uint8_t>((currentAddr >> 8) & 0xFF);
      txBuf[4] = static_cast<uint8_t>(currentAddr & 0xFF);
    } else {
      txBuf[1] = static_cast<uint8_t>((currentAddr >> 16) & 0xFF);
      txBuf[2] = static_cast<uint8_t>((currentAddr >> 8) & 0xFF);
      txBuf[3] = static_cast<uint8_t>(currentAddr & 0xFF);
    }
    // Fill remaining with dummy (0x00) — already zero from stack initialization

    if (_spi.transfer(txBuf, rxBuf, totalLen) != RetVal::Ok) {
      return false;
    }

    // Copy received data (skip command + address bytes in rx)
    std::memcpy(currentData, &rxBuf[cmdLen], chunkLen);

    currentAddr += chunkLen;
    currentData += chunkLen;
    remaining -= chunkLen;
  }

  return true;
}

// ── write ─────────────────────────────────────────────────────

bool W25qxxFlash::write(uint32_t addr, const uint8_t *data, uint32_t len) {
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
    // Page program boundary: each page is 256 bytes
    uint32_t pageOffset = currentAddr % 256;
    uint32_t pageRemaining = 256 - pageOffset;
    uint32_t writeLen = (remaining < pageRemaining) ? remaining : pageRemaining;

    writeEnable();

    uint8_t addrBytes = _addrMode4Byte ? 4 : 3;
    uint8_t cmdLen = 1 + addrBytes;
    uint8_t totalLen = cmdLen + writeLen;

    // Build full transfer buffer: cmd + addr + data
    uint8_t txBuf[261]; // max: 5 (cmd+4byte addr) + 256 (page)
    uint8_t rxBuf[261];

    txBuf[0] = _addrMode4Byte ? I_PAGE_PGM_4B : I_PAGE_PGM;
    if (_addrMode4Byte) {
      txBuf[1] = static_cast<uint8_t>((currentAddr >> 24) & 0xFF);
      txBuf[2] = static_cast<uint8_t>((currentAddr >> 16) & 0xFF);
      txBuf[3] = static_cast<uint8_t>((currentAddr >> 8) & 0xFF);
      txBuf[4] = static_cast<uint8_t>(currentAddr & 0xFF);
    } else {
      txBuf[1] = static_cast<uint8_t>((currentAddr >> 16) & 0xFF);
      txBuf[2] = static_cast<uint8_t>((currentAddr >> 8) & 0xFF);
      txBuf[3] = static_cast<uint8_t>(currentAddr & 0xFF);
    }
    std::memcpy(&txBuf[cmdLen], currentData, writeLen);

    // Full-duplex transfer: send command + addr + data, discard rx
    if (_spi.transfer(txBuf, rxBuf, totalLen) != RetVal::Ok) {
      return false;
    }

    waitWhileBusy();

    currentAddr += writeLen;
    currentData += writeLen;
    remaining -= writeLen;
  }

  return true;
}

// ── eraseSector ───────────────────────────────────────────────

bool W25qxxFlash::eraseSector(uint32_t addr) {
  if (!_initialized) {
    return false;
  }

  if (addr >= _info.sizeBytes) {
    return false;
  }

  writeEnable();

  uint8_t addrBytes = _addrMode4Byte ? 4 : 3;
  uint8_t totalLen = 1 + addrBytes;

  uint8_t txBuf[5];
  uint8_t rxBuf[5];

  txBuf[0] = _addrMode4Byte ? I_SECTOR_ERASE_4K_4B : I_SECTOR_ERASE_4K;
  if (_addrMode4Byte) {
    txBuf[1] = static_cast<uint8_t>((addr >> 24) & 0xFF);
    txBuf[2] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    txBuf[3] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    txBuf[4] = static_cast<uint8_t>(addr & 0xFF);
  } else {
    txBuf[1] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    txBuf[2] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    txBuf[3] = static_cast<uint8_t>(addr & 0xFF);
  }

  if (_spi.transfer(txBuf, rxBuf, totalLen) != RetVal::Ok) {
    return false;
  }

  waitWhileBusy();
  return true;
}

// ── eraseChip ─────────────────────────────────────────────────

bool W25qxxFlash::eraseChip() {
  if (!_initialized) {
    return false;
  }

  writeEnable();

  uint8_t tx[1] = {I_CHIP_ERASE};
  uint8_t rx[1] = {0};

  if (_spi.transfer(tx, rx, sizeof(tx)) != RetVal::Ok) {
    return false;
  }

  waitWhileBusy();
  return true;
}

// ── getInfo ───────────────────────────────────────────────────

const FlashInfo &W25qxxFlash::getInfo() const {
  return _info;
}

} // namespace ThetaGP::Drivers::Device
