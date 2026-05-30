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

  [[nodiscard]] static PoolID createPool(void *memory, size_t size, const char *name);
  [[nodiscard]] static PoolError destroyPool(PoolID poolId);

  [[nodiscard]] static Mempool *pool(PoolID poolId);
  [[nodiscard]] static const char *poolName(PoolID poolId);

  [[nodiscard]] static void *alloc(PoolID poolId, size_t size);
  [[nodiscard]] static PoolError free(PoolID poolId, void *ptr);

  [[nodiscard]] static PoolStats poolStats(PoolID poolId);
  [[nodiscard]] static size_t totalAllocated();
  [[nodiscard]] static size_t totalFree();
  [[nodiscard]] static uint16_t poolCount();

private:
  static MempoolEntry _entries[MAX_POOLS];
  static bool _initialized;

  static MempoolEntry *findFreeEntry();
};

} // namespace ThetaGP::Mempool
