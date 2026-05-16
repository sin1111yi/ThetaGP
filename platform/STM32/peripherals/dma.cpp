/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * @file dma.cpp
 * @brief DmaChannel implementation using LL (Low Layer) library.
 *
 * Pure LL-based DMA stream management. No HAL_DMA_Init or HAL_DMA_IRQHandler.
 * ISR dispatch is custom: entry → flag check/clear via LL → C callback.
 */

#include "build_info.h"

#include "drivers/peripherals/dma.h"
#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/nvic_exti.h"
#include "drivers/peripherals/systick.h"
#include "utils/types.h"

#include <cstring>

using ThetaGP::RetVal;
using namespace ThetaGP::Drivers::Peripheral::DMA;

// ── Constants ──

static constexpr uint32_t DMA_STREAM_TOTAL = 16;

// ── C-style ISR dispatch table ──

struct DmaIsrEntry {
  void (*handler)(void *ctx);
  void *context;
};

static DmaIsrEntry dmaIsrTable[DMA_STREAM_TOTAL] = {{nullptr, nullptr}};

// ── Stream resource tables ──

#if defined(STM32H7)

static constexpr DMA_Stream_TypeDef *streamInstances[DMA_STREAM_TOTAL] = {
    DMA1_Stream0, DMA1_Stream1, DMA1_Stream2, DMA1_Stream3,
    DMA1_Stream4, DMA1_Stream5, DMA1_Stream6, DMA1_Stream7,
    DMA2_Stream0, DMA2_Stream1, DMA2_Stream2, DMA2_Stream3,
    DMA2_Stream4, DMA2_Stream5, DMA2_Stream6, DMA2_Stream7,
};

static constexpr DMA_TypeDef *dmaControllers[DMA_STREAM_TOTAL] = {
    DMA1, DMA1, DMA1, DMA1, DMA1, DMA1, DMA1, DMA1,
    DMA2, DMA2, DMA2, DMA2, DMA2, DMA2, DMA2, DMA2,
};

static constexpr uint32_t llStreamId[DMA_STREAM_TOTAL] = {
    LL_DMA_STREAM_0, LL_DMA_STREAM_1, LL_DMA_STREAM_2, LL_DMA_STREAM_3,
    LL_DMA_STREAM_4, LL_DMA_STREAM_5, LL_DMA_STREAM_6, LL_DMA_STREAM_7,
    LL_DMA_STREAM_0, LL_DMA_STREAM_1, LL_DMA_STREAM_2, LL_DMA_STREAM_3,
    LL_DMA_STREAM_4, LL_DMA_STREAM_5, LL_DMA_STREAM_6, LL_DMA_STREAM_7,
};

static constexpr IRQn_Type streamIRQn[DMA_STREAM_TOTAL] = {
    DMA1_Stream0_IRQn, DMA1_Stream1_IRQn, DMA1_Stream2_IRQn, DMA1_Stream3_IRQn,
    DMA1_Stream4_IRQn, DMA1_Stream5_IRQn, DMA1_Stream6_IRQn, DMA1_Stream7_IRQn,
    DMA2_Stream0_IRQn, DMA2_Stream1_IRQn, DMA2_Stream2_IRQn, DMA2_Stream3_IRQn,
    DMA2_Stream4_IRQn, DMA2_Stream5_IRQn, DMA2_Stream6_IRQn, DMA2_Stream7_IRQn,
};

#endif

static inline uint8_t streamIndex(Controller ctrl, Stream stream) {
  return static_cast<uint8_t>(ctrl) * 8 + static_cast<uint8_t>(stream);
}

// ── LL helper: per-stream flag check/clear dispatch ──

#if defined(STM32H7)

static bool checkTcFlag(DMA_TypeDef *dma, uint32_t stream) {
  static bool (*const tcCheck[])(DMA_TypeDef *) = {
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TC0(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TC1(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TC2(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TC3(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TC4(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TC5(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TC6(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TC7(d); },
  };
  if (stream < 8) return tcCheck[stream](dma);
  return false;
}

static void clearTcFlag(DMA_TypeDef *dma, uint32_t stream) {
  static void (*const tcClear[])(DMA_TypeDef *) = {
      LL_DMA_ClearFlag_TC0, LL_DMA_ClearFlag_TC1, LL_DMA_ClearFlag_TC2,
      LL_DMA_ClearFlag_TC3, LL_DMA_ClearFlag_TC4, LL_DMA_ClearFlag_TC5,
      LL_DMA_ClearFlag_TC6, LL_DMA_ClearFlag_TC7,
  };
  if (stream < 8) tcClear[stream](dma);
}

static bool checkTeFlag(DMA_TypeDef *dma, uint32_t stream) {
  static bool (*const teCheck[])(DMA_TypeDef *) = {
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TE0(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TE1(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TE2(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TE3(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TE4(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TE5(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TE6(d); },
      [](DMA_TypeDef *d) { return !!LL_DMA_IsActiveFlag_TE7(d); },
  };
  if (stream < 8) return teCheck[stream](dma);
  return false;
}

static void clearTeFlag(DMA_TypeDef *dma, uint32_t stream) {
  static void (*const teClear[])(DMA_TypeDef *) = {
      LL_DMA_ClearFlag_TE0, LL_DMA_ClearFlag_TE1, LL_DMA_ClearFlag_TE2,
      LL_DMA_ClearFlag_TE3, LL_DMA_ClearFlag_TE4, LL_DMA_ClearFlag_TE5,
      LL_DMA_ClearFlag_TE6, LL_DMA_ClearFlag_TE7,
  };
  if (stream < 8) teClear[stream](dma);
}

#endif

// ── DmaChannel ──

DmaChannel::DmaChannel(Controller controller, Stream stream)
    : _ctrl(controller), _stream(stream) {
  // Keep a pointer to the DMA stream register block for address lookups
  _halHandle = nullptr;
}

DmaChannel::~DmaChannel() {
  deinit();
}

void DmaChannel::setRequestId(uint32_t dmamuxRequestId) {
  _requestId = dmamuxRequestId;
}

void DmaChannel::configure(const DmaConfig &cfg) {
  _cfg = cfg;
}

void DmaChannel::setCallback(DmaIsrCallback cb, void *context) {
  _isrCb = cb;
  _isrCtx = context;
}

void DmaChannel::enableClock() {
#if defined(STM32H7)
  if (_ctrl == Controller::Dma1) {
    __HAL_RCC_DMA1_CLK_ENABLE();
  } else {
    __HAL_RCC_DMA2_CLK_ENABLE();
  }
#endif
}

void DmaChannel::disableClock() {
#if defined(STM32H7)
  if (_ctrl == Controller::Dma1) {
    __HAL_RCC_DMA1_CLK_DISABLE();
  } else {
    __HAL_RCC_DMA2_CLK_DISABLE();
  }
#endif
}

RetVal DmaChannel::init() {
  if (_initialized) {
    return RetVal::Ok;
  }

#if defined(STM32H7)
  const auto idx = streamIndex(_ctrl, _stream);
  if (idx >= DMA_STREAM_TOTAL || streamInstances[idx] == nullptr) {
    return RetVal::Error;
  }

  auto *dma = dmaControllers[idx];
  uint32_t stream = llStreamId[idx];

  enableClock();

  // ── Disable stream before configuration ──
  LL_DMA_DisableStream(dma, stream);
  {
    uint32_t retry = 100;
    while (LL_DMA_IsEnabledStream(dma, stream) && retry > 0) {
      delay_us(1);
      retry--;
    }
  }

  // Clear any pending flags
  clearTcFlag(dma, stream);
  clearTeFlag(dma, stream);

  // ── Configure CR register ──
  static constexpr uint32_t dirMap[] = {
      LL_DMA_DIRECTION_PERIPH_TO_MEMORY,
      LL_DMA_DIRECTION_MEMORY_TO_PERIPH,
      LL_DMA_DIRECTION_MEMORY_TO_MEMORY,
  };
  static constexpr uint32_t sizeMap[] = {
      LL_DMA_PDATAALIGN_BYTE,
      LL_DMA_PDATAALIGN_HALFWORD,
      LL_DMA_PDATAALIGN_WORD,
  };
  static constexpr uint32_t memSizeMap[] = {
      LL_DMA_MDATAALIGN_BYTE,
      LL_DMA_MDATAALIGN_HALFWORD,
      LL_DMA_MDATAALIGN_WORD,
  };
  static constexpr uint32_t prioMap[] = {
      LL_DMA_PRIORITY_LOW,
      LL_DMA_PRIORITY_MEDIUM,
      LL_DMA_PRIORITY_HIGH,
      LL_DMA_PRIORITY_VERYHIGH,
  };

  // Direction-aware increment selection
  uint32_t pinc, minc;
  if (_cfg.direction == Direction::MemoryToPeripheral) {
    pinc = _cfg.destIncrement ? LL_DMA_PERIPH_INCREMENT : LL_DMA_PERIPH_NOINCREMENT;
    minc = _cfg.srcIncrement ? LL_DMA_MEMORY_INCREMENT : LL_DMA_MEMORY_NOINCREMENT;
  } else {
    pinc = _cfg.srcIncrement ? LL_DMA_PERIPH_INCREMENT : LL_DMA_PERIPH_NOINCREMENT;
    minc = _cfg.destIncrement ? LL_DMA_MEMORY_INCREMENT : LL_DMA_MEMORY_NOINCREMENT;
  }

  LL_DMA_ConfigTransfer(dma, stream,
      dirMap[static_cast<size_t>(_cfg.direction)] |
      (_cfg.circularMode ? LL_DMA_MODE_CIRCULAR : LL_DMA_MODE_NORMAL) |
      pinc |
      minc |
      sizeMap[static_cast<size_t>(_cfg.srcDataWidth)] |
      memSizeMap[static_cast<size_t>(_cfg.destDataWidth)] |
      prioMap[static_cast<size_t>(_cfg.priority)] |
      (_cfg.periphFlowControl ? DMA_SxCR_PFCTRL : 0));

  // ── Configure DMAMUX ──
  if (_requestId != 0) {
    LL_DMAMUX_SetRequestID(DMAMUX1, idx, _requestId);
  }

  // ── Register to C dispatch table ──
  dmaIsrTable[idx].handler = _isrCb;
  dmaIsrTable[idx].context = _isrCtx;

  // ── NVIC ──
  const auto prio = static_cast<uint32_t>(
      NVIC_EXTI::NvicPriority::PriorityLow);
  HAL_NVIC_SetPriority(streamIRQn[idx], NVIC_PRIORITY_BASE(prio),
                       NVIC_PRIORITY_SUB(prio));
  HAL_NVIC_EnableIRQ(streamIRQn[idx]);

  _initialized = true;
  return RetVal::Ok;
#else
  return RetVal::Error;
#endif
}

RetVal DmaChannel::deinit() {
  if (!_initialized) {
    return RetVal::Ok;
  }

#if defined(STM32H7)
  const auto idx = streamIndex(_ctrl, _stream);
  auto *dma = dmaControllers[idx];
  uint32_t stream = llStreamId[idx];

  LL_DMA_DisableStream(dma, stream);
  {
    uint32_t retry = 100;
    while (LL_DMA_IsEnabledStream(dma, stream) && retry > 0) {
      delay_us(1);
      retry--;
    }
  }

  HAL_NVIC_DisableIRQ(streamIRQn[idx]);
  dmaIsrTable[idx].handler = nullptr;
  dmaIsrTable[idx].context = nullptr;

  _initialized = false;
  return RetVal::Ok;
#else
  return RetVal::Error;
#endif
}

RetVal DmaChannel::start(uint32_t srcAddress, uint32_t dstAddress,
                         uint16_t dataCount) {
  if (!_initialized) {
    return RetVal::Error;
  }

#if defined(STM32H7)
  const auto idx = streamIndex(_ctrl, _stream);
  auto *dma = dmaControllers[idx];
  uint32_t stream = llStreamId[idx];

  // Refresh ISR dispatch
  dmaIsrTable[idx].handler = _isrCb;
  dmaIsrTable[idx].context = _isrCtx;

  // Set addresses and data count — direction-aware
  // MemoryToPeripheral: PAR=TDR (dst), M0AR=buffer (src)
  // PeripheralToMemory/MemoryToMemory: PAR=src, M0AR=dst
  if (_cfg.direction == Direction::MemoryToPeripheral) {
    LL_DMA_SetPeriphAddress(dma, stream, dstAddress);
    LL_DMA_SetMemoryAddress(dma, stream, srcAddress);
  } else {
    LL_DMA_SetPeriphAddress(dma, stream, srcAddress);
    LL_DMA_SetMemoryAddress(dma, stream, dstAddress);
  }
  LL_DMA_SetDataLength(dma, stream, dataCount);

  // Clear pending flags
  clearTcFlag(dma, stream);
  clearTeFlag(dma, stream);

  // Enable interrupts and stream
  LL_DMA_EnableIT_TC(dma, stream);
  LL_DMA_EnableIT_TE(dma, stream);
  LL_DMA_EnableStream(dma, stream);

  return RetVal::Ok;
#else
  return RetVal::Error;
#endif
}

RetVal DmaChannel::stop() {
  if (!_initialized) {
    return RetVal::Ok;
  }

#if defined(STM32H7)
  const auto idx = streamIndex(_ctrl, _stream);
  auto *dma = dmaControllers[idx];
  uint32_t stream = llStreamId[idx];

  LL_DMA_DisableStream(dma, stream);
  {
    uint32_t retry = 100;
    while (LL_DMA_IsEnabledStream(dma, stream) && retry > 0) {
      delay_us(1);
      retry--;
    }
  }

  LL_DMA_DisableIT_TC(dma, stream);
  LL_DMA_DisableIT_TE(dma, stream);

  return RetVal::Ok;
#else
  return RetVal::Error;
#endif
}

bool DmaChannel::isBusy() const {
#if defined(STM32H7)
  const auto idx = streamIndex(_ctrl, _stream);
  auto *dma = dmaControllers[idx];
  uint32_t stream = llStreamId[idx];
  return LL_DMA_IsEnabledStream(dma, stream);
#else
  return false;
#endif
}

uint32_t DmaChannel::getRemainingCount() const {
  if (!_initialized) {
    return 0;
  }
#if defined(STM32H7)
  const auto idx = streamIndex(_ctrl, _stream);
  auto *dma = dmaControllers[idx];
  uint32_t stream = llStreamId[idx];
  return LL_DMA_GetDataLength(dma, stream);
#else
  return 0;
#endif
}

// ── Pure C ISR entry points ──

extern "C" {

#if defined(STM32H7)

static void dmaIrqHandler(int idx) {
  if (idx < 0 || idx >= static_cast<int>(DMA_STREAM_TOTAL)) return;

  auto *dma = dmaControllers[idx];
  uint32_t stream = llStreamId[idx];

  if (checkTcFlag(dma, stream)) {
    clearTcFlag(dma, stream);
    if (dmaIsrTable[idx].handler) {
      dmaIsrTable[idx].handler(dmaIsrTable[idx].context);
    }
  }

  if (checkTeFlag(dma, stream)) {
    clearTeFlag(dma, stream);
    if (dmaIsrTable[idx].handler) {
      dmaIsrTable[idx].handler(dmaIsrTable[idx].context);
    }
  }
}

#define DMAx_StreamN_IRQHandler(name, idx) \
  void name(void) { dmaIrqHandler(idx); }

DMAx_StreamN_IRQHandler(DMA1_Stream0_IRQHandler, 0)
DMAx_StreamN_IRQHandler(DMA1_Stream1_IRQHandler, 1)
DMAx_StreamN_IRQHandler(DMA1_Stream2_IRQHandler, 2)
DMAx_StreamN_IRQHandler(DMA1_Stream3_IRQHandler, 3)
DMAx_StreamN_IRQHandler(DMA1_Stream4_IRQHandler, 4)
DMAx_StreamN_IRQHandler(DMA1_Stream5_IRQHandler, 5)
DMAx_StreamN_IRQHandler(DMA1_Stream6_IRQHandler, 6)
DMAx_StreamN_IRQHandler(DMA1_Stream7_IRQHandler, 7)
DMAx_StreamN_IRQHandler(DMA2_Stream0_IRQHandler, 8)
DMAx_StreamN_IRQHandler(DMA2_Stream1_IRQHandler, 9)
DMAx_StreamN_IRQHandler(DMA2_Stream2_IRQHandler, 10)
DMAx_StreamN_IRQHandler(DMA2_Stream3_IRQHandler, 11)
DMAx_StreamN_IRQHandler(DMA2_Stream4_IRQHandler, 12)
DMAx_StreamN_IRQHandler(DMA2_Stream5_IRQHandler, 13)
DMAx_StreamN_IRQHandler(DMA2_Stream6_IRQHandler, 14)
DMAx_StreamN_IRQHandler(DMA2_Stream7_IRQHandler, 15)

#endif
} // extern "C"
