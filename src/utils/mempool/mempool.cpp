#include "utils/mempool/mempool.h"
#include <cstring>

using namespace ThetaGP::Mempool;

Mempool::Iterator::Iterator(const Mempool* pool, MemBlock *block, uint16_t version)
    : _pool(pool), _block(block), _version(version) {}

void *Mempool::Iterator::data() const {
  if (_block == nullptr) {
    return nullptr;
  }
  // Return pointer to user data area (after header)
  return reinterpret_cast<uint8_t *>(_block) + sizeof(MemBlock);
}

size_t Mempool::Iterator::size() const {
  return _block != nullptr ? _block->size : 0;
}

bool Mempool::Iterator::isFree() const {
  return _block != nullptr ? _block->isFree : true;
}

bool Mempool::Iterator::isValid() const {
  // Check if block is allocated (not free) and has valid magic number
  return _block != nullptr && _block->magic == MAGIC_USED;
}

bool Mempool::Iterator::isExpired() const {
  // Iterator is valid only if pool exists and version matches
  return _pool == nullptr || _version != _pool->version();
}

Mempool::Iterator &Mempool::Iterator::operator++() {
  if (_block != nullptr) {
    _block = _block->next;
  }
  return *this;
}

Mempool::Iterator Mempool::Iterator::operator++(int) {
  Iterator tmp = *this;
  ++(*this);
  return tmp;
}

Mempool::Iterator &Mempool::Iterator::operator--() {
  if (_block != nullptr) {
    _block = _block->prev;
  }
  return *this;
}

bool Mempool::Iterator::operator==(const Iterator &other) const {
  return _block == other._block;
}

bool Mempool::Iterator::operator!=(const Iterator &other) const {
  return _block != other._block;
}

Mempool::Mempool()
    : _memory(nullptr), _totalSize(0), _usedSize(0), _allocCount(0),
      _peakUsage(0), _version(0), _initialized(false), _head(nullptr),
      _tail(nullptr) {}

Mempool::~Mempool() { deinit(); }

size_t Mempool::alignSize(size_t size) const {
  // Align size to 4-byte boundary
  return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

MemBlock *Mempool::findFreeBlock(size_t size) {
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

void Mempool::splitBlock(MemBlock *block, size_t size) {
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

void Mempool::mergeFreeBlocks() {
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

PoolError Mempool::init(void *memory, size_t size) {
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

void Mempool::deinit() {
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

void *Mempool::alloc(size_t size) {
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

PoolError Mempool::free(void *ptr) {
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

bool Mempool::isValidPtr(void *ptr) const {
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

PoolStats Mempool::stats() const {
  return PoolStats{
      .totalSize = static_cast<uint16_t>(_totalSize),
      .usedSize = static_cast<uint16_t>(_usedSize),
      .freeSize = static_cast<uint16_t>(_totalSize - _usedSize),
      .allocCount = _allocCount,
      .peakUsage = _peakUsage,
  };
}

size_t Mempool::freeSize() const { return _totalSize - _usedSize; }

void Mempool::resetPeakUsage() { _peakUsage = _allocCount; }

Mempool::Iterator Mempool::begin() {
  return Iterator(this, _head, _version);
}

Mempool::Iterator Mempool::end() {
  return Iterator(this, nullptr, _version);
}

Mempool::Iterator Mempool::rbegin() {
  return Iterator(this, _tail, _version);
}

Mempool::Iterator Mempool::rend() {
  return Iterator(this, nullptr, _version);
}

Mempool::Iterator Mempool::cbegin() const {
  return Iterator(this, _head, _version);
}

Mempool::Iterator Mempool::cend() const {
  return Iterator(this, nullptr, _version);
}

Mempool::Iterator Mempool::crbegin() const {
  return Iterator(this, _tail, _version);
}

Mempool::Iterator Mempool::crend() const {
  return Iterator(this, nullptr, _version);
}
