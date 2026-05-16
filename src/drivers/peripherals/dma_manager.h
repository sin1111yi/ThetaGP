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

/**
 * @file dma_manager.h
 * @brief DMA stream allocation manager (singleton)
 *
 * DmaManager provides centralized allocation of DMA streams.
 * Each controller (DMA1, DMA2) has 8 streams (S0-S7).
 * The allocator tracks which streams are in use via a 16-bit
 * bitmask and hands out the first available stream.
 */

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

  /**
   * @brief Allocate a DMA stream on the given controller
   *
   * Finds the first free stream on the specified controller,
   * marks it as allocated, creates a new DmaChannel, and sets
   * the DMAMUX request ID.
   *
   * @param ctrl     DMA controller (Dma1 or Dma2)
   * @param requestId DMAMUX request number (e.g. DMA_REQUEST_USART1_TX)
   * @return DmaChannel* on success, nullptr if all streams on that
   *         controller are already allocated
   */
  DmaChannel *allocate(Controller ctrl, uint32_t requestId);

private:
  DmaManager() = default;
  uint16_t _allocated = 0; // bit 0-7: DMA1 S0-S7, bit 8-15: DMA2 S0-S7
};

} // namespace ThetaGP::Drivers::Peripheral::DMA
