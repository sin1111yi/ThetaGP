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
