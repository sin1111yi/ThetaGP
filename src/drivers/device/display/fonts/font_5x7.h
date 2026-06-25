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

namespace ThetaGP::Drivers::Device::DisplayDrv {

/// Minimal 5×7 pixel bitmap font (1 bpp, monospace).
/// Glyphs: ASCII 0x20–0x7E. Each glyph is 5 bytes (5 columns, top-to-bottom).
struct Font {
  static constexpr uint8_t kWidth = 5;
  static constexpr uint8_t kHeight = 7;
  static constexpr uint8_t kBytesPerGlyph = 5;
  static constexpr char kFirstChar = 0x20;
  static constexpr char kLastChar = 0x7E;

  /// Return pointer to the 5-byte glyph bitmap, or nullptr.
  static const uint8_t *getGlyph(char c);
};

} // namespace ThetaGP::Drivers::Device::DisplayDrv
