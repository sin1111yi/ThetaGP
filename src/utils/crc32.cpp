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

/*
 * Algorithm and table taken from CRC32 by Christopher Baker
 * <https://christopherbaker.net>, MIT licensed (Copyright (c) 2013
 * Christopher Baker, SPDX-License-Identifier: MIT); that notice covers the
 * derived portions.
 */

#include "crc32.h"

// Nibble wide lookup for the reflected polynomial 0xEDB88320.
static const uint32_t kCrc32Table[16] = {
  0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
  0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
  0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
  0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

void CRC32::reset() {
  _state = 0xFFFFFFFFu;
}

void CRC32::update(uint8_t data) {
  uint8_t idx = static_cast<uint8_t>(_state ^ data);
  _state = kCrc32Table[idx & 0x0F] ^ (_state >> 4);

  idx = static_cast<uint8_t>(_state ^ (data >> 4));
  _state = kCrc32Table[idx & 0x0F] ^ (_state >> 4);
}

uint32_t CRC32::finalize() const {
  return ~_state;
}
