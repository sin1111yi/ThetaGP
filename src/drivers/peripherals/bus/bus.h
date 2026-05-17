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
 * Architecture (after refactor):
 *   - 2 modes: Synchronous (blocking) and Asynchronous (non-blocking)
 *   - 4 protected virtual hooks: writeSync/readSync/writeAsync/readAsync
 *   - No function-pointer dispatch (removed double indirection)
 *   - Single-byte ops delegate to multi-byte variants
 *   - Subclasses override only what they support; unsupported returns Unsupported
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

/**
 * @brief Bus transfer mode
 *
 * Synchronous  — blocking call; returns when transfer completes or times out.
 * Asynchronous — non-blocking; returns immediately, completion signaled via
 *                callback or event. Only UART uses this (DMA) in practice.
 */
enum class Mode { Synchronous, Asynchronous };

/**
 * @brief Abstract base class for BUS peripherals
 *
 * Write/Read dispatch directly on _mode via switch statement — no function
 * pointers. Subclasses override the *Sync or *Async hooks they support.
 * Single-byte write/read simply delegates to the multi-byte variant with len=1.
 */
class Bus {
public:
  virtual ~Bus();

  // ── Public API ──────────────────────────────────────────────
  RetVal write(uint8_t byte);
  RetVal write(const uint8_t *data, uint16_t len);
  RetVal read(uint8_t *byte);
  RetVal read(uint8_t *data, uint16_t len);

  // ── Configuration ───────────────────────────────────────────
  void setType(Type t) { _type = t; }
  void setMode(Mode m) { _mode = m; }
  Type type() const { return _type; }
  Mode mode() const { return _mode; }
  bool isInitialized() const { return _initialized; }
  void setBuffers(uint8_t *txBuf, uint8_t *rxBuf, uint32_t size);

  // ── Lifecycle ───────────────────────────────────────────────
  virtual void init();
  virtual void enableClock() = 0;

protected:
  Bus();

  // ── Subclass hooks (all default to RetVal::Unsupported) ─────
  virtual RetVal writeSync(const uint8_t *data, uint16_t len);
  virtual RetVal readSync(uint8_t *data, uint16_t len);
  virtual RetVal writeAsync(const uint8_t *data, uint16_t len);
  virtual RetVal readAsync(uint8_t *data, uint16_t len);

  // ── Members ─────────────────────────────────────────────────
  Type _type;
  Mode _mode = Mode::Synchronous;
  bool _initialized = false;

  // DMA-safe buffers (set externally via setBuffers())
  uint8_t *_txBuf = nullptr;
  uint8_t *_rxBuf = nullptr;
  uint32_t _bufSize = 0;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
