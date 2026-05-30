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
 *   - All subclass hooks default to Result::Unsupported
 *   - Single-byte write/read delegated to multi-byte variants
 */

#include "build_info.h"

#include "drivers/peripherals/bus/bus.h"

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

Bus::Bus() {}

Bus::~Bus() {}

void Bus::setBuffers(uint8_t *txBuf, uint8_t *rxBuf, uint32_t size) {
  _txBuf = txBuf;
  _rxBuf = rxBuf;
  _bufSize = size;
}

void Bus::init() {
  if (_txBuf == nullptr && _rxBuf == nullptr) {
    _initialized = false;
    return;
  }
  _initialized = true;
}

// ── Default subclass hooks (all return Unsupported) ──────────
Result Bus::writeSync(const uint8_t *, uint16_t) {
  return Result::Unsupported;
}

Result Bus::readSync(uint8_t *, uint16_t) {
  return Result::Unsupported;
}

Result Bus::writeAsync(const uint8_t *, uint16_t) {
  return Result::Unsupported;
}

Result Bus::readAsync(uint8_t *, uint16_t) {
  return Result::Unsupported;
}

// ── Single-byte convenience (delegates to multi-byte) ────────
Result Bus::write(uint8_t byte) {
  return write(&byte, 1);
}

Result Bus::read(uint8_t *byte) {
  if (byte == nullptr) return Result::InvalidParam;
  return read(byte, 1);
}

// ── Multi-byte dispatch ──────────────────────────────────────
Result Bus::write(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len == 0) {
    return Result::InvalidParam;
  }

  switch (_mode) {
  case Mode::Synchronous:
    return writeSync(data, len);
  case Mode::Asynchronous:
    return writeAsync(data, len);
  }

  return Result::Error;
}

Result Bus::read(uint8_t *data, uint16_t len) {
  if (data == nullptr || len == 0) {
    return Result::InvalidParam;
  }

  switch (_mode) {
  case Mode::Synchronous:
    return readSync(data, len);
  case Mode::Asynchronous:
    return readAsync(data, len);
  }

  return Result::Error;
}

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
