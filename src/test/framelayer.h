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
#include "build_info.h"

namespace ThetaGP::Test {

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
     * Process command queue from main loop context.
     * Must be called periodically from a non-ISR task.
     * Handles:
     *   1. Deferred RAW capture completion callbacks
     *   2. Dispatch of enqueued command frames via _frameCallback
     */
    void processCommandQueue();

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

    /** Callback type invoked when a raw capture completes. */
    using RawCaptureCallback = void (*)(const uint8_t *buf, uint16_t len);

    /**
     * Register the callback that will be called for each complete frame.
     */
    void setFrameCallback(FrameCallback cb);

    /**
     * Start raw byte capture mode.
     *
     * In RAW state, processByte() accumulates bytes into buf until
     * len bytes are received, then calls the callback and returns to IDLE.
     */
    void startRawCapture(uint8_t *buf, uint16_t len, RawCaptureCallback cb);

    void flushTx();
    /** \r\n frame delimiter state machine states. */
    enum class RxState { IDLE, DATA, CR, RAW };

    RxState _rxState = RxState::IDLE;
    char _rxBuf[8192];
    uint16_t _rxLen = 0;
    FrameCallback _frameCallback = nullptr;

    // TX pending buffer — defer tud_cdc_write to main loop
    char _txPendingBuf[4096];
    uint16_t _txPendingLen = 0;
    uint16_t _txPendingSent = 0;
    bool _txPending = false;

    // RAW capture support
    uint8_t *_rawBuf = nullptr;
    uint16_t _rawTargetLen = 0;
    RawCaptureCallback _rawCallback = nullptr;

    // ── Command queue (ISR → main loop decoupling) ──
    static constexpr uint8_t CMD_QUEUE_SIZE = 8;
    static constexpr uint16_t CMD_QUEUE_FRAME_MAX = 256;

    COMMON_ZERO_INIT static char _cmdQueue[CMD_QUEUE_SIZE][CMD_QUEUE_FRAME_MAX];
    COMMON_ZERO_INIT static volatile uint8_t _cmdHead;   // ISR writes (producer)
    static uint8_t _cmdTail;                     // main loop reads (consumer)

    // ── Deferred RAW completion (ISR → main loop) ──
    COMMON_ZERO_INIT static volatile bool _rawDone;
    COMMON_ZERO_INIT static volatile uint16_t _rawDoneLen;
    COMMON_ZERO_INIT static uint8_t *_rawDoneBuf;
    COMMON_ZERO_INIT static RawCaptureCallback _rawDoneCallback;
};

} // namespace ThetaGP::Test
