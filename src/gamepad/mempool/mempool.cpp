#include "gamepad/mempool/mempool.h"
#include <cstring>

namespace ThetaGP::GamePad::MemPool {

MemPool::Iterator::Iterator(const MemPool* pool, MemBlock *block, uint16_t version)
    : _pool(pool), _block(block), _version(version) {}

void *MemPool::Iterator::data() const {
  if (_block == nullptr) {
    return nullptr;
  }
  // Return pointer to user data area (after header)
  return reinterpret_cast<uint8_t *>(_block) + sizeof(MemBlock);
}

size_t MemPool::Iterator::size() const {
  return _block != nullptr ? _block->size : 0;
}

bool MemPool::Iterator::isFree() const {
  return _block != nullptr ? _block->isFree : true;
}

bool MemPool::Iterator::isValid() const {
  // Check if block is allocated (not free) and has valid magic number
  return _block != nullptr && _block->magic == MAGIC_USED;
}

bool MemPool::Iterator::isExpired() const {
  // Iterator is valid only if pool exists and version matches
  return _pool == nullptr || _version != _pool->version();
}

MemPool::Iterator &MemPool::Iterator::operator++() {
  if (_block != nullptr) {
    _block = _block->next;
  }
  return *this;
}

MemPool::Iterator MemPool::Iterator::operator++(int) {
  Iterator tmp = *this;
  ++(*this);
  return tmp;
}

MemPool::Iterator &MemPool::Iterator::operator--() {
  if (_block != nullptr) {
    _block = _block->prev;
  }
  return *this;
}

bool MemPool::Iterator::operator==(const Iterator &other) const {
  return _block == other._block;
}

bool MemPool::Iterator::operator!=(const Iterator &other) const {
  return _block != other._block;
}

MemPool::MemPool()
    : _memory(nullptr), _totalSize(0), _usedSize(0), _allocCount(0),
      _peakUsage(0), _version(0), _initialized(false), _head(nullptr),
      _tail(nullptr) {}

MemPool::~MemPool() { deinit(); }

size_t MemPool::alignSize(size_t size) const {
  // Align size to 4-byte boundary
  return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

MemBlock *MemPool::findFreeBlock(size_t size) {
  // First-fit search for a free block
  MemBlock *current = _head;
  while (current != nullptr) {
    if (current->isFree && current->size >= size) {
      return current;
    }
    current = current->next;
  }
  return nullptr;
}

void MemPool::splitBlock(MemBlock *block, size_t size) {
  // Calculate remaining space after allocation
  size_t remaining = block->size - size - sizeof(MemBlock);

  // Split only if remaining space can hold a valid block
  if (remaining >= MIN_BLOCK_SIZE) {
    auto *newBlock = reinterpret_cast<MemBlock *>(
        reinterpret_cast<uint8_t *>(block) + sizeof(MemBlock) + size);

    newBlock->size = remaining;
    newBlock->isFree = true;
    newBlock->magic = MAGIC_FREE;
    newBlock->next = block->next;
    newBlock->prev = block;

    if (block->next != nullptr) {
      block->next->prev = newBlock;
    } else {
      _tail = newBlock;  // block was tail, newBlock becomes new tail
    }

    block->next = newBlock;
    block->size = size;
  }
}

void MemPool::mergeFreeBlocks() {
  // Merge consecutive free blocks to reduce fragmentation
  MemBlock *current = _head;
  while (current != nullptr && current->next != nullptr) {
    if (current->isFree && current->next->isFree) {
      current->size += sizeof(MemBlock) + current->next->size;
      current->next = current->next->next;
      if (current->next != nullptr) {
        current->next->prev = current;
      } else {
        _tail = current;
      }
    } else {
      current = current->next;
    }
  }
}

PoolError MemPool::init(void *memory, size_t size) {
  if (_initialized) {
    return PoolError::AlreadyInitialized;
  }

  if (memory == nullptr || size < MIN_BLOCK_SIZE) {
    return PoolError::InvalidPtr;
  }

  size_t alignedSize = alignSize(size);
  _memory = static_cast<uint8_t *>(memory);
  _totalSize = alignedSize;
  _usedSize = 0;
  _allocCount = 0;
  _peakUsage = 0;
  _version = 1;
  _initialized = true;

  // Clear memory and initialize first block
  std::memset(_memory, 0, alignedSize);

  _head = reinterpret_cast<MemBlock *>(_memory);
  _head->size = alignedSize - sizeof(MemBlock);
  _head->isFree = true;
  _head->magic = MAGIC_FREE;
  _head->next = nullptr;
  _head->prev = nullptr;
  _tail = _head;

  return PoolError::OK;
}

void MemPool::deinit() {
  _memory = nullptr;
  _totalSize = 0;
  _usedSize = 0;
  _allocCount = 0;
  _peakUsage = 0;
  _version = 0;
  _initialized = false;
  _head = nullptr;
  _tail = nullptr;
}

void *MemPool::alloc(size_t size) {
  if (!_initialized || size == 0) {
    return nullptr;
  }

  size_t alignedSize = alignSize(size);
  MemBlock *block = findFreeBlock(alignedSize);

  if (block == nullptr) {
    return nullptr;  // No suitable block found
  }

  splitBlock(block, alignedSize);

  block->isFree = false;
  block->magic = MAGIC_USED;

  _usedSize += alignedSize;
  ++_allocCount;
  ++_version;  // Increment version to invalidate existing iterators

  if (_allocCount > _peakUsage) {
    _peakUsage = _allocCount;
  }

  // Return pointer to user data area
  return reinterpret_cast<uint8_t *>(block) + sizeof(MemBlock);
}

PoolError MemPool::free(void *ptr) {
  if (!_initialized) {
    return PoolError::NotInitialized;
  }

  if (ptr == nullptr) {
    return PoolError::InvalidPtr;
  }

  if (!isValidPtr(ptr)) {
    return PoolError::InvalidPtr;
  }

  auto *block = reinterpret_cast<MemBlock *>(
      reinterpret_cast<uint8_t *>(ptr) - sizeof(MemBlock));

  if (block->magic != MAGIC_USED) {
    return PoolError::NotAllocated;
  }

  size_t blockSize = block->size;
  _usedSize -= blockSize;
  --_allocCount;
  ++_version;  // Increment version to invalidate existing iterators

  block->isFree = true;
  block->magic = MAGIC_FREE;

  mergeFreeBlocks();

  return PoolError::OK;
}

bool MemPool::isValidPtr(void *ptr) const {
  if (!_initialized || ptr == nullptr) {
    return false;
  }

  auto *block = reinterpret_cast<MemBlock *>(
      reinterpret_cast<uint8_t *>(ptr) - sizeof(MemBlock));

  auto *memoryStart = _memory;
  auto *memoryEnd = _memory + _totalSize;
  auto *blockAddr = reinterpret_cast<uint8_t *>(block);

  // Check if block is within memory bounds
  if (blockAddr < memoryStart || blockAddr >= memoryEnd) {
    return false;
  }

  // Check if block has valid magic number
  return block->magic == MAGIC_USED || block->magic == MAGIC_FREE;
}

PoolStats MemPool::stats() const {
  return PoolStats{
      .totalSize = static_cast<uint16_t>(_totalSize),
      .usedSize = static_cast<uint16_t>(_usedSize),
      .freeSize = static_cast<uint16_t>(_totalSize - _usedSize),
      .allocCount = _allocCount,
      .peakUsage = _peakUsage,
  };
}

size_t MemPool::freeSize() const { return _totalSize - _usedSize; }

void MemPool::resetPeakUsage() { _peakUsage = _allocCount; }

MemPool::Iterator MemPool::begin() {
  return Iterator(this, _head, _version);
}

MemPool::Iterator MemPool::end() {
  return Iterator(this, nullptr, _version);
}

MemPool::Iterator MemPool::rbegin() {
  return Iterator(this, _tail, _version);
}

MemPool::Iterator MemPool::rend() {
  return Iterator(this, nullptr, _version);
}

MemPool::Iterator MemPool::cbegin() const {
  return Iterator(this, _head, _version);
}

MemPool::Iterator MemPool::cend() const {
  return Iterator(this, nullptr, _version);
}

MemPool::Iterator MemPool::crbegin() const {
  return Iterator(this, _tail, _version);
}

MemPool::Iterator MemPool::crend() const {
  return Iterator(this, nullptr, _version);
}

} // namespace ThetaGP::GamePad::MemPool
