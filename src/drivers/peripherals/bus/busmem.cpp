/**
 * @file busmem.cpp
 * @brief BUS memory pool manager implementation
 */

#include "drivers/peripherals/bus/busmem.h"

#include <cstring>

using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Mempool;

bool BusMem::_initialized = false;

bool BusMem::init(void *txMemory, size_t txSize, void *rxMemory,
                  size_t rxSize) {

  MempoolManager &manager = MempoolManager::getInstance();

  // Add TX buffer pool
  PoolError txErr = manager.addPool(static_cast<uint16_t>(PoolId::TxBuffer),
                                    txMemory, txSize, TX_POOL_NAME);
  if (txErr != PoolError::OK) {
    return false;
  }

  // Add RX buffer pool
  PoolError rxErr = manager.addPool(static_cast<uint16_t>(PoolId::RxBuffer),
                                    rxMemory, rxSize, RX_POOL_NAME);
  if (rxErr != PoolError::OK) {
    manager.removePool(static_cast<uint16_t>(PoolId::TxBuffer));
    return false;
  }

  _initialized = true;
  return true;
}

bool BusMem::initDefault(void *memory, size_t totalSize) {
  if (memory == nullptr || totalSize < TX_DEFAULT_SIZE + RX_DEFAULT_SIZE) {
    return false;
  }

  auto *mem = static_cast<uint8_t *>(memory);

  return init(mem, TX_DEFAULT_SIZE, mem + TX_DEFAULT_SIZE, RX_DEFAULT_SIZE);
}

void BusMem::deinit() {
  if (!_initialized) {
    return;
  }

  MempoolManager &manager = MempoolManager::getInstance();

  manager.removePool(static_cast<uint16_t>(PoolId::TxBuffer));
  manager.removePool(static_cast<uint16_t>(PoolId::RxBuffer));

  _initialized = false;
}

bool BusMem::isInitialized() { return _initialized; }

void *BusMem::allocTxBuffer(size_t size) {
  if (!_initialized) {
    return nullptr;
  }
  return MempoolManager::getInstance().alloc(
      static_cast<uint16_t>(PoolId::TxBuffer), size);
}

void BusMem::freeTxBuffer(void *ptr) {
  if (!_initialized || ptr == nullptr) {
    return;
  }
  MempoolManager::getInstance().free(static_cast<uint16_t>(PoolId::TxBuffer),
                                     ptr);
}

void *BusMem::allocRxBuffer(size_t size) {
  if (!_initialized) {
    return nullptr;
  }
  return MempoolManager::getInstance().alloc(
      static_cast<uint16_t>(PoolId::RxBuffer), size);
}

void BusMem::freeRxBuffer(void *ptr) {
  if (!_initialized || ptr == nullptr) {
    return;
  }
  MempoolManager::getInstance().free(static_cast<uint16_t>(PoolId::RxBuffer),
                                     ptr);
}

PoolStats BusMem::txStats() {
  return MempoolManager::getInstance().poolStats(
      static_cast<uint16_t>(PoolId::TxBuffer));
}

PoolStats BusMem::rxStats() {
  return MempoolManager::getInstance().poolStats(
      static_cast<uint16_t>(PoolId::RxBuffer));
}

size_t BusMem::totalAllocated() {
  return MempoolManager::getInstance().totalAllocated();
}

size_t BusMem::totalFree() { return MempoolManager::getInstance().totalFree(); }
