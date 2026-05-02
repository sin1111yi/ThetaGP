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

#include "utils/mempool/mempoolmanager.h"

using namespace ThetaGP::Mempool;

MempoolEntry MempoolManager::_entries[MAX_POOLS]{};
bool MempoolManager::_initialized = false;

void MempoolManager::init() {
  if (_initialized) {
    return;
  }

  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    _entries[i].pool.deinit();
    _entries[i].inUse = false;
    std::memset(_entries[i].name, 0, sizeof(_entries[i].name));
  }

  _initialized = true;
}

PoolID MempoolManager::createPool(void *memory, size_t size,
                                   const char *name) {
  if (!_initialized) {
    return INVALID_POOL_ID;
  }

  MempoolEntry *entry = findFreeEntry();
  if (entry == nullptr) {
    return INVALID_POOL_ID;
  }

  PoolError result = entry->pool.init(memory, size);
  if (result != PoolError::OK) {
    return INVALID_POOL_ID;
  }

  uint16_t idx = static_cast<uint16_t>(entry - _entries);
  entry->inUse = true;
  if (name != nullptr) {
    std::strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
  }
  return static_cast<PoolID>(idx + 1);
}

PoolError MempoolManager::destroyPool(PoolID poolId) {
  if (!_initialized || poolId == INVALID_POOL_ID || poolId > MAX_POOLS) {
    return PoolError::NotInitialized;
  }

  MempoolEntry *entry = &_entries[poolId - 1];
  if (!entry->inUse) {
    return PoolError::NotInitialized;
  }

  entry->pool.deinit();
  entry->inUse = false;
  std::memset(entry->name, 0, sizeof(entry->name));

  return PoolError::OK;
}

Mempool *MempoolManager::pool(PoolID poolId) {
  if (!_initialized || poolId == INVALID_POOL_ID || poolId > MAX_POOLS || !_entries[poolId - 1].inUse) {
    return nullptr;
  }
  return &_entries[poolId - 1].pool;
}

const char *MempoolManager::poolName(PoolID poolId) {
  if (!_initialized || poolId == INVALID_POOL_ID || poolId > MAX_POOLS || !_entries[poolId - 1].inUse) {
    return nullptr;
  }
  return _entries[poolId - 1].name;
}

void *MempoolManager::alloc(PoolID poolId, size_t size) {
  Mempool *p = pool(poolId);
  return p != nullptr ? p->alloc(size) : nullptr;
}

PoolError MempoolManager::free(PoolID poolId, void *ptr) {
  Mempool *p = pool(poolId);
  return p != nullptr ? p->free(ptr) : PoolError::NotInitialized;
}

PoolStats MempoolManager::poolStats(PoolID poolId) {
  static const PoolStats empty = {0, 0, 0, 0, 0};

  Mempool *p = pool(poolId);
  return p != nullptr ? p->stats() : empty;
}

uint16_t MempoolManager::poolCount() {
  uint16_t count = 0;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_entries[i].inUse) {
      ++count;
    }
  }
  return count;
}

size_t MempoolManager::totalAllocated() {
  size_t total = 0;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_entries[i].inUse) {
      total += _entries[i].pool.usedSize();
    }
  }
  return total;
}

size_t MempoolManager::totalFree() {
  size_t total = 0;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_entries[i].inUse) {
      total += _entries[i].pool.freeSize();
    }
  }
  return total;
}

MempoolEntry *MempoolManager::findFreeEntry() {
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (!_entries[i].inUse) {
      return &_entries[i];
    }
  }
  return nullptr;
}
