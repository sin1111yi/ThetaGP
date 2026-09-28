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

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Checksums over a byte range: CRC-32 and CRC-16/CCITT.
 *
 * Both are pure functions of the bytes they are given, so the variant is chosen
 * by name rather than by which helper file a caller happens to include.
 */
class Crc {
public:
  /**
   * @brief CRC-32: reflected, polynomial 0xEDB88320, initial 0xFFFFFFFF, final
   * XOR.
   * @param data Array to read.
   * @param size Number of elements in the array (1 by default).
   * @returns the checksum.
   */
  template <typename Type>
  static uint32_t crc32(const Type *data, size_t size = 1) {
    return crc32Bytes(reinterpret_cast<const uint8_t *>(data),
                      size * sizeof(Type));
  }

  /**
   * @brief CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF, not
   * reflected, no final XOR. This is the variant the BootMeta field carries.
   * @param data Array to read.
   * @param size Number of elements in the array (1 by default).
   * @returns the checksum.
   */
  template <typename Type>
  static uint16_t crc16Ccitt(const Type *data, size_t size = 1) {
    return crc16CcittBytes(reinterpret_cast<const uint8_t *>(data),
                           size * sizeof(Type));
  }

private:
  static uint32_t crc32Bytes(const uint8_t *bytes, size_t len);
  static uint16_t crc16CcittBytes(const uint8_t *bytes, size_t len);
};
