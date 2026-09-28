/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "wire/frame_codec.h"

#include <cstring>

namespace ThetaGP::Test {

// A payload byte is added to the running sum as it arrives and the frame is
// held against its checksum at the checksum's last byte, so a whole frame
// reaches the layer above in one pass and the assembler is free for the frame
// after it. What the feed never does is read a message, write a reply or take
// a decision about a command: a payload leaves this class as bytes.
//
// The sum could equally be scanned over the payload once the frame is whole
// (addition is associative, so the two agree byte for byte). Adding it here
// costs one addition per byte, which is the arithmetic section 3.3 gives for
// the scan, and is what keeps a verified frame from having to wait for the
// main loop before the assembler can take the next one.

FrameCodec &FrameCodec::getInstance() {
    static FrameCodec instance;
    return instance;
}

// ── the send side ──

uint16_t FrameCodec::checksum(const uint8_t *payload, uint16_t length) {
    uint16_t sum = 0;
    for (uint16_t i = 0; i < length; ++i) {
        sum = static_cast<uint16_t>(sum + payload[i]);
    }
    return sum;
}

uint8_t FrameCodec::prefixSize(uint16_t length) {
    return length < 0x80u ? 1 : 2;
}

uint16_t FrameCodec::encode(const uint8_t *payload, uint16_t length,
                            uint8_t *out, uint16_t capacity) {
    if (out == nullptr || length > PAYLOAD_MAX) {
        return 0;
    }
    if (length > 0 && payload == nullptr) {
        return 0;
    }

    const uint8_t prefix = prefixSize(length);
    if (static_cast<uint32_t>(prefix) + length + CHECKSUM_BYTES > capacity) {
        return 0;
    }

    uint16_t used = 0;
    if (prefix == 1) {
        out[used] = static_cast<uint8_t>(length);
        ++used;
    } else {
        out[used] = static_cast<uint8_t>((length & 0x7Fu) | 0x80u);
        out[used + 1] = static_cast<uint8_t>((length >> 7) & 0x7Fu);
        used = 2;
    }

    for (uint16_t i = 0; i < length; ++i) {
        out[used] = payload[i];
        ++used;
    }

    const uint16_t sum = checksum(payload, length);
    out[used] = static_cast<uint8_t>(sum & 0xFFu);
    out[used + 1] = static_cast<uint8_t>((sum >> 8) & 0xFFu);
    used = static_cast<uint16_t>(used + CHECKSUM_BYTES);
    return used;
}

// ── the receive side ──

void FrameCodec::feed(const uint8_t *bytes, uint16_t length, uint32_t nowMs) {
    if (bytes == nullptr) {
        return;
    }
    for (uint16_t i = 0; i < length; ++i) {
        feedByte(bytes[i], nowMs);
    }
}

void FrameCodec::feedByte(uint8_t byte, uint32_t nowMs) {
    _lastByteMs = nowMs;

    switch (_state) {
    case State::Prefix: {
        // A third prefix byte declares a length of at least 128 * 128, which is
        // above PAYLOAD_MAX whatever the byte's own bits say, so it is refused
        // where it arrives rather than decoded and then refused (section 3.1).
        if (_prefixLen == PREFIX_MAX_BYTES) {
            drop(Drop::Prefix);
            return;
        }
        _prefix[_prefixLen] = byte;
        ++_prefixLen;
        if ((byte & 0x80u) != 0) {
            return; // the length continues in the byte after this one
        }

        uint16_t declared = 0;
        for (uint8_t i = 0; i < _prefixLen; ++i) {
            declared = static_cast<uint16_t>(
                declared |
                (static_cast<uint16_t>(_prefix[i] & 0x7Fu) << (7u * i)));
        }
        if (declared > PAYLOAD_MAX) {
            drop(Drop::Length);
            return;
        }

        _length = declared;
        _used = 0;
        _sum = 0;
        _checksumLen = 0;
        _state = declared == 0 ? State::Checksum : State::Payload;
        return;
    }

    case State::Payload:
        _payload[_used] = byte;
        ++_used;
        _sum = static_cast<uint16_t>(_sum + byte);
        if (_used == _length) {
            _state = State::Checksum;
        }
        return;

    case State::Checksum:
        _checksum[_checksumLen] = byte;
        ++_checksumLen;
        if (_checksumLen < CHECKSUM_BYTES) {
            return;
        }
        if (checksumOfChecksumBytes() != _sum) {
            // The payload is never read and never travels: the frame is refused
            // whole and the byte after it is read as a new length prefix
            // (section 3.4 rules 1 and 3).
            drop(Drop::Checksum);
            return;
        }
        publish();
        return;
    }
}

uint16_t FrameCodec::checksumOfChecksumBytes() const {
    return static_cast<uint16_t>(_checksum[0] |
                                 (static_cast<uint16_t>(_checksum[1]) << 8));
}

bool FrameCodec::tick(uint32_t nowMs) {
    // Only a length that was declared can be half a frame: a prefix that
    // promised another prefix byte declares nothing yet and is left where it is
    // (section 3.4 rule 5).
    if (!halfFrame()) {
        return false;
    }
    if (static_cast<uint32_t>(nowMs - _lastByteMs) < IDLE_TIMEOUT_MS) {
        return false;
    }
    drop(Drop::Idle);
    return true;
}

bool FrameCodec::take(Payload &out) {
    if (_slotCount == 0) {
        return false;
    }
    const Slot &slot = _slots[_slotHead];
    out.bytes = slot.bytes;
    out.length = slot.length;
    _slotHead = static_cast<uint8_t>((_slotHead + 1) % SLOTS);
    --_slotCount;
    return true;
}

uint8_t FrameCodec::queued() const {
    return _slotCount;
}

uint32_t FrameCodec::droppedFrames() const {
    return _dropped;
}

FrameCodec::Drop FrameCodec::lastDrop() const {
    return _lastDrop;
}

FrameCodec::State FrameCodec::state() const {
    return _state;
}

bool FrameCodec::halfFrame() const {
    return _state != State::Prefix;
}

uint16_t FrameCodec::declaredLength() const {
    return _length;
}

// ── the queue of whole frames and the refusals ──

void FrameCodec::publish() {
    if (_slotCount == SLOTS) {
        // A frame that is whole and whose checksum held, with every slot taken:
        // the frame is refused and counted, and the stream goes on at the byte
        // after it rather than at the byte after its payload.
        drop(Drop::NoRoom);
        return;
    }

    Slot &slot = _slots[(_slotHead + _slotCount) % SLOTS];
    slot.length = _length;
    for (uint16_t i = 0; i < _length; ++i) {
        slot.bytes[i] = _payload[i];
    }
    ++_slotCount;
    reset();
}

void FrameCodec::drop(Drop reason) {
    ++_dropped;
    _lastDrop = reason;
    reset();
}

void FrameCodec::reset() {
    _state = State::Prefix;
    _prefixLen = 0;
    _prefix[0] = 0;
    _prefix[1] = 0;
    _length = 0;
    _used = 0;
    _sum = 0;
    _checksum[0] = 0;
    _checksum[1] = 0;
    _checksumLen = 0;
}

} // namespace ThetaGP::Test
