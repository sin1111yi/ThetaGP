/**
 * @file bus.cpp
 * @brief BUS base class implementation with memory pool support
 */

#include "build_info.h"

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/bus/busmem.h"

#include <cstring>

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

COMMON_CODE static uint8_t s_busPoolMemory[4096];

// Track if memory pool has been initialized
static bool s_poolInitialized = false;

Bus::Bus() : _pTxBuf(nullptr), _pRxBuf(nullptr) {
  // Initialize BUS memory pool on first construction
  if (!s_poolInitialized) {
    s_poolInitialized =
        BusMem::initDefault(s_busPoolMemory, sizeof(s_busPoolMemory));
  }
}

Bus::~Bus() { freeBuf(); }

bool Bus::initMemoryPool(void *memory, size_t totalSize) {
  return BusMem::initDefault(memory, totalSize);
}

Mempool::PoolStats Bus::getPoolStats() {
  return BusMem::txStats(); // Return TX stats as representative
}

void Bus::allocBuf(size_t txSize, size_t rxSize) {
  if (_pTxBuf == nullptr) {
    _pTxBuf = static_cast<uint8_t *>(BusMem::allocTxBuffer(txSize));
    if (_pTxBuf != nullptr) {
      std::memset(_pTxBuf, 0, txSize);
    }
  }

  if (_pRxBuf == nullptr) {
    _pRxBuf = static_cast<uint8_t *>(BusMem::allocRxBuffer(rxSize));
    if (_pRxBuf != nullptr) {
      std::memset(_pRxBuf, 0, rxSize);
    }
  }
}

void Bus::freeBuf() {
  if (_pTxBuf != nullptr) {
    BusMem::freeTxBuffer(_pTxBuf);
    _pTxBuf = nullptr;
  }

  if (_pRxBuf != nullptr) {
    BusMem::freeRxBuffer(_pRxBuf);
    _pRxBuf = nullptr;
  }
}

// Default implementations for bulk operations
RetVal Bus::write(uint8_t *bytes, uint16_t num) {
  if (bytes == nullptr || num == 0) {
    return RetVal::Error;
  }
  
  for (uint16_t i = 0; i < num; i++) {
    RetVal result = write(bytes[i]);
    if (result != RetVal::Ok) {
      return result;
    }
  }
  
  return RetVal::Ok;
}

RetVal Bus::read(uint8_t *bytes, uint16_t num) {
  if (bytes == nullptr || num == 0) {
    return RetVal::Error;
  }
  
  for (uint16_t i = 0; i < num; i++) {
    RetVal result = read(&bytes[i]);
    if (result != RetVal::Ok) {
      return result;
    }
  }
  
  return RetVal::Ok;
}

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
