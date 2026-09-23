/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 *
 * @file dma_manager.cpp
 * @brief DmaManager allocation implementation
 */

#include "drivers/peripherals/dma_manager.h"

using namespace ThetaGP::Drivers::Peripheral::DMA;

DmaChannel *DmaManager::allocate(Controller ctrl, uint32_t requestId) {
  // Determine bit range for the requested controller
  // DMA1: bits 0-7, DMA2: bits 8-15
  const uint8_t shift =
      (ctrl == Controller::Dma1) ? 0 : 8;
  // Find first free stream in the controller's bit range
  uint16_t occupied = (_allocated >> shift) & 0x00FF;
  if (occupied == 0x00FF) {
    // All 8 streams on this controller are in use
    return nullptr;
  }

  // Find the first zero bit
  uint8_t streamIdx = 0;
  while ((occupied >> streamIdx) & 1) {
    streamIdx++;
  }

  // Mark as allocated
  _allocated |= (1 << (shift + streamIdx));

  // Create and configure the channel
  auto stream = static_cast<Stream>(streamIdx);
  auto *ch = new DmaChannel(ctrl, stream);
  ch->setRequestId(requestId);

  return ch;
}
