#pragma once

#include "utils/mempool/mempool.h"
#include <cstdint>
#include <cstring>

namespace ThetaGP::Mempool {

using PoolID = uint8_t;
constexpr PoolID INVALID_POOL_ID = 0;

struct MempoolEntry {
  Mempool pool;
  bool inUse = false;
  char name[16]{};
};

class MempoolManager {
public:
  static constexpr uint16_t MAX_POOLS = 8;

  MempoolManager() = delete;

  static void init();

  static PoolID createPool(void *memory, size_t size, const char *name);
  static PoolError destroyPool(PoolID poolId);

  static Mempool *pool(PoolID poolId);
  static const char *poolName(PoolID poolId);

  static void *alloc(PoolID poolId, size_t size);
  static PoolError free(PoolID poolId, void *ptr);

  static PoolStats poolStats(PoolID poolId);
  static size_t totalAllocated();
  static size_t totalFree();
  static uint16_t poolCount();

private:
  static MempoolEntry _entries[MAX_POOLS];
  static bool _initialized;

  static MempoolEntry *findFreeEntry();
};

} // namespace ThetaGP::Mempool
