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
 * The CRC-32 table and algorithm are taken from CRC32 by Christopher Baker
 * <https://christopherbaker.net>, MIT licensed (Copyright (c) 2013
 * Christopher Baker, SPDX-License-Identifier: MIT); that notice covers the
 * derived portions. The CRC-16 function was written for this project.
 */

#include "crc.h"

// Nibble wide lookup for the reflected CRC-32 polynomial 0xEDB88320. A byte
// wide table costs 1 KB of flash for about twice the speed, which a record of a
// few dozen bytes cannot spend.
static const uint32_t kCrc32Table[16] = {
  0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
  0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
  0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
  0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

uint32_t Crc::crc32Bytes(const uint8_t *bytes, size_t len) {
  uint32_t state = 0xFFFFFFFFu;

  for (size_t i = 0; i < len; i++) {
    uint8_t idx = static_cast<uint8_t>(state ^ bytes[i]);
    state = kCrc32Table[idx & 0x0F] ^ (state >> 4);

    idx = static_cast<uint8_t>(state ^ (bytes[i] >> 4));
    state = kCrc32Table[idx & 0x0F] ^ (state >> 4);
  }

  return ~state;
}

uint16_t Crc::crc16CcittBytes(const uint8_t *bytes, size_t len) {
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= static_cast<uint16_t>(bytes[i]) << 8;

    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }

  return crc;
}
