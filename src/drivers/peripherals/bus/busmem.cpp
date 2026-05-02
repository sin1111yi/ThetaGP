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
 * @file busmem.cpp
 * @brief BUS memory pool manager implementation
 */

#include "build_info.h"

#include "drivers/peripherals/bus/busmem.h"

using namespace ThetaGP::Drivers::Peripheral::BUS;

#define BUS_MEMPOOL_SIZE (2048 + 2048 + 16)

COMMON_CODE static uint8_t s_BusMempool[BUS_MEMPOOL_SIZE];

BusMem::BusMem() {}

bool BusMem::init() {
  _poolId = ThetaGP::Mempool::MempoolManager::createPool(
      s_BusMempool, BUS_MEMPOOL_SIZE, "BusPool");
  if (_poolId == ThetaGP::Mempool::INVALID_POOL_ID) {
    return false;
  }

  _initialized = true;
  return true;
}

void *BusMem::allocTxBuffer(uint32_t size) {
  return ThetaGP::Mempool::MempoolManager::alloc(_poolId, size);
}

void BusMem::freeTxBuffer(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  ThetaGP::Mempool::MempoolManager::free(_poolId, ptr);
}

void *BusMem::allocRxBuffer(uint32_t size) {
  return ThetaGP::Mempool::MempoolManager::alloc(_poolId, size);
}

void BusMem::freeRxBuffer(void *ptr) {
  if (ptr == nullptr) {
    return;
  }
  ThetaGP::Mempool::MempoolManager::free(_poolId, ptr);
}

ThetaGP::Mempool::PoolStats BusMem::txStats() {
  return ThetaGP::Mempool::MempoolManager::poolStats(_poolId);
}

ThetaGP::Mempool::PoolStats BusMem::rxStats() {
  return ThetaGP::Mempool::MempoolManager::poolStats(_poolId);
}

uint32_t BusMem::totalAllocated() {
  ThetaGP::Mempool::PoolStats s = ThetaGP::Mempool::MempoolManager::poolStats(_poolId);
  return s.usedSize;
}

uint32_t BusMem::totalFree() {
  ThetaGP::Mempool::PoolStats s = ThetaGP::Mempool::MempoolManager::poolStats(_poolId);
  return s.freeSize;
}
