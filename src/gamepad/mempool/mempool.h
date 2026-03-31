#pragma once

#include <cstddef>
#include <cstdint>

namespace ThetaGP {
namespace GamePad {
namespace MemPool {

enum class PoolError {
  OK = 0,
  NoMemory,
  InvalidPtr,
  NotAllocated,
  AlreadyInitialized,
  NotInitialized,
  IteratorExpired,
  PoolFull
};

struct PoolStats {
  uint16_t totalSize;
  uint16_t usedSize;
  uint16_t freeSize;
  uint16_t allocCount;
  uint16_t peakUsage;
};

struct MemBlock {
  size_t size;    // Block size (excluding header)
  bool isFree;    // Allocation status
  MemBlock *next; // Next block in linked list
  MemBlock *prev; // Previous block in linked list
  uint32_t magic; // Magic number for validation
};

class MemPool {
private:
  static constexpr uint32_t MAGIC_FREE = 0xDEADBEEF;
  static constexpr uint32_t MAGIC_USED = 0xCAFEBABE;
  static constexpr size_t ALIGNMENT = 4;
  static constexpr size_t MIN_BLOCK_SIZE = sizeof(MemBlock) + ALIGNMENT;

  uint8_t *_memory;     // Pointer to memory region
  size_t _totalSize;    // Total memory size
  size_t _usedSize;     // Currently used size
  uint16_t _allocCount; // Number of allocated blocks
  uint16_t _peakUsage;  // Peak allocation count
  uint16_t _version;    // Version for iterator invalidation
  bool _initialized;    // Initialization status

  MemBlock *_head; // First block in list
  MemBlock *_tail; // Last block in list

  size_t alignSize(size_t size) const;
  MemBlock *findFreeBlock(size_t size);
  void splitBlock(MemBlock *block, size_t size);
  void mergeFreeBlocks();

public:
  // Iterator for traversing memory blocks
  class Iterator {
  public:
    Iterator() : _pool(nullptr), _block(nullptr), _version(0) {}
    Iterator(const MemPool* pool, MemBlock *block, uint16_t version);

    void *data() const;     // Get pointer to user data
    size_t size() const;    // Get block size
    bool isFree() const;    // Check if block is free
    bool isValid() const;   // Check if block is allocated
    bool isExpired() const; // Check if iterator is expired

    Iterator &operator++();   // Pre-increment
    Iterator operator++(int); // Post-increment
    Iterator &operator--();   // Pre-decrement

    bool operator==(const Iterator &other) const;
    bool operator!=(const Iterator &other) const;

  private:
    const MemPool* _pool;
    MemBlock *_block;
    uint16_t _version;
  };

  MemPool();
  ~MemPool();

  MemPool(const MemPool &) = delete;
  MemPool &operator=(const MemPool &) = delete;

  PoolError init(void *memory, size_t size);
  void deinit();

  void *alloc(size_t size);
  PoolError free(void *ptr);

  bool isValidPtr(void *ptr) const;
  PoolStats stats() const;

  bool isInitialized() const { return _initialized; }
  size_t freeSize() const;
  size_t usedSize() const { return _usedSize; }
  void resetPeakUsage();

  Iterator begin();
  Iterator end();
  Iterator rbegin();
  Iterator rend();
  Iterator cbegin() const;
  Iterator cend() const;
  Iterator crbegin() const;
  Iterator crend() const;

  uint16_t version() const { return _version; }
};

} // namespace MemPool
} // namespace GamePad
} // namespace ThetaGP
