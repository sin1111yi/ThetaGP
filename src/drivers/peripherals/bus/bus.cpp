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

Result Bus::write(const uint8_t *data, uint16_t len) {
  return transferImpl(nullptr, nullptr, data, nullptr, len);
}

Result Bus::read(uint8_t *data, uint16_t len) {
  return transferImpl(nullptr, nullptr, nullptr, data, len);
}

Result Bus::duplexTransfer(const uint8_t *txData, uint8_t *rxData,
                            uint16_t len) {
  return transferImpl(nullptr, nullptr, txData, rxData, len);
}

Result Bus::duplexTransfer(TransferCallback cb, void *ctx,
                            const uint8_t *txData, uint8_t *rxData,
                            uint16_t len) {
  return transferImpl(cb, ctx, txData, rxData, len);
}

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
