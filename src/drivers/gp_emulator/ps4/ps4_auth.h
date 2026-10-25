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

namespace ThetaGP::Drivers::GPEmulator {

// What a PS4 authentication answer carries besides the signature over the
// host's challenge: the serial the device reports, the RSA-2048 key it signs
// with, and the certificate signature over that serial and public key.
struct Ps4AuthKey {
  const uint8_t *serial = nullptr;      // 16 bytes
  const uint8_t *modulus = nullptr;     // 256 bytes, big endian
  const uint8_t *exponent = nullptr;    // 256 bytes, big endian
  const uint8_t *prime_p = nullptr;     // 128 bytes
  const uint8_t *prime_q = nullptr;     // 128 bytes
  const uint8_t *certificate = nullptr; // 256 bytes
};

// The key this build carries, or nullptr when it carries none. A device without
// one answers the authentication reports as an unauthenticated controller, which
// is what a host that does not check accepts.
[[nodiscard]] const Ps4AuthKey *ps4AuthKey();

// The PS4 authentication channel. The host writes a 256-byte challenge through
// feature report 0xF0 in five pages; the device answers with a 1064-byte answer
// read back in nineteen chunks: a PSS signature over the challenge's digest,
// the serial and public key it signed with, and the certificate signature that
// vouches for them.
class Ps4Auth {
public:
  void initialize();
  [[nodiscard]] bool isAvailable() const;

  // Signs a challenge the host has finished writing, if one is owed.
  void process();

  [[nodiscard]] uint16_t getReport(uint8_t report_id, uint8_t *buffer,
                                   uint16_t reqlen);
  void setReport(uint8_t report_id, uint8_t const *buffer, uint16_t size);
};

} // namespace ThetaGP::Drivers::GPEmulator
