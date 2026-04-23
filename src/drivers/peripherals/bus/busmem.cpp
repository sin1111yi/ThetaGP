/**
 * @file busmem.cpp
 * @brief BUS memory pool manager implementation
 */

#include "build_info.h"

#include "drivers/peripherals/bus/busmem.h"

#include <cstring>

using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Mempool;

#define BUS_TX_POOL_SIZE 2048
#define BUS_RX_POOL_SIZE 2048

COMMON_CODE static uint8_t BusMemPool[BUS_TX_POOL_SIZE + BUS_RX_POOL_SIZE + 16];

BusMem::BusMem()
    : _initialized(false), _mem(static_cast<uint8_t *>(BusMemPool)) {}

bool BusMem::init() {
  return initPool(_mem, BUS_TX_POOL_SIZE, _mem + BUS_TX_POOL_SIZE,
                  BUS_RX_POOL_SIZE);
}

bool BusMem::initPool(void *txMemory, uint32_t txSize, void *rxMemory,
                      uint32_t rxSize) {

  MempoolManager &manager = MempoolManager::getInstance();

  // Add TX buffer pool
  PoolError txErr = manager.addPool(static_cast<uint16_t>(PoolId::TxBuffer),
                                    txMemory, txSize, "BusTxPool");
  if (txErr != PoolError::OK) {
    return false;
  }

  // Add RX buffer pool
  PoolError rxErr = manager.addPool(static_cast<uint16_t>(PoolId::RxBuffer),
                                    rxMemory, rxSize, "BusRxPool");
  if (rxErr != PoolError::OK) {
    manager.removePool(static_cast<uint16_t>(PoolId::TxBuffer));
    return false;
  }

  _initialized = true;
  return true;
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

void *BusMem::allocTxBuffer(uint32_t size) {
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

void *BusMem::allocRxBuffer(uint32_t size) {
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

uint32_t BusMem::totalAllocated() {
  return MempoolManager::getInstance().totalAllocated();
}

uint32_t BusMem::totalFree() {
  return MempoolManager::getInstance().totalFree();
}
