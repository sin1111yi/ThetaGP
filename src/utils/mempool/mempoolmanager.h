#pragma once

#include "utils/mempool/mempool.h"
#include <array>

namespace ThetaGP {
namespace Mempool {

struct MempoolEntry {
  Mempool pool;       // Memory pool instance
  uint16_t id;        // Pool identifier
  bool inUse;         // Entry usage status
  MempoolEntry *next; // Next entry in linked list
  MempoolEntry *prev; // Previous entry in linked list
  char name[16];      // Pool name (max 15 chars + null)
};

class MempoolManager {
public:
  static constexpr uint16_t MAX_POOLS = 8;

  static MempoolManager &getInstance();

  // Iterator for traversing managed pools
  class Iterator {
  public:
    Iterator() : _manager(nullptr), _entry(nullptr), _version(0) {}
    Iterator(const MempoolManager *manager, MempoolEntry *entry,
             uint16_t version);

    Mempool &operator*() const;  // Dereference to get pool
    Mempool *operator->() const; // Arrow operator for pool access
    uint16_t poolId() const;     // Get pool ID

    Iterator &operator++();   // Pre-increment
    Iterator operator++(int); // Post-increment
    Iterator &operator--();   // Pre-decrement

    bool operator==(const Iterator &other) const;
    bool operator!=(const Iterator &other) const;

    bool isExpired() const; // Check if iterator is expired
    bool isValid() const;   // Check if entry is valid

  private:
    const MempoolManager *_manager;
    MempoolEntry *_entry;
    uint16_t _version;
  };

  MempoolManager();
  ~MempoolManager();

  MempoolManager(const MempoolManager &) = delete;
  MempoolManager &operator=(const MempoolManager &) = delete;

  void init();
  void deinit();

  bool isInitialized() const { return _initialized; }

  PoolError addPool(uint16_t poolId, void *memory, size_t size,
                    const char *name);
  PoolError addPool(void *memory, size_t size, const char *name);
  PoolError removePool(uint16_t poolId);

  Mempool *pool(uint16_t poolId);
  const Mempool *pool(uint16_t poolId) const;
  Mempool *pool(const char *name);
  const Mempool *pool(const char *name) const;

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
  MempoolEntry *_pools; // Array of pool entries
  MempoolEntry *_head;  // First used entry
  MempoolEntry *_tail;  // Last used entry
  uint16_t _poolCount;  // Number of active pools
  uint16_t _version;    // Version for iterator invalidation
  bool _initialized;    // Manager initialization status

  MempoolEntry *findEntry(uint16_t poolId);
  const MempoolEntry *findEntry(uint16_t poolId) const;
  MempoolEntry *findFreeEntry();
  void updateListPointers();
};

} // namespace Mempool
} // namespace ThetaGP
