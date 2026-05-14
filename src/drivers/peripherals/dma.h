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
 * @file dma.h
 * @brief DMA channel abstraction for ThetaGP
 *
 * DmaChannel manages a single DMA stream: clock gating, DMAMUX routing,
 * HW register configuration, and ISR dispatch. Designed to be used by
 * peripheral drivers (UART, SPI) or standalone for memory-to-memory
 * transfers.
 *
 * ISR dispatch uses pure-C function pointers registered in a static
 * dispatch table — no C++ virtual calls in interrupt context.
 */

#pragma once

#include "utils/types.h"

#include <cstdint>

namespace ThetaGP::Drivers::Peripheral::DMA {

// ── Enumerations ──

enum class Controller : uint8_t {
  Dma1,
  Dma2,
};

enum class Stream : uint8_t {
  Stream0,
  Stream1,
  Stream2,
  Stream3,
  Stream4,
  Stream5,
  Stream6,
  Stream7,
};

enum class Direction : uint8_t {
  PeripheralToMemory,
  MemoryToPeripheral,
  MemoryToMemory,
};

enum class DataWidth : uint8_t {
  Byte,
  HalfWord,
  Word,
};

enum class Priority : uint8_t {
  Low,
  Medium,
  High,
  VeryHigh,
};

enum class Burst : uint8_t {
  Single,
  Incr4,
  Incr8,
  Incr16,
};

// ── Configuration struct ──

struct DmaConfig {
  Direction direction = Direction::PeripheralToMemory;
  DataWidth srcDataWidth = DataWidth::Byte;
  DataWidth destDataWidth = DataWidth::Byte;
  Priority priority = Priority::Medium;
  Burst srcBurst = Burst::Single;
  Burst destBurst = Burst::Single;
  bool srcIncrement = false;
  bool destIncrement = true;
  bool circularMode = false;
  bool periphFlowControl = false;
  bool fifoMode = false;
  uint32_t fifoThreshold = 0;
};

// ── C-compatible ISR callback ──
// Called from pure-C ISR context. The user sets this callback and context;
// the ISR dispatch function invokes it directly without C++ method calls.

typedef void (*DmaIsrCallback)(void *context);

// ── DmaChannel ──

class DmaChannel {
public:
  DmaChannel(Controller controller, Stream stream);
  ~DmaChannel();

  // --- Configuration (call before init) ---
  void setRequestId(uint32_t dmamuxRequestId);
  void configure(const DmaConfig &cfg);

  /**
   * @brief Set ISR completion callback
   *
   * Called from the pure-C ISR dispatch when transfer completes
   * or on error. The callback receives the context pointer set here.
   */
  void setCallback(DmaIsrCallback cb, void *context = nullptr);

  // --- Lifecycle ---
  RetVal init();
  RetVal deinit();

  // --- Transfer control ---
  RetVal start(uint32_t srcAddress, uint32_t dstAddress,
               uint16_t dataCount);
  RetVal stop();

  // --- Status ---
  bool isBusy() const;
  bool isInitialized() const { return _initialized; }
  void *halHandle() const { return _halHandle; }

  Controller controller() const { return _ctrl; }
  Stream stream() const { return _stream; }
  uint32_t getRemainingCount() const;

private:
  void enableClock();
  void disableClock();

  Controller _ctrl;
  Stream _stream;
  uint32_t _requestId = 0;
  DmaConfig _cfg;
  void *_halHandle = nullptr;
  bool _initialized = false;

  DmaIsrCallback _isrCb = nullptr;
  void *_isrCtx = nullptr;
};

} // namespace ThetaGP::Drivers::Peripheral::DMA
