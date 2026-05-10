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

/**
 * @file bus.cpp
 * @brief BUS base class implementation with memory pool support
 */

#include "build_info.h"

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/bus/busmem.h"

#include <cstring>

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

Bus::Bus() : _busMem(BusMem::getInstance()) {}

Bus::~Bus() { freeBuf(); }

void Bus::allocBuf(uint32_t txSize, uint32_t rxSize) {
  if (_pTxBuf == nullptr) {
    _pTxBuf = static_cast<uint8_t *>(_busMem.allocTxBuffer(txSize));
    if (_pTxBuf != nullptr) {
      std::memset(_pTxBuf, 0, txSize);
    }
  }

  if (_pRxBuf == nullptr) {
    _pRxBuf = static_cast<uint8_t *>(_busMem.allocRxBuffer(rxSize));
    if (_pRxBuf != nullptr) {
      std::memset(_pRxBuf, 0, rxSize);
    }
  }
}

void Bus::freeBuf() {
  if (_pTxBuf != nullptr) {
    _busMem.freeTxBuffer(_pTxBuf);
    _pTxBuf = nullptr;
  }

  if (_pRxBuf != nullptr) {
    _busMem.freeRxBuffer(_pRxBuf);
    _pRxBuf = nullptr;
  }
}

void Bus::init() {
  setupCallbacks();
  _initialized = true;
}

void Bus::setupCallbacks() {
  switch (_mode) {
  case Mode::Polling:
    _writeByteFn = &Bus::writeBytePolling;
    _writeBytesFn = &Bus::writeBytesPolling;
    _readByteFn = &Bus::readBytePolling;
    _readBytesFn = &Bus::readBytesPolling;
    break;
  case Mode::Interrupt:
    _writeByteFn = &Bus::writeByteInterrupt;
    _writeBytesFn = &Bus::writeBytesInterrupt;
    _readByteFn = &Bus::readByteInterrupt;
    _readBytesFn = &Bus::readBytesInterrupt;
    break;
  case Mode::DirectMemAccess:
    _writeByteFn = &Bus::writeByteDMA;
    _writeBytesFn = &Bus::writeBytesDMA;
    _readByteFn = &Bus::readByteDMA;
    _readBytesFn = &Bus::readBytesDMA;
    break;
  }
}

// Function-pointer dispatch for single-byte operations
RetVal Bus::write(uint8_t byte) {
  if (_writeByteFn != nullptr) {
    return (this->*_writeByteFn)(byte);
  }
  return RetVal::Error;
}

RetVal Bus::read(uint8_t *byte) {
  if (_readByteFn != nullptr) {
    return (this->*_readByteFn)(byte);
  }
  return RetVal::Error;
}

RetVal Bus::write(uint8_t *bytes, uint16_t num) {
  if (bytes == nullptr || num == 0 || _writeBytesFn == nullptr) {
    return RetVal::Error;
  }
  return (this->*_writeBytesFn)(bytes, num);
}

RetVal Bus::read(uint8_t *bytes, uint16_t num) {
  if (bytes == nullptr || num == 0 || _readBytesFn == nullptr) {
    return RetVal::Error;
  }
  return (this->*_readBytesFn)(bytes, num);
}

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
