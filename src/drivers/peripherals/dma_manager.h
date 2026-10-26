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

// Hands out DMA streams. Each controller has eight (S0 to S7), and which ones
// are taken is tracked in a 16-bit bitmask.

#pragma once

#include "drivers/peripherals/dma.h"

#include <cstdint>

namespace ThetaGP::Drivers::Peripheral::DMA {

class DmaManager {
public:
  static DmaManager &getInstance() {
    static DmaManager instance;
    return instance;
  }

  // Takes the first free stream on the controller, sets its DMAMUX request, and
  // reports nullptr when the controller has none left. The caller owns the
  // channel: a write to one in use, or to an unknown request id, lands on a
  // channel it does not own.
  [[nodiscard]] DmaChannel *allocate(Controller ctrl, uint32_t requestId);

private:
  DmaManager() = default;
  uint16_t _allocated = 0; // bit 0-7: DMA1 S0-S7, bit 8-15: DMA2 S0-S7
};

} // namespace ThetaGP::Drivers::Peripheral::DMA
