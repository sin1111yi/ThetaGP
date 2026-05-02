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

#include <cstddef>
#include <cstdint>

namespace ThetaGP {
namespace Mempool {

enum class PoolError {
  OK = 0,
  NoMemory,
  InvalidPtr,
  NotAllocated,
  AlreadyInitialized,
  NotInitialized,
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
  size_t size;
  bool isFree;
  MemBlock *next;
  uint32_t magic;
};

class Mempool {
private:
  static constexpr uint32_t MAGIC_FREE = 0xDEADBEEF;
  static constexpr uint32_t MAGIC_USED = 0xCAFEBABE;
  static constexpr size_t ALIGNMENT = 4;
  static constexpr size_t MIN_BLOCK_SIZE = sizeof(MemBlock) + ALIGNMENT;

  uint8_t *_memory = nullptr;
  size_t _totalSize = 0;
  size_t _usedSize = 0;
  uint16_t _allocCount = 0;
  uint16_t _peakUsage = 0;
  bool _initialized = false;

  MemBlock *_head = nullptr;

  size_t alignSize(size_t size) const;
  MemBlock *findFreeBlock(size_t size);
  void splitBlock(MemBlock *block, size_t size);
  void mergeFreeBlocks();

public:
  Mempool() = default;
  ~Mempool();

  Mempool(const Mempool &) = delete;
  Mempool &operator=(const Mempool &) = delete;

  PoolError init(void *memory, size_t size);
  void deinit();

  void *alloc(size_t size);
  PoolError free(void *ptr);

  bool isValidPtr(void *ptr) const;
  PoolStats stats() const;

  size_t freeSize() const;
  size_t usedSize() const { return _usedSize; }
};

} // namespace Mempool
} // namespace ThetaGP
