#include "utils/mempool/mempoolmanager.h"
#include <cstring>

using namespace ThetaGP::Mempool;

// Static storage for pool entries (avoids dynamic allocation)
static MempoolEntry s_poolEntries[MempoolManager::MAX_POOLS];

// Singleton instance (static storage, no dynamic allocation)
static MempoolManager s_instance;

MempoolManager& MempoolManager::getInstance() {
  return s_instance;
}

MempoolManager::Iterator::Iterator(const MempoolManager* manager, MempoolEntry *entry, uint16_t version)
    : _manager(manager), _entry(entry), _version(version) {}

Mempool &MempoolManager::Iterator::operator*() const {
  return _entry->pool;
}

Mempool *MempoolManager::Iterator::operator->() const {
  return &_entry->pool;
}

uint16_t MempoolManager::Iterator::poolId() const {
  return _entry != nullptr ? _entry->id : 0;
}

MempoolManager::Iterator &MempoolManager::Iterator::operator++() {
  if (_entry != nullptr) {
    _entry = _entry->next;
  }
  return *this;
}

MempoolManager::Iterator MempoolManager::Iterator::operator++(int) {
  Iterator tmp = *this;
  ++(*this);
  return tmp;
}

MempoolManager::Iterator &MempoolManager::Iterator::operator--() {
  if (_entry != nullptr) {
    _entry = _entry->prev;
  }
  return *this;
}

bool MempoolManager::Iterator::operator==(const Iterator &other) const {
  return _entry == other._entry;
}

bool MempoolManager::Iterator::operator!=(const Iterator &other) const {
  return _entry != other._entry;
}

bool MempoolManager::Iterator::isExpired() const {
  return _manager == nullptr || _version != _manager->version();
}

bool MempoolManager::Iterator::isValid() const {
  return _entry != nullptr && _entry->inUse;
}

MempoolManager::MempoolManager()
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

MempoolManager::~MempoolManager() { deinit(); }

void MempoolManager::init() {
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

void MempoolManager::deinit() {
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

void MempoolManager::updateListPointers() {
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

PoolError MempoolManager::addPool(uint16_t poolId, void *memory, size_t size,
                                  const char *name) {
  if (!_initialized) {
    return PoolError::NotInitialized;
  }

  MempoolEntry *entry = findFreeEntry();
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

PoolError MempoolManager::addPool(void *memory, size_t size, const char *name) {
  // Auto-generate poolId: find first unused ID
  uint16_t newId = 1;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse && _pools[i].id >= newId) {
      newId = _pools[i].id + 1;
    }
  }
  return addPool(newId, memory, size, name);
}

PoolError MempoolManager::removePool(uint16_t poolId) {
  if (!_initialized) {
    return PoolError::NotInitialized;
  }

  MempoolEntry *entry = findEntry(poolId);
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

MempoolEntry *MempoolManager::findEntry(uint16_t poolId) {
  // Linear search for pool by ID
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].id == poolId && _pools[i].inUse) {
      return &_pools[i];
    }
  }
  return nullptr;
}

const MempoolEntry *MempoolManager::findEntry(
    uint16_t poolId) const {
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].id == poolId && _pools[i].inUse) {
      return &_pools[i];
    }
  }
  return nullptr;
}

MempoolEntry *MempoolManager::findFreeEntry() {
  // Find first unused entry
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (!_pools[i].inUse) {
      return &_pools[i];
    }
  }
  return nullptr;
}

Mempool *MempoolManager::pool(uint16_t poolId) {
  MempoolEntry *entry = findEntry(poolId);
  if (entry == nullptr || !entry->inUse) {
    return nullptr;
  }
  return &entry->pool;
}

const Mempool *MempoolManager::pool(uint16_t poolId) const {
  const MempoolEntry *entry = findEntry(poolId);
  if (entry == nullptr || !entry->inUse) {
    return nullptr;
  }
  return &entry->pool;
}

Mempool *MempoolManager::pool(const char *name) {
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

const Mempool *MempoolManager::pool(const char *name) const {
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

const char *MempoolManager::poolName(uint16_t poolId) const {
  const MempoolEntry *entry = findEntry(poolId);
  if (entry == nullptr || !entry->inUse) {
    return nullptr;
  }
  return entry->name;
}

void *MempoolManager::alloc(uint16_t poolId, size_t size) {
  if (!_initialized) {
    return nullptr;
  }

  Mempool *p = pool(poolId);
  if (p == nullptr) {
    return nullptr;
  }
  return p->alloc(size);
}

void *MempoolManager::alloc(const char *name, size_t size) {
  if (!_initialized || name == nullptr) {
    return nullptr;
  }

  Mempool *p = pool(name);
  if (p == nullptr) {
    return nullptr;
  }
  return p->alloc(size);
}

PoolError MempoolManager::free(uint16_t poolId, void *ptr) {
  if (!_initialized) {
    return PoolError::NotInitialized;
  }

  Mempool *p = pool(poolId);
  if (p == nullptr) {
    return PoolError::NotInitialized;
  }
  return p->free(ptr);
}

PoolError MempoolManager::free(const char *name, void *ptr) {
  if (!_initialized || name == nullptr) {
    return PoolError::NotInitialized;
  }

  Mempool *p = pool(name);
  if (p == nullptr) {
    return PoolError::NotInitialized;
  }
  return p->free(ptr);
}

PoolStats MempoolManager::poolStats(uint16_t poolId) const {
  static const PoolStats empty = {0, 0, 0, 0, 0};

  if (!_initialized) {
    return empty;
  }

  const Mempool *p = pool(poolId);
  if (p == nullptr) {
    return empty;
  }
  return p->stats();
}

void MempoolManager::allStats(PoolStats *stats, uint16_t maxCount) const {
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

size_t MempoolManager::totalAllocated() const {
  size_t total = 0;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse) {
      total += _pools[i].pool.usedSize();
    }
  }
  return total;
}

size_t MempoolManager::totalFree() const {
  size_t total = 0;
  for (uint16_t i = 0; i < MAX_POOLS; ++i) {
    if (_pools[i].inUse) {
      total += _pools[i].pool.freeSize();
    }
  }
  return total;
}

MempoolManager::Iterator MempoolManager::begin() {
  return Iterator(this, _head, _version);
}

MempoolManager::Iterator MempoolManager::end() {
  return Iterator(this, nullptr, _version);
}

MempoolManager::Iterator MempoolManager::rbegin() {
  return Iterator(this, _tail, _version);
}

MempoolManager::Iterator MempoolManager::rend() {
  return Iterator(this, nullptr, _version);
}

MempoolManager::Iterator MempoolManager::cbegin() const {
  return Iterator(this, const_cast<MempoolEntry *>(_head), _version);
}

MempoolManager::Iterator MempoolManager::cend() const {
  return Iterator(this, nullptr, _version);
}

MempoolManager::Iterator MempoolManager::crbegin() const {
  return Iterator(this, const_cast<MempoolEntry *>(_tail), _version);
}

MempoolManager::Iterator MempoolManager::crend() const {
  return Iterator(this, nullptr, _version);
}
