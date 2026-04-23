/**
 * @file busmem.h
 * @brief BUS memory pool manager using MempoolManager
 *
 * Provides dedicated memory pools for BUS operations:
 * - TX buffer pool: For transmission data
 * - RX buffer pool: For reception data
 */

#pragma once

#include "utils/mempool/mempoolmanager.h"

#include <cstddef>
#include <cstdint>

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

/**
 * @brief BUS memory pool manager
 *
 * Wrapper around MempoolManager that provides BUS-specific
 * memory allocation interfaces.
 */
class BusMem {
public:
  BusMem();
  static BusMem &getInstance() {
    static BusMem instance;
    return instance;
  }
  /**
   * @brief Pool identifiers for BUS memory pools
   */
  enum class PoolId : uint16_t {
    TxBuffer = 0x4255, // 'BU' - TX buffer pool
    RxBuffer,          // RX buffer pool
  };

  bool init();

  /**
   * @brief Check if pools are initialized
   */
  bool isInitialized();

  // TX Buffer operations
  void *allocTxBuffer(uint32_t size);
  void freeTxBuffer(void *ptr);

  // RX Buffer operations
  void *allocRxBuffer(uint32_t size);
  void freeRxBuffer(void *ptr);

  // Statistics
  Mempool::PoolStats txStats();
  Mempool::PoolStats rxStats();

  uint32_t totalAllocated();
  uint32_t totalFree();

private:
  bool _initialized;
  uint8_t* _mem;
  /**
   * @brief Initialize BUS memory pools
   *
   * Creates two pools in the global MempoolManager:
   * - TX buffer pool
   * - RX buffer pool
   *
   * @param txMemory Memory region for TX pool
   * @param txSize Size of TX memory region
   * @param rxMemory Memory region for RX pool
   * @param rxSize Size of RX memory region
   * @return true if all pools created successfully
   */
  bool initPool(void *txMemory, uint32_t txSize, void *rxMemory, uint32_t rxSize);
  /**
   * @brief Deinitialize all BUS pools
   */
  void deinit();
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
