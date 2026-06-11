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

#include "test/framelayer.h"

#include "utils/log/log.h"
#include "tusb.h"

#include <cstring>

namespace ThetaGP::Test {

// ── Static member definitions ──

COMMON_ZERO_INIT char FrameLayer::_cmdQueue[CMD_QUEUE_SIZE][CMD_QUEUE_FRAME_MAX]{};
COMMON_ZERO_INIT volatile uint8_t FrameLayer::_cmdHead = 0;
uint8_t FrameLayer::_cmdTail = 0;

COMMON_ZERO_INIT volatile bool FrameLayer::_rawDone = false;
COMMON_ZERO_INIT volatile uint16_t FrameLayer::_rawDoneLen = 0;
COMMON_ZERO_INIT uint8_t *FrameLayer::_rawDoneBuf = nullptr;
COMMON_ZERO_INIT FrameLayer::RawCaptureCallback FrameLayer::_rawDoneCallback = nullptr;

// ── FrameLayer implementation ──

FrameLayer &FrameLayer::getInstance() {
    static FrameLayer instance;
    return instance;
}

void FrameLayer::cdcRxHandler(void *buffer, uint16_t len) {
    auto *bytes = static_cast<uint8_t *>(buffer);
    LOG_DEBUG("FrameLayer: CDC RX %u bytes", len);
    auto &instance = getInstance();
    for (uint16_t i = 0; i < len; ++i) {
        instance.processByte(bytes[i]);
    }
}

void FrameLayer::processByte(uint8_t byte) {
    switch (_rxState) {
    case RxState::RAW: {
        // RAW capture mode: accumulate bytes without interpretation
        if (_rawBuf && _rxLen < _rawTargetLen) {
            _rawBuf[_rxLen++] = byte;
            if (_rxLen >= _rawTargetLen) {
                // Capture complete — defer callback to main loop
                _rawDone = true;
                _rawDoneLen = _rxLen;
                _rawDoneBuf = _rawBuf;
                _rawDoneCallback = _rawCallback;
                _rxLen = 0;
                _rxState = RxState::IDLE;
                _rawBuf = nullptr;
                _rawTargetLen = 0;
                // NOTE: _rawCallback intentionally NOT cleared here —
                //       processCommandQueue reads it via _rawDoneCallback
            }
        } else {
            // Safety fallback
            _rxLen = 0;
            _rxState = RxState::IDLE;
            _rawBuf = nullptr;
            _rawTargetLen = 0;
            _rawCallback = nullptr;
        }
        break;
    }

    case RxState::IDLE: {
        // Skip stray CR/LF — these are just leftover terminators
        if (byte == '\n' || byte == '\r') {
            break;
        }
        // Any other byte starts a new frame
        _rxLen = 0;
        _rxBuf[_rxLen++] = static_cast<char>(byte);
        _rxState = RxState::DATA;
        break;
    }

    case RxState::DATA: {
        if (byte == '\r') {
            _rxState = RxState::CR;
        } else if (byte == '\n') {
            // Frame complete — enqueue instead of calling callback directly
            _rxBuf[_rxLen] = '\0';
            if (_rxLen > 0) {
                LOG_DEBUG("FrameLayer: RX frame (%uB): %.*s",
                       _rxLen, _rxLen > 60 ? 60 : _rxLen, _rxBuf);
                uint8_t next = (_cmdHead + 1) & (CMD_QUEUE_SIZE - 1);
                if (next != _cmdTail) {
                    memcpy(_cmdQueue[_cmdHead], _rxBuf, _rxLen + 1);
                    _cmdHead = next;
                } else {
                    LOG_WARN("FrameLayer: command queue full, dropping frame");
                }
            }
            _rxLen = 0;
            _rxState = RxState::IDLE;
        } else {
            if (_rxLen < sizeof(_rxBuf) - 1) {
                _rxBuf[_rxLen++] = static_cast<char>(byte);
            } else {
                LOG_WARN("FrameLayer: RX buffer overflow, resetting");
                _rxLen = 0;
                _rxState = RxState::IDLE;
            }
        }
        break;
    }

    case RxState::CR: {
        if (byte == '\n') {
            // CRLF complete — enqueue instead of calling callback directly
            _rxBuf[_rxLen] = '\0';
            if (_rxLen > 0) {
                LOG_DEBUG("FrameLayer: RX frame (%uB): %.*s",
                       _rxLen, _rxLen > 60 ? 60 : _rxLen, _rxBuf);
                uint8_t next = (_cmdHead + 1) & (CMD_QUEUE_SIZE - 1);
                if (next != _cmdTail) {
                    memcpy(_cmdQueue[_cmdHead], _rxBuf, _rxLen + 1);
                    _cmdHead = next;
                } else {
                    LOG_WARN("FrameLayer: command queue full, dropping frame");
                }
            }
            _rxLen = 0;
            _rxState = RxState::IDLE;
        } else {
            // Stray CR without LF -- treat the CR as data and continue
            if (_rxLen < sizeof(_rxBuf) - 2) {
                _rxBuf[_rxLen++] = '\r';
                _rxBuf[_rxLen++] = static_cast<char>(byte);
                _rxState = RxState::DATA;
            } else {
                LOG_WARN("FrameLayer: RX buffer overflow in CR state, resetting");
                _rxLen = 0;
                _rxState = RxState::IDLE;
            }
        }
        break;
    }
    }
}

void FrameLayer::processCommandQueue() {
    // 1. Process deferred RAW capture completion callback
    if (_rawDone) {
        _rawDone = false;
        auto *cb = _rawDoneCallback;
        auto len = _rawDoneLen;
        auto *buf = _rawDoneBuf;
        _rawDoneCallback = nullptr;
        _rawDoneBuf = nullptr;
        _rawDoneLen = 0;
        if (cb) {
            LOG_DEBUG("FrameLayer: RAW deferred callback len=%u", len);
            cb(buf, len);
        }
    }

    // 2. Dispatch all enqueued command frames
    while (_cmdHead != _cmdTail) {
        if (_frameCallback) {
            LOG_DEBUG("FrameLayer: dispatching queued cmd");
            _frameCallback(_cmdQueue[_cmdTail]);
        }
        _cmdTail = (_cmdTail + 1) & (CMD_QUEUE_SIZE - 1);
    }
}

void FrameLayer::sendResponse(const JsonDocument &doc) {
    _txPendingLen = serializeJson(doc, _txPendingBuf, sizeof(_txPendingBuf) - 2);
    if (_txPendingLen >= sizeof(_txPendingBuf) - 2) {
        _txPendingLen = 0;
        _txPending = false;
        LOG_WARN("FrameLayer: Response too large for TX buffer");
        return;
    }
    _txPendingBuf[_txPendingLen++] = '\r';
    _txPendingBuf[_txPendingLen++] = '\n';
    _txPending = true;
    _txPendingSent = 0;
    LOG_DEBUG("FrameLayer: TX queued (%u bytes)", _txPendingLen);
}

void FrameLayer::flushTx() {
    if (!_txPending) return;
    uint32_t remaining = static_cast<uint32_t>(_txPendingLen - _txPendingSent);
    uint32_t written = tud_cdc_write(
        _txPendingBuf + _txPendingSent,
        remaining);
    if (written < remaining) {
        // FIFO full — remaining bytes will be sent on next tick
        _txPendingSent += written;
        LOG_DEBUG("FrameLayer: TX partial %u/%u, %u remain",
                  written, _txPendingLen, _txPendingLen - _txPendingSent);
        return;
    }
    tud_cdc_write_flush();
    LOG_DEBUG("FrameLayer: TX done (%u bytes)", _txPendingLen);
    _txPending = false;
    _txPendingSent = 0;
}

void FrameLayer::setFrameCallback(FrameCallback cb) {
    _frameCallback = cb;
}

void FrameLayer::startRawCapture(uint8_t *buf, uint16_t len, RawCaptureCallback cb) {
    LOG_DEBUG("FrameLayer: startRawCapture len=%u", len);
    _rawBuf = buf;
    _rawTargetLen = len;
    _rawCallback = cb;
    _rxLen = 0;
    _rxState = RxState::RAW;
}

} // namespace ThetaGP::Test
