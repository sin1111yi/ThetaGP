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
 * @file bus.h
 * @brief Abstract base class for all BUS peripherals (SPI, UART, I2C)
 *
 * Two modes: Polling (LL), Dma (auto DMA with fallback).
 *
 * All transfers route through a single virtual hook transferImpl.
 * Half-duplex write/read are convenience wrappers.
 */

#pragma once

#include "build_info.h"

#include "utils/mempool/mempoolmanager.h"
#include "utils/types.h"

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

enum class Type { Uart, Spi, I2c };

enum class Mode { Polling, Dma };

enum class TransferStatus { Ready, Repeat };

using TransferCallback = TransferStatus (*)(void *ctx);

class Bus {
public:
  virtual ~Bus();

  void setMode(Mode m) { _mode = m; }
  void setType(Type t) { _type = t; }
  [[nodiscard]] Mode mode() const { return _mode; }
  [[nodiscard]] Type type() const { return _type; }
  [[nodiscard]] bool isInitialized() const { return _initialized; }
  void setBuffers(uint8_t *txBuf, uint8_t *rxBuf, uint32_t size);

  uint8_t *txBuf() const { return _txBuf; }
  uint8_t *rxBuf() const { return _rxBuf; }
  uint32_t bufSize() const { return _bufSize; }

  // ── blocking half-duplex ──────────────────────────────────
  Result write(const uint8_t *data, uint16_t len);
  Result read(uint8_t *data, uint16_t len);

  // ── blocking full-duplex ──────────────────────────────────
  Result duplexTransfer(const uint8_t *txData, uint8_t *rxData,
                         uint16_t len);

  // ── non-blocking full-duplex (DMA + callback) ─────────────
  Result duplexTransfer(TransferCallback cb, void *ctx,
                         const uint8_t *txData, uint8_t *rxData,
                         uint16_t len);

  virtual void init();
  virtual void enableClock() = 0;

protected:
  Bus();

  virtual Result transferImpl(TransferCallback cb, void *ctx,
                               const uint8_t *txData, uint8_t *rxData,
                               uint16_t len) {
    return Result::Unsupported;
  }

  Type _type;
  Mode _mode = Mode::Dma;
  bool _initialized = false;

  uint8_t *_txBuf = nullptr;
  uint8_t *_rxBuf = nullptr;
  uint32_t _bufSize = 0;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
