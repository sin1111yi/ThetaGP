/**
 * @file busmem.h
 * @brief BUS memory pool manager using MempoolManager
 *
 * Provides a shared memory pool for BUS operations:
 * - TX and RX buffers share a single pool
 */

#pragma once

#include "utils/mempool/mempoolmanager.h"

#include <cstddef>
#include <cstdint>

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

class BusMem {
public:
  BusMem();
  static BusMem &getInstance() {
    static BusMem instance;
    return instance;
  }

  bool init();

  void *allocTxBuffer(uint32_t size);
  void freeTxBuffer(void *ptr);

  void *allocRxBuffer(uint32_t size);
  void freeRxBuffer(void *ptr);

  Mempool::PoolStats txStats();
  Mempool::PoolStats rxStats();

  uint32_t totalAllocated();
  uint32_t totalFree();

private:
  bool _initialized = false;
  ThetaGP::Mempool::PoolID _poolId = ThetaGP::Mempool::INVALID_POOL_ID;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
