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

#include "utils/mempool/mempool.h"
#include <cstring>

using namespace ThetaGP::Mempool;

Mempool::~Mempool() { deinit(); }

size_t Mempool::alignSize(size_t size) const {
  return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

MemBlock *Mempool::findFreeBlock(size_t size) {
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
  size_t remaining = block->size - size - sizeof(MemBlock);

  if (remaining >= MIN_BLOCK_SIZE) {
    auto *newBlock = reinterpret_cast<MemBlock *>(
        reinterpret_cast<uint8_t *>(block) + sizeof(MemBlock) + size);

    newBlock->size = remaining;
    newBlock->isFree = true;
    newBlock->magic = MAGIC_FREE;
    newBlock->next = block->next;

    block->next = newBlock;
    block->size = size;
  }
}

void Mempool::mergeFreeBlocks() {
  MemBlock *current = _head;
  while (current != nullptr && current->next != nullptr) {
    if (current->isFree && current->next->isFree) {
      current->size += sizeof(MemBlock) + current->next->size;
      current->next = current->next->next;
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
  _initialized = true;

  std::memset(_memory, 0, alignedSize);

  _head = reinterpret_cast<MemBlock *>(_memory);
  _head->size = alignedSize - sizeof(MemBlock);
  _head->isFree = true;
  _head->magic = MAGIC_FREE;
  _head->next = nullptr;

  return PoolError::OK;
}

void Mempool::deinit() {
  _memory = nullptr;
  _totalSize = 0;
  _usedSize = 0;
  _allocCount = 0;
  _peakUsage = 0;
  _initialized = false;
  _head = nullptr;
}

void *Mempool::alloc(size_t size) {
  if (!_initialized || size == 0) {
    return nullptr;
  }

  size_t alignedSize = alignSize(size);
  MemBlock *block = findFreeBlock(alignedSize);

  if (block == nullptr) {
    return nullptr;
  }

  splitBlock(block, alignedSize);

  block->isFree = false;
  block->magic = MAGIC_USED;

  _usedSize += alignedSize;
  ++_allocCount;

  if (_allocCount > _peakUsage) {
    _peakUsage = _allocCount;
  }

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

  if (blockAddr < memoryStart || blockAddr >= memoryEnd) {
    return false;
  }

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
