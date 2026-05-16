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
 * @brief BUS base class implementation
 *
 * After refactor:
 *   - No function pointer dispatch (removed _writeByteFn etc.)
 *   - No setupCallbacks() — dispatch is a simple switch in write()/read()
 *   - All subclass hooks default to RetVal::Unsupported
 *   - Single-byte write/read delegated to multi-byte variants
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
      _pTxBufSize = txSize;
    }
  }

  if (_pRxBuf == nullptr) {
    _pRxBuf = static_cast<uint8_t *>(_busMem.allocRxBuffer(rxSize));
    if (_pRxBuf != nullptr) {
      std::memset(_pRxBuf, 0, rxSize);
      _pRxBufSize = rxSize;
    }
  }
}

void Bus::freeBuf() {
  if (_pTxBuf != nullptr) {
    _busMem.freeTxBuffer(_pTxBuf);
    _pTxBuf = nullptr;
    _pTxBufSize = 0;
  }

  if (_pRxBuf != nullptr) {
    _busMem.freeRxBuffer(_pRxBuf);
    _pRxBuf = nullptr;
    _pRxBufSize = 0;
  }
}

void Bus::init() {
  _initialized = true;
}

// ── Default subclass hooks (all return Unsupported) ──────────
RetVal Bus::writeSync(const uint8_t *, uint16_t) {
  return RetVal::Unsupported;
}

RetVal Bus::readSync(uint8_t *, uint16_t) {
  return RetVal::Unsupported;
}

RetVal Bus::writeAsync(const uint8_t *, uint16_t) {
  return RetVal::Unsupported;
}

RetVal Bus::readAsync(uint8_t *, uint16_t) {
  return RetVal::Unsupported;
}

// ── Single-byte convenience (delegates to multi-byte) ────────
RetVal Bus::write(uint8_t byte) {
  return write(&byte, 1);
}

RetVal Bus::read(uint8_t *byte) {
  if (byte == nullptr) return RetVal::InvalidParam;
  return read(byte, 1);
}

// ── Multi-byte dispatch ──────────────────────────────────────
RetVal Bus::write(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len == 0) {
    return RetVal::InvalidParam;
  }

  switch (_mode) {
  case Mode::Synchronous:
    return writeSync(data, len);
  case Mode::Asynchronous:
    return writeAsync(data, len);
  }

  return RetVal::Error;
}

RetVal Bus::read(uint8_t *data, uint16_t len) {
  if (data == nullptr || len == 0) {
    return RetVal::InvalidParam;
  }

  switch (_mode) {
  case Mode::Synchronous:
    return readSync(data, len);
  case Mode::Asynchronous:
    return readAsync(data, len);
  }

  return RetVal::Error;
}

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
