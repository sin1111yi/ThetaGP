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
  /**
   * @brief Pool identifiers for BUS memory pools
   */
  enum class PoolId : uint16_t {
    TxBuffer = 0x4255,    // 'BU' - TX buffer pool
    RxBuffer,             // RX buffer pool
  };
  
  /**
   * @brief Pool names for identification
   */
  static constexpr const char* TX_POOL_NAME = "BUS_TX";
  static constexpr const char* RX_POOL_NAME = "BUS_RX";
  
  /**
   * @brief Default pool sizes
   */
  static constexpr size_t TX_DEFAULT_SIZE = 1024;
  static constexpr size_t RX_DEFAULT_SIZE = 1024;
  
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
  static bool init(void *txMemory, size_t txSize,
                   void *rxMemory, size_t rxSize);
  
  /**
   * @brief Initialize with default sizes from single memory region
   * 
   * Partitions a single memory region into two pools with default sizes.
   * 
   * @param memory Pointer to memory region
   * @param totalSize Total size of memory region
   * @return true if initialization successful
   */
  static bool initDefault(void *memory, size_t totalSize);
  
  /**
   * @brief Deinitialize all BUS pools
   */
  static void deinit();
  
  /**
   * @brief Check if pools are initialized
   */
  static bool isInitialized();
  
  // TX Buffer operations
  static void *allocTxBuffer(size_t size);
  static void freeTxBuffer(void *ptr);
  
  // RX Buffer operations
  static void *allocRxBuffer(size_t size);
  static void freeRxBuffer(void *ptr);
  
  // Statistics
  static Mempool::PoolStats txStats();
  static Mempool::PoolStats rxStats();
  
  static size_t totalAllocated();
  static size_t totalFree();
  
private:
  static bool _initialized;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
