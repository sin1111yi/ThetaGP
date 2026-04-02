#include "gamepad/mempool/mempoolmanager.h"
#include <cstring>

namespace ThetaGP::GamePad::MemPool {

// Static storage for pool entries (avoids dynamic allocation)
static MemPoolEntry s_poolEntries[MemPoolManager::MAX_POOLS];

MemPoolManager::Iterator::Iterator(const MemPoolManager* manager, MemPoolEntry *entry, uint16_t version)
    : _manager(manager), _entry(entry), _version(version) {}

MemPool &MemPoolManager::Iterator::operator*() const {
  return _entry->pool;
}

MemPool *MemPoolManager::Iterator::operator->() const {
  return &_entry->pool;
}

uint16_t MemPoolManager::Iterator::poolId() const {
  return _entry != nullptr ? _entry->id : 0;
}

MemPoolManager::Iterator &MemPoolManager::Iterator::operator++() {
  if (_entry != nullptr) {
    _entry = _entry->next;
  }
  return *this;
}

MemPoolManager::Iterator MemPoolManager::Iterator::operator++(int) {
  Iterator tmp = *this;
  ++(*this);
  return tmp;
}

MemPoolManager::Iterator &MemPoolManager::Iterator::operator--() {
  if (_entry != nullptr) {
    _entry = _entry->prev;
  }
  return *this;
}

bool MemPoolManager::Iterator::operator==(const Iterator &other) const {
  return _entry == other._entry;
}

bool MemPoolManager::Iterator::operator!=(const Iterator &other) const {
  return _entry != other._entry;
}

bool MemPoolManager::Iterator::isExpired() const {
  return _manager == nullptr || _version != _manager->version();
}

bool MemPoolManager::Iterator::isValid() const {
  return _entry != nullptr && _entry->inUse;
}

MemPoolManager::MemPoolManager()
    : _pools(s_poolEntries), _head(nullptr), _tail(nullptr), _poolCount(0),
      _version(0), _initialized(false) {
  // Initialize all pool entries
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    _pools[i].id = 0;
    _pools[i].inUse = false;
    _pools[i].next = nullptr;
    _pools[i].prev = nullptr;
    std::memset(_pools[i].name, 0, sizeof(_pools[i].name));
  }
}

MemPoolManager::~MemPoolManager() { deinit(); }

void MemPoolManager::init() {
  if (_initialized) {
    return;
  }

  // Deinitialize all pools and reset entries
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    _pools[i].pool.deinit();
    _pools[i].id = 0;
    _pools[i].inUse = false;
    _pools[i].next = nullptr;
    _pools[i].prev = nullptr;
    std::memset(_pools[i].name, 0, sizeof(_pools[i].name));
  }

  _head = nullptr;
  _tail = nullptr;
  _poolCount = 0;
  _version = 1;
  _initialized = true;
}

void MemPoolManager::deinit() {
  if (!_initialized) {
    return;
  }

  // Clean up all pools
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    _pools[i].pool.deinit();
    _pools[i].id = 0;
    _pools[i].inUse = false;
    _pools[i].next = nullptr;
    _pools[i].prev = nullptr;
    std::memset(_pools[i].name, 0, sizeof(_pools[i].name));
  }

  _head = nullptr;
  _tail = nullptr;
  _poolCount = 0;
  _version = 0;
  _initialized = false;
}

void MemPoolManager::updateListPointers() {
  // Rebuild linked list from active pool entries
  _head = nullptr;
  _tail = nullptr;

  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse) {
      if (_head == nullptr) {
        _head = &_pools[i];
        _tail = &_pools[i];
        _pools[i].prev = nullptr;
        _pools[i].next = nullptr;
      } else {
        _tail->next = &_pools[i];
        _pools[i].prev = _tail;
        _pools[i].next = nullptr;
        _tail = &_pools[i];
      }
    }
  }
}

PoolError MemPoolManager::addPool(uint16_t poolId, void *memory, size_t size,
                                  const char *name) {
  if (!_initialized) {
    return PoolError::NotInitialized;
  }

  MemPoolEntry *entry = findFreeEntry();
  if (entry == nullptr) {
    return PoolError::PoolFull;
  }

  PoolError result = entry->pool.init(memory, size);
  if (result == PoolError::OK) {
    entry->id = poolId;
    entry->inUse = true;
    // Copy pool name (truncate if necessary)
    if (name != nullptr) {
      std::strncpy(entry->name, name, sizeof(entry->name) - 1);
      entry->name[sizeof(entry->name) - 1] = '\0';
    }
    ++_poolCount;
    ++_version;  // Invalidate existing iterators
    updateListPointers();
  }

  return result;
}

PoolError MemPoolManager::addPool(void *memory, size_t size, const char *name) {
  // Auto-generate poolId: find first unused ID
  uint16_t newId = 1;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse && _pools[i].id >= newId) {
      newId = _pools[i].id + 1;
    }
  }
  return addPool(newId, memory, size, name);
}

PoolError MemPoolManager::removePool(uint16_t poolId) {
  if (!_initialized) {
    return PoolError::NotInitialized;
  }

  MemPoolEntry *entry = findEntry(poolId);
  if (entry == nullptr || !entry->inUse) {
    return PoolError::NotInitialized;
  }

  entry->pool.deinit();
  entry->inUse = false;
  entry->id = 0;
  std::memset(entry->name, 0, sizeof(entry->name));
  --_poolCount;
  ++_version;  // Invalidate existing iterators
  updateListPointers();

  return PoolError::OK;
}

MemPoolEntry *MemPoolManager::findEntry(uint16_t poolId) {
  // Linear search for pool by ID
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].id == poolId && _pools[i].inUse) {
      return &_pools[i];
    }
  }
  return nullptr;
}

const MemPoolEntry *MemPoolManager::findEntry(
    uint16_t poolId) const {
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].id == poolId && _pools[i].inUse) {
      return &_pools[i];
    }
  }
  return nullptr;
}

MemPoolEntry *MemPoolManager::findFreeEntry() {
  // Find first unused entry
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (!_pools[i].inUse) {
      return &_pools[i];
    }
  }
  return nullptr;
}

MemPool *MemPoolManager::pool(uint16_t poolId) {
  MemPoolEntry *entry = findEntry(poolId);
  if (entry == nullptr || !entry->inUse) {
    return nullptr;
  }
  return &entry->pool;
}

const MemPool *MemPoolManager::pool(uint16_t poolId) const {
  const MemPoolEntry *entry = findEntry(poolId);
  if (entry == nullptr || !entry->inUse) {
    return nullptr;
  }
  return &entry->pool;
}

MemPool *MemPoolManager::pool(const char *name) {
  if (name == nullptr) {
    return nullptr;
  }
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse && std::strcmp(_pools[i].name, name) == 0) {
      return &_pools[i].pool;
    }
  }
  return nullptr;
}

const MemPool *MemPoolManager::pool(const char *name) const {
  if (name == nullptr) {
    return nullptr;
  }
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse && std::strcmp(_pools[i].name, name) == 0) {
      return &_pools[i].pool;
    }
  }
  return nullptr;
}

const char *MemPoolManager::poolName(uint16_t poolId) const {
  const MemPoolEntry *entry = findEntry(poolId);
  if (entry == nullptr || !entry->inUse) {
    return nullptr;
  }
  return entry->name;
}

void *MemPoolManager::alloc(uint16_t poolId, size_t size) {
  if (!_initialized) {
    return nullptr;
  }

  MemPool *p = pool(poolId);
  if (p == nullptr) {
    return nullptr;
  }
  return p->alloc(size);
}

void *MemPoolManager::alloc(const char *name, size_t size) {
  if (!_initialized || name == nullptr) {
    return nullptr;
  }

  MemPool *p = pool(name);
  if (p == nullptr) {
    return nullptr;
  }
  return p->alloc(size);
}

PoolError MemPoolManager::free(uint16_t poolId, void *ptr) {
  if (!_initialized) {
    return PoolError::NotInitialized;
  }

  MemPool *p = pool(poolId);
  if (p == nullptr) {
    return PoolError::NotInitialized;
  }
  return p->free(ptr);
}

PoolError MemPoolManager::free(const char *name, void *ptr) {
  if (!_initialized || name == nullptr) {
    return PoolError::NotInitialized;
  }

  MemPool *p = pool(name);
  if (p == nullptr) {
    return PoolError::NotInitialized;
  }
  return p->free(ptr);
}

PoolStats MemPoolManager::poolStats(uint16_t poolId) const {
  static const PoolStats empty = {0, 0, 0, 0, 0};

  if (!_initialized) {
    return empty;
  }

  const MemPool *p = pool(poolId);
  if (p == nullptr) {
    return empty;
  }
  return p->stats();
}

void MemPoolManager::allStats(PoolStats *stats, uint16_t maxCount) const {
  if (!_initialized || stats == nullptr || maxCount == 0) {
    return;
  }

  uint16_t count = 0;
  for (uint16_t i = 0; i < MAX_POOLS && count < maxCount; ++i) {
    if (_pools[i].inUse) {
      stats[count] = _pools[i].pool.stats();
      ++count;
    }
  }
}

size_t MemPoolManager::totalAllocated() const {
  size_t total = 0;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse) {
      total += _pools[i].pool.usedSize();
    }
  }
  return total;
}

size_t MemPoolManager::totalFree() const {
  size_t total = 0;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse) {
      total += _pools[i].pool.freeSize();
    }
  }
  return total;
}

MemPoolManager::Iterator MemPoolManager::begin() {
  return Iterator(this, _head, _version);
}

MemPoolManager::Iterator MemPoolManager::end() {
  return Iterator(this, nullptr, _version);
}

MemPoolManager::Iterator MemPoolManager::rbegin() {
  return Iterator(this, _tail, _version);
}

MemPoolManager::Iterator MemPoolManager::rend() {
  return Iterator(this, nullptr, _version);
}

MemPoolManager::Iterator MemPoolManager::cbegin() const {
  return Iterator(this, const_cast<MemPoolEntry *>(_head), _version);
}

MemPoolManager::Iterator MemPoolManager::cend() const {
  return Iterator(this, nullptr, _version);
}

MemPoolManager::Iterator MemPoolManager::crbegin() const {
  return Iterator(this, const_cast<MemPoolEntry *>(_tail), _version);
}

MemPoolManager::Iterator MemPoolManager::crend() const {
  return Iterator(this, nullptr, _version);
}

} // namespace ThetaGP::GamePad::MemPool
