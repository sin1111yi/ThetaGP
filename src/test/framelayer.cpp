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

#ifdef THETAGP_ENABLE_TEST_API

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
    case RxState::IDLE: {
        // Any byte starts a new frame
        _rxLen = 0;
        _rxBuf[_rxLen++] = static_cast<char>(byte);
        _rxState = RxState::DATA;
        break;
    }

    case RxState::DATA: {
        if (byte == '\r') {
            _rxState = RxState::CR;
        } else if (byte == '\n') {
            // Frame complete
            _rxBuf[_rxLen] = '\0';
            if (_frameCallback && _rxLen > 0) {
                LOG_DEBUG("FrameLayer: RX frame (%uB): %.*s",
                       _rxLen, _rxLen > 60 ? 60 : _rxLen, _rxBuf);
                _frameCallback(_rxBuf);
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
            // CRLF complete
            _rxBuf[_rxLen] = '\0';
            if (_frameCallback && _rxLen > 0) {
                LOG_DEBUG("FrameLayer: RX frame (%uB): %.*s",
                       _rxLen, _rxLen > 60 ? 60 : _rxLen, _rxBuf);
                _frameCallback(_rxBuf);
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

#endif // THETAGP_ENABLE_TEST_API

} // namespace ThetaGP::Test
