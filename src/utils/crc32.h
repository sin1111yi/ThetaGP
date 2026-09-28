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

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Streaming CRC-32 checksum, reflected, polynomial 0xEDB88320.
 *
 * Feed bytes with update(), read the result with finalize(). The table is
 * nibble wide (16 entries, 64 B of rodata): a byte wide table costs 1 KB of
 * flash for about twice the speed, which a frame of a few dozen bytes cannot
 * spend.
 */
class CRC32 {
public:
  CRC32() = default;

  /// @brief Restarts the calculation.
  void reset();

  /// @brief Adds one byte.
  void update(uint8_t data);

  /// @brief Adds one value of the given type.
  template <typename Type>
  void update(const Type &data) {
    update(&data, 1);
  }

  /// @brief Adds size elements of the given type.
  /// @param data The array to read.
  /// @param size Number of elements in the array.
  template <typename Type>
  void update(const Type *data, size_t size) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    const size_t nBytes = size * sizeof(Type);

    for (size_t i = 0; i < nBytes; i++) {
      update(bytes[i]);
    }
  }

  /// @returns the checksum of everything fed so far.
  uint32_t finalize() const;

  /// @brief Checksum of an array in one call.
  /// @param data The array to read.
  /// @param size Number of elements in the array (1 by default).
  /// @returns the calculated checksum.
  template <typename Type>
  static uint32_t calculate(const Type *data, size_t size = 1) {
    CRC32 crc;
    crc.update(data, size);
    return crc.finalize();
  }

private:
  uint32_t _state = 0xFFFFFFFFu;
};
