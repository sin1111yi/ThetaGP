#pragma once

#include "gamepad/mempool/mempool.h"
#include <array>

namespace ThetaGP {
namespace GamePad {
namespace MemPool {

struct MemPoolEntry {
  MemPool pool;       // Memory pool instance
  uint16_t id;        // Pool identifier
  bool inUse;         // Entry usage status
  MemPoolEntry *next; // Next entry in linked list
  MemPoolEntry *prev; // Previous entry in linked list
  char name[16];      // Pool name (max 15 chars + null)
};

class MemPoolManager {
public:
  static constexpr uint16_t MAX_POOLS = 8;

  // Iterator for traversing managed pools
  class Iterator {
  public:
    Iterator() : _manager(nullptr), _entry(nullptr), _version(0) {}
    Iterator(const MemPoolManager* manager, MemPoolEntry *entry, uint16_t version);

    MemPool &operator*() const;  // Dereference to get pool
    MemPool *operator->() const; // Arrow operator for pool access
    uint16_t poolId() const;     // Get pool ID

    Iterator &operator++();   // Pre-increment
    Iterator operator++(int); // Post-increment
    Iterator &operator--();   // Pre-decrement

    bool operator==(const Iterator &other) const;
    bool operator!=(const Iterator &other) const;

    bool isExpired() const; // Check if iterator is expired
    bool isValid() const;   // Check if entry is valid

  private:
    const MemPoolManager* _manager;
    MemPoolEntry *_entry;
    uint16_t _version;
  };

  MemPoolManager();
  ~MemPoolManager();

  MemPoolManager(const MemPoolManager &) = delete;
  MemPoolManager &operator=(const MemPoolManager &) = delete;

  void init();
  void deinit();

  bool isInitialized() const { return _initialized; }

  PoolError addPool(uint16_t poolId, void *memory, size_t size,
                    const char *name);
  PoolError addPool(void *memory, size_t size, const char *name);
  PoolError removePool(uint16_t poolId);

  MemPool *pool(uint16_t poolId);
  const MemPool *pool(uint16_t poolId) const;
  MemPool *pool(const char *name);
  const MemPool *pool(const char *name) const;

  const char *poolName(uint16_t poolId) const;

  void *alloc(uint16_t poolId, size_t size);
  void *alloc(const char *name, size_t size);
  PoolError free(uint16_t poolId, void *ptr);
  PoolError free(const char *name, void *ptr);

  PoolStats poolStats(uint16_t poolId) const;
  void allStats(PoolStats *stats, uint16_t maxCount) const;

  uint16_t poolCount() const { return _poolCount; }
  uint16_t version() const { return _version; }
  size_t totalAllocated() const;
  size_t totalFree() const;

  Iterator begin();
  Iterator end();
  Iterator rbegin();
  Iterator rend();
  Iterator cbegin() const;
  Iterator cend() const;
  Iterator crbegin() const;
  Iterator crend() const;

private:
  MemPoolEntry *_pools; // Array of pool entries
  MemPoolEntry *_head;  // First used entry
  MemPoolEntry *_tail;  // Last used entry
  uint16_t _poolCount;  // Number of active pools
  uint16_t _version;    // Version for iterator invalidation
  bool _initialized;    // Manager initialization status

  MemPoolEntry *findEntry(uint16_t poolId);
  const MemPoolEntry *findEntry(uint16_t poolId) const;
  MemPoolEntry *findFreeEntry();
  void updateListPointers();
};

} // namespace MemPool
} // namespace GamePad
} // namespace ThetaGP
