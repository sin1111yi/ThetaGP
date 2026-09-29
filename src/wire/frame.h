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
 * be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A
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

namespace ThetaGP::Wire {

// One frame of the CDC wire: a varint length, the payload, and the payload's
// byte sum in the low 16 bits, little endian. The length counts the payload's
// bytes and neither the prefix nor the checksum; the checksum covers the
// payload's bytes and neither the prefix nor itself.
//
// The class carries no message and no message type: a payload is handed up as
// a pointer and a length, and what is inside it belongs to the layer above.
//
// The receive side is fed one byte or one buffer at a time and accumulates, so
// the USB interrupt feeds it and the main loop takes from it. A frame whose
// checksum does not hold is refused whole, and every refusal is counted in a
// counter nothing else clears.
class Frame {
public:
    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;
    Frame() = default;

    static Frame &getInstance();

    // Largest payload one frame carries.
    static constexpr uint16_t PAYLOAD_MAX = 1024;
    // Most bytes one frame takes on the wire: a two-byte length, the payload, the checksum.
    static constexpr uint16_t FRAME_MAX = 2 + PAYLOAD_MAX + 2;
    // Lengths are 0..PAYLOAD_MAX, so a prefix is one byte below 128 and two from 128 up.
    static constexpr uint8_t PREFIX_MAX_BYTES = 2;
    // Bytes the checksum takes, low byte first.
    static constexpr uint8_t CHECKSUM_BYTES = 2;
    // Whole frames the receive side holds for the layer above: one the frame layer is taking, the rest queued.
    static constexpr uint8_t SLOTS = 4;
    // Idle time a declared length is given to arrive in before the half frame is dropped.
    static constexpr uint32_t IDLE_TIMEOUT_MS = 1000;

    // Why a frame was refused. The codec names the step and carries no sentence.
    enum class Drop : uint8_t {
        None = 0, // Nothing has been refused.
        Prefix,   // A length prefix of more than PREFIX_MAX_BYTES bytes.
        Length,   // A length above PAYLOAD_MAX.
        Checksum, // The checksum is not the payload's sum.
        Idle,     // A declared length whose bytes stopped arriving.
        NoRoom,   // A whole frame with every slot taken.
    };

    // The step the receive side is in.
    enum class State : uint8_t {
        Prefix,   // Reading a length prefix.
        Payload,  // Reading the payload the prefix declared.
        Checksum, // Reading the checksum's two bytes.
    };

    // One frame's payload, as it is handed to the layer above.
    struct Payload {
        const uint8_t *bytes;
        uint16_t length;
    };

    // ── the send side ──

    // Sum of a payload's bytes in the low 16 bits, wrapping at 16 bits.
    static uint16_t checksum(const uint8_t *payload, uint16_t length);

    // Bytes a length takes as a prefix: 1 for a length below 128, 2 for one up
    // to PAYLOAD_MAX.
    static uint8_t prefixSize(uint16_t length);

    // Writes one frame into a buffer and returns its size in bytes, or 0 when
    // the payload is above PAYLOAD_MAX, a pointer is null, or out is too short
    // for this frame. A frame of an empty payload is 3 bytes, so 0 is no frame
    // rather than one.
    static uint16_t encode(const uint8_t *payload, uint16_t length,
                           uint8_t *out, uint16_t capacity);

    // ── the receive side ──

    // Feeds received bytes into the assembler, stamped with their arrival time
    // for the idle timeout of a half frame.
    void feed(const uint8_t *bytes, uint16_t length, uint32_t nowMs);

    // Feeds one received byte into the assembler, stamped with its arrival time.
    void feedByte(uint8_t byte, uint32_t nowMs);

    // The main loop's tick: drops a half frame whose bytes stopped arriving and
    // returns whether this tick dropped one.
    bool tick(uint32_t nowMs);

    // Takes the oldest whole frame whose checksum held and returns whether a
    // frame was taken. A frame is only ever taken after its checksum has been
    // held against it, and the bytes stay valid until the next frame is
    // published into that slot.
    bool take(Payload &out);

    // Whole frames waiting to be taken.
    uint8_t queued() const;

    // Frames refused since power-on; feeding, taking and resetting do not clear it.
    uint32_t droppedFrames() const;

    // Why the frame last refused was refused.
    Drop lastDrop() const;

    // The step the receive side is in.
    State state() const;

    // Whether a length is declared and its payload and checksum are not all in.
    bool halfFrame() const;

    // The length the prefix of a half frame declared.
    uint16_t declaredLength() const;

private:
    // One whole frame held for the layer above.
    struct Slot {
        uint16_t length;
        uint8_t bytes[PAYLOAD_MAX];
    };

    // Hand the assembled frame up and free the assembler for the next one.
    void publish();
    // Refuse what the assembler holds, count it, and read the next byte as a prefix.
    void drop(Drop reason);
    // Back to reading a length prefix. The slots and the counter are not touched.
    void reset();
    // The two checksum bytes just arrived, low byte first, as the number they are.
    uint16_t checksumOfChecksumBytes() const;

    Slot _slots[SLOTS] = {};
    uint8_t _slotHead = 0;
    uint8_t _slotCount = 0;

    State _state = State::Prefix;
    uint8_t _prefix[PREFIX_MAX_BYTES] = {0};
    uint8_t _prefixLen = 0;
    uint16_t _length = 0;
    uint8_t _payload[PAYLOAD_MAX] = {0};
    uint16_t _used = 0;
    uint16_t _sum = 0;
    uint8_t _checksum[CHECKSUM_BYTES] = {0};
    uint8_t _checksumLen = 0;
    uint32_t _lastByteMs = 0;

    uint32_t _dropped = 0;
    Drop _lastDrop = Drop::None;
};

} // namespace ThetaGP::Wire
