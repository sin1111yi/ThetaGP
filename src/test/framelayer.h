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

#include <cstdint>
#include <ArduinoJson.h>

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

/**
 * FrameLayer -- CDC frame transport layer.
 *
 * Handles \r\n frame delimiting and JSON serialization/deserialization
 * over the USB CDC ACM virtual serial port.
 */
class FrameLayer {
public:
  FrameLayer(const FrameLayer &) = delete;
  FrameLayer &operator=(const FrameLayer &) = delete;
  FrameLayer() = default;

  static FrameLayer &getInstance();

    /**
     * Static CDC RX handler suitable for USBDriver::setCDCRxCallback.
     * Called from USB IRQ context with a buffer of received bytes.
     */
    static void cdcRxHandler(void *buffer, uint16_t len);

    /**
     * Process a single received byte through the \r\n state machine.
     * Accumulates bytes in _rxBuf until a complete frame is received.
     */
    void processByte(uint8_t byte);

    /**
     * Serialize a JsonDocument and send it as a CDC frame (appends \r\n).
     */
    void sendResponse(const JsonDocument &doc);

    /** Callback type invoked when a complete frame is received. */
    using FrameCallback = void (*)(const char *jsonLine);

    /**
     * Register the callback that will be called for each complete frame.
     */
    void setFrameCallback(FrameCallback cb);

    void flushTx();
    /** \r\n frame delimiter state machine states. */
    enum class RxState { IDLE, DATA, CR };

  RxState _rxState = RxState::IDLE;
  char _rxBuf[1024];
  uint16_t _rxLen = 0;
  FrameCallback _frameCallback = nullptr;

  // TX pending buffer — defer tud_cdc_write to main loop
  char _txPendingBuf[1024];
  uint16_t _txPendingLen = 0;
  uint16_t _txPendingSent = 0;
  bool _txPending = false;
};

#else

/** Production-mode no-op stub. */
class FrameLayer {
public:
    static FrameLayer &getInstance() { static FrameLayer i; return i; }
    static void cdcRxHandler(void *, uint16_t) {}
    void processByte(uint8_t) {}
    void sendResponse(const JsonDocument &) {}
    using FrameCallback = void (*)(const char *);
    void setFrameCallback(FrameCallback) {}
    void flushTx() {}
};

#endif

} // namespace ThetaGP::Test
