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

#ifndef _DRIVER_HELPER_H_
#define _DRIVER_HELPER_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern "C" {

static inline uint16_t *getStringDescriptor(const char *value, uint8_t index) {
  static uint16_t
      descriptorStringBuffer[128]; // Max 256 bytes, 127 unicode characters
  size_t charCount;
  if (index == 0) {
    // The language descriptor is not text: its two bytes are the one language
    // id it carries, and it is the four bytes its header says it is.
    auto *bytes = reinterpret_cast<uint8_t *>(descriptorStringBuffer);
    bytes[0] = 4;
    bytes[1] = 0x03;
    bytes[2] = static_cast<uint8_t>(value[0]);
    bytes[3] = static_cast<uint8_t>(value[1]);
    return descriptorStringBuffer;
  }
  charCount = strlen(value);
  if (charCount > 127)
    charCount = 127;
  // Fill descriptionStringBuffer[1] .. [32]
  for (uint8_t i = 0; i < charCount; i++)
    descriptorStringBuffer[i + 1] = value[i];

  // first byte (descriptionStringBuffer[0]) is length (including header),
  // second byte is string type
  descriptorStringBuffer[0] = (0x03 << 8) | (2 * (uint8_t)charCount + 2);

  // Cast temp buffer to final result
  return descriptorStringBuffer;
}
}

#endif // _DRIVER_HELPER_H_
