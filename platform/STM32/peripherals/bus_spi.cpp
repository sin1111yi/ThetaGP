/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file bus_spi.cpp (refactored)
 * @brief SPI bus implementation for STM32H7
 *
 * After bus refactor:
 *   - Removed: writeBytesDMA, readBytesDMA (empty stubs)
 *   - Renamed: writeBytesPolling → writeSync
 *   - Renamed: readBytesPolling → readSync
 *   - Added: transfer() for full-duplex operation (fixes NCS assertion)
 */

#include "build_info.h"

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/dma.h"
#include "drivers/peripherals/dma_manager.h"
#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/nvic_exti.h"
#include "utils/log/log.h"

#include <array>
#include <cstring>

#if defined(STM32H7)
#include "stm32h7xx_ll_spi.h"
#endif

#if defined(STM32H7)
#define SPI_IRQ_GROUPS 6
#endif

using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Drivers::Peripheral::GPIO;
using ThetaGP::Result;

struct HalSpi {
  SPI_HandleTypeDef handle;
};

#define HANDLE (static_cast<HalSpi *>(_halHandle)->handle)

static constexpr struct {
  SpiInstance spi;
  Port port;
  Pin pin;
  uint8_t af;
} spiPinAfTable[] = {
    {SpiInstance::Spi1, Port::PortA, Pin::Pin5, 5},
    {SpiInstance::Spi1, Port::PortA, Pin::Pin6, 5},
    {SpiInstance::Spi1, Port::PortA, Pin::Pin7, 5},
    {SpiInstance::Spi1, Port::PortB, Pin::Pin3, 5},
    {SpiInstance::Spi1, Port::PortB, Pin::Pin4, 5},
    {SpiInstance::Spi1, Port::PortB, Pin::Pin5, 5},
    {SpiInstance::Spi1, Port::PortE, Pin::Pin13, 5},
    {SpiInstance::Spi1, Port::PortE, Pin::Pin14, 5},
    {SpiInstance::Spi1, Port::PortE, Pin::Pin15, 5},
    {SpiInstance::Spi2, Port::PortB, Pin::Pin10, 5},
    {SpiInstance::Spi2, Port::PortB, Pin::Pin13, 5},
    {SpiInstance::Spi2, Port::PortB, Pin::Pin14, 5},
    {SpiInstance::Spi2, Port::PortB, Pin::Pin15, 5},
    {SpiInstance::Spi2, Port::PortC, Pin::Pin2, 5},
    {SpiInstance::Spi2, Port::PortD, Pin::Pin1, 6},
    {SpiInstance::Spi2, Port::PortD, Pin::Pin3, 6},
    {SpiInstance::Spi2, Port::PortD, Pin::Pin4, 6},
    {SpiInstance::Spi2, Port::PortI, Pin::Pin1, 5},
    {SpiInstance::Spi2, Port::PortI, Pin::Pin2, 5},
    {SpiInstance::Spi2, Port::PortI, Pin::Pin3, 5},
    {SpiInstance::Spi3, Port::PortB, Pin::Pin3, 6},
    {SpiInstance::Spi3, Port::PortB, Pin::Pin4, 6},
    {SpiInstance::Spi3, Port::PortB, Pin::Pin5, 6},
    {SpiInstance::Spi3, Port::PortC, Pin::Pin10, 6},
    {SpiInstance::Spi3, Port::PortC, Pin::Pin11, 6},
    {SpiInstance::Spi3, Port::PortC, Pin::Pin12, 6},
    {SpiInstance::Spi3, Port::PortD, Pin::Pin6, 5},
    {SpiInstance::Spi4, Port::PortE, Pin::Pin2, 5},
    {SpiInstance::Spi4, Port::PortE, Pin::Pin5, 5},
    {SpiInstance::Spi4, Port::PortE, Pin::Pin6, 5},
    {SpiInstance::Spi4, Port::PortE, Pin::Pin12, 5},
    {SpiInstance::Spi4, Port::PortE, Pin::Pin13, 5},
    {SpiInstance::Spi4, Port::PortE, Pin::Pin14, 5},
    {SpiInstance::Spi5, Port::PortF, Pin::Pin7, 5},
    {SpiInstance::Spi5, Port::PortF, Pin::Pin8, 5},
    {SpiInstance::Spi5, Port::PortF, Pin::Pin9, 5},
    {SpiInstance::Spi5, Port::PortH, Pin::Pin6, 5},
    {SpiInstance::Spi5, Port::PortH, Pin::Pin7, 5},
    {SpiInstance::Spi6, Port::PortG, Pin::Pin12, 5},
    {SpiInstance::Spi6, Port::PortG, Pin::Pin13, 5},
    {SpiInstance::Spi6, Port::PortG, Pin::Pin14, 5},
    {SpiInstance::Spi6, Port::PortB, Pin::Pin3, 7},
    {SpiInstance::Spi6, Port::PortB, Pin::Pin4, 7},
    {SpiInstance::Spi6, Port::PortB, Pin::Pin5, 7},
    {SpiInstance::Spi6, Port::PortA, Pin::Pin5, 8},
    {SpiInstance::Spi6, Port::PortA, Pin::Pin6, 8},
    {SpiInstance::Spi6, Port::PortA, Pin::Pin7, 8},
};

static uint8_t lookupSpiAf(SpiInstance spi, Port port, Pin pin) {
  for (auto &entry : spiPinAfTable) {
    if (entry.spi == spi && entry.port == port && entry.pin == pin)
      return entry.af;
  }
  return 0;
}

#if defined(STM32H7)
DMA_BSS static std::array<SpiBus *, SPI_IRQ_GROUPS> spiBusInstance = {};

const std::array<SPI_TypeDef *, SPI_IRQ_GROUPS> spiInstance = {
    SPI1, SPI2, SPI3, SPI4, SPI5, SPI6};

constexpr std::array<IRQn_Type, SPI_IRQ_GROUPS> spiGroupIRQn = {
    SPI1_IRQn, SPI2_IRQn, SPI3_IRQn, SPI4_IRQn, SPI5_IRQn, SPI6_IRQn};
#endif

// ── DMA completion callbacks (forward decl for init()) ──
static void spiTxDmaComplete(void *context);
static void spiRxDmaComplete(void *context);

// ── DMAMUX request ID lookup (forward decl) ──
static bool spiDmaRequestIds(SpiInstance spix, uint32_t &txReq,
                             uint32_t &rxReq);

void enableBusSPIClock(SpiInstance spix) {
  using ClockFunc = void (*)();
  static const std::array<ClockFunc, 6> clockEnableTable = {{
#if defined(STM32H7)
      []() { __HAL_RCC_SPI1_CLK_ENABLE(); },
      []() { __HAL_RCC_SPI2_CLK_ENABLE(); },
      []() { __HAL_RCC_SPI3_CLK_ENABLE(); },
      []() { __HAL_RCC_SPI4_CLK_ENABLE(); },
      []() { __HAL_RCC_SPI5_CLK_ENABLE(); },
      []() { __HAL_RCC_SPI6_CLK_ENABLE(); },
#endif
  }};

  const auto index = static_cast<size_t>(spix);
  if (index < clockEnableTable.size()) {
    clockEnableTable[index]();
  }
}

SpiBus::SpiBus(SpiInstance spix, PinDesc clk, PinDesc mosi, PinDesc miso,
               PinDesc ncs) {
  _halHandle = new HalSpi();
  setType(Type::Spi);
  _desc.spix = spix;
  _desc.busPinDesc[static_cast<uint32_t>(SpiBusIO::CLK)] = clk;
  _desc.busPinDesc[static_cast<uint32_t>(SpiBusIO::MOSI)] = mosi;
  _desc.busPinDesc[static_cast<uint32_t>(SpiBusIO::MISO)] = miso;
  _desc.ncs = ncs;
}

SpiBus::SpiBus(const SpiDesc &desc) {
  _halHandle = new HalSpi();
  setType(Type::Spi);
  _desc = desc;
}

SpiBus::~SpiBus() {
  if (_dmaTx) {
    (void)_dmaTx->deinit();
    delete _dmaTx;
    _dmaTx = nullptr;
  }
  if (_dmaRx) {
    (void)_dmaRx->deinit();
    delete _dmaRx;
    _dmaRx = nullptr;
  }
  if (_halHandle) {
    delete static_cast<HalSpi *>(_halHandle);
    _halHandle = nullptr;
  }
}

void SpiBus::enableClock() {
  RCC_PeriphCLKInitTypeDef periphClkInitStruct;
  std::memset(&periphClkInitStruct, 0, sizeof(RCC_PeriphCLKInitTypeDef));

  switch (_desc.spix) {
  case SpiInstance::Spi1:
  case SpiInstance::Spi2:
  case SpiInstance::Spi3:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI123;
    periphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
    break;
  case SpiInstance::Spi4:
  case SpiInstance::Spi5:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI45;
    periphClkInitStruct.Spi45ClockSelection = RCC_SPI45CLKSOURCE_PLL2;
    break;
  case SpiInstance::Spi6:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI6;
    periphClkInitStruct.Spi6ClockSelection = RCC_SPI6CLKSOURCE_PLL2;
    break;
  }

  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);

  enableBusSPIClock(_desc.spix);
}

void SpiBus::configBufSize(uint32_t txBufSize, uint32_t rxBufSize) {
  if (txBufSize != 0)
    _bufSize = txBufSize;

  if (rxBufSize != 0)
    _bufSize = rxBufSize;
}

void SpiBus::configPins() {
#if defined(STM32H7)

  for (const auto &pinDesc : _desc.busPinDesc) {
    uint32_t alternate = lookupSpiAf(_desc.spix, pinDesc.port, pinDesc.pin);
    Gpio gpio(pinDesc);
    gpio.config(GPIO::Mode::AlternateFunctionPushPull, Pull::NoPull,
                Speed::High, alternate);
    gpio.init();
  }

  Gpio gpio(_desc.ncs);
  gpio.config(GPIO::Mode::OutputPushPull, Pull::NoPull, Speed::High);
  gpio.init();

#endif
}

void SpiBus::init() {
  if (_txBuf == nullptr || _rxBuf == nullptr) {
    return;
  }
  std::memset(_rxBuf, 0, _bufSize * sizeof(uint8_t));
  std::memset(_txBuf, 0, _bufSize * sizeof(uint8_t));

  enableClock();
  configPins();
#if defined(STM32H7)
  const auto spiIdx = static_cast<uint32_t>(_desc.spix);
  SPI_TypeDef *SPIx = spiInstance[spiIdx];
  HANDLE.Instance = SPIx;
  HANDLE.State = HAL_SPI_STATE_READY;

  // Clear any stale state from watchdog reset (CSTART, TSIZE)
  CLEAR_BIT(SPIx->CR1, SPI_CR1_CSTART);

  // LL SPI initialization
  LL_SPI_InitTypeDef spiInit = {};
  spiInit.TransferDirection = LL_SPI_FULL_DUPLEX;
  spiInit.Mode = LL_SPI_MODE_MASTER;
  spiInit.DataWidth = LL_SPI_DATAWIDTH_8BIT;
  spiInit.ClockPolarity = LL_SPI_POLARITY_LOW;
  spiInit.ClockPhase = LL_SPI_PHASE_1EDGE;
  spiInit.NSS = LL_SPI_NSS_SOFT;
  spiInit.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV2;
  spiInit.BitOrder = LL_SPI_MSB_FIRST;
  spiInit.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE;
  spiInit.CRCPoly = 0x0;
  (void)LL_SPI_Init(SPIx, &spiInit);

  // Configure additional CFG2 parameters not in LL_SPI_InitTypeDef
  LL_SPI_SetMasterSSIdleness(SPIx, LL_SPI_SS_IDLENESS_00CYCLE);
  LL_SPI_SetInterDataIdleness(SPIx, LL_SPI_ID_IDLENESS_00CYCLE);
  LL_SPI_SetFIFOThreshold(SPIx, LL_SPI_FIFO_TH_01DATA);
  LL_SPI_SetNSSPolarity(SPIx, LL_SPI_NSS_POLARITY_LOW);
  LL_SPI_EnableNSSPulseMgt(SPIx);

  // Enable DMA requests BEFORE SPI is enabled (per STM32H7 reference)
  LL_SPI_EnableDMAReq_TX(SPIx);
  LL_SPI_EnableDMAReq_RX(SPIx);

  if (spiIdx < spiBusInstance.size()) {
    if (spiBusInstance[spiIdx] != nullptr) {
      LOG_WARN("SPI%u already initialized, overwriting", spiIdx + 1);
    }
    spiBusInstance[spiIdx] = this;
  }
  const auto spiPrio =
      static_cast<uint32_t>(NVIC_EXTI::NvicPriority::PriorityMedium);
  HAL_NVIC_SetPriority(spiGroupIRQn[spiIdx], NVIC_PRIORITY_BASE(spiPrio),
                       NVIC_PRIORITY_SUB(spiPrio));
  HAL_NVIC_EnableIRQ(spiGroupIRQn[spiIdx]);
#endif

  Bus::init();
}

// ── Synchronous write (HAL polling) ──

Result SpiBus::writeSync(const uint8_t *data, uint16_t num) {
#if defined(STM32H7)
  if (!_initialized || !data || num == 0)
    return Result::InvalidParam;

  uint16_t offset = 0;
  while (offset < num) {
    uint16_t remaining = num - offset;
    uint16_t thisLen = (remaining < _bufSize) ? remaining : _bufSize;
    std::memcpy(_txBuf, data + offset, thisLen);
    if (HAL_SPI_Transmit(&HANDLE, _txBuf, thisLen, HAL_MAX_DELAY) != HAL_OK)
      return Result::Error;
    offset += thisLen;
  }
  return Result::Ok;
#else
  (void)data;
  (void)num;
  return Result::Error;
#endif
}

// ── Synchronous read (HAL polling) ──

Result SpiBus::readSync(uint8_t *data, uint16_t num) {
#if defined(STM32H7)
  if (!_initialized || !data || num == 0)
    return Result::InvalidParam;

  uint16_t offset = 0;
  while (offset < num) {
    uint16_t remaining = num - offset;
    uint16_t thisLen = (remaining < _bufSize) ? remaining : _bufSize;
    if (HAL_SPI_Receive(&HANDLE, data + offset, thisLen, HAL_MAX_DELAY) !=
        HAL_OK)
      return Result::Error;
    offset += thisLen;
  }
  return Result::Ok;
#else
  (void)data;
  (void)num;
  return Result::Error;
#endif
}

// ── Full-duplex transfer (SPI-specific, HAL polling) ──
// Handles NCS assertion/deassertion and simultaneous TX/RX.

Result SpiBus::transfer(const uint8_t *txData, uint8_t *rxData, uint16_t len) {
#if defined(STM32H7)
  if (!_initialized || len == 0) {
    return Result::InvalidParam;
  }

  // Assert NCS (chip select) — output low for entire multi-chunk transfer
  Gpio ncs(_desc.ncs);
  ncs.reset();

  // Determine chunk size
  uint16_t chunkSize = _bufSize;

  uint16_t offset = 0;
  while (offset < len) {
    uint16_t remaining = len - offset;
    uint16_t thisLen = (remaining < chunkSize) ? remaining : chunkSize;

    if (txData != nullptr && rxData != nullptr) {
      // Full-duplex: send txData, receive into rxData
      std::memcpy(_txBuf, txData + offset, thisLen);
      if (HAL_SPI_TransmitReceive(&HANDLE, _txBuf, rxData + offset, thisLen,
                                  HAL_MAX_DELAY) != HAL_OK) {
        ncs.set();
        return Result::Error;
      }
    } else if (txData != nullptr) {
      // TX only (MOSI only)
      std::memcpy(_txBuf, txData + offset, thisLen);
      if (HAL_SPI_Transmit(&HANDLE, _txBuf, thisLen, HAL_MAX_DELAY) != HAL_OK) {
        ncs.set();
        return Result::Error;
      }
    } else if (rxData != nullptr) {
      // RX only (send dummy 0xFF to clock in data)
      if (HAL_SPI_Receive(&HANDLE, rxData + offset, thisLen, HAL_MAX_DELAY) !=
          HAL_OK) {
        ncs.set();
        return Result::Error;
      }
    } else {
      // Both null: send dummy 0xFF bytes, discard RX
      std::memset(_txBuf, 0xFF, thisLen);
      if (HAL_SPI_Transmit(&HANDLE, _txBuf, thisLen, HAL_MAX_DELAY) != HAL_OK) {
        ncs.set();
        return Result::Error;
      }
    }

    offset += thisLen;
  }

  // De-assert NCS (chip select) — output high
  ncs.set();

  return Result::Ok;
#else
  (void)txData;
  (void)rxData;
  (void)len;
  return Result::Error;
#endif
}

// ── Simple microsecond delay for post-CMD spacing ──
// Calibrated for 400 MHz CPU: ~4 cycles per NOP + loop overhead.
static void spiDelayUs(uint32_t us) {
  // Each loop iteration approximates 10 CPU cycles ~ 25ns at 400MHz
  // so for 1µs we need ~40 iterations.  Scale linearly.
  uint32_t count = us * 40;
  while (count--) {
    __NOP();
  }
}

// ── Two-phase asynchronous transfer ──
// Phase 1: CMD via polling (HAL_SPI_Transmit)
// Phase 2: Data via DMA (TX dummy 0xFF, RX into caller buffer)

Result SpiBus::transferAsync(const uint8_t *cmdBuf, uint16_t cmdLen,
                             uint8_t *rxBuf, uint16_t dataLen,
                             uint8_t postCmdDelayUs,
                             void (*callback)(void *context), void *context) {
#if defined(STM32H7)
  if (!_initialized || !cmdBuf || cmdLen == 0 || !rxBuf || dataLen == 0) {
    return Result::InvalidParam;
  }
  if (dataLen > _bufSize || dataLen > static_cast<uint32_t>(Bus::_bufSize)) {
    return Result::InvalidParam;
  }

  // Check that a transfer is not already in flight
  if ((_dmaTx && _dmaTx->isBusy()) || (_dmaRx && _dmaRx->isBusy()) ||
      _transferCb != nullptr) {
    return Result::Busy;
  }

  // Validate SPI6 — uses BDMA, not supported by DmaManager
  uint32_t txReq = 0, rxReq = 0;
  if (!spiDmaRequestIds(_desc.spix, txReq, rxReq)) {
    return Result::Unsupported;
  }

  // Lazy-allocate DMA channels on first use
  if (!_dmaTx) {
    _dmaTx =
        DMA::DmaManager::getInstance().allocate(DMA::Controller::Dma2, txReq);
    if (!_dmaTx)
      return Result::Error;
    _dmaTx->configure({
        .direction = DMA::Direction::MemoryToPeripheral,
        .srcDataWidth = DMA::DataWidth::Byte,
        .destDataWidth = DMA::DataWidth::Byte,
        .priority = DMA::Priority::Medium,
        .srcIncrement = true,
        .destIncrement = false,
    });
    _dmaTx->setCallback(spiTxDmaComplete, this);
    (void)_dmaTx->init();
  }
  if (!_dmaRx) {
    _dmaRx =
        DMA::DmaManager::getInstance().allocate(DMA::Controller::Dma2, rxReq);
    if (!_dmaRx)
      return Result::Error;
    _dmaRx->configure({
        .direction = DMA::Direction::PeripheralToMemory,
        .srcDataWidth = DMA::DataWidth::Byte,
        .destDataWidth = DMA::DataWidth::Byte,
        .priority = DMA::Priority::Medium,
        .srcIncrement = false,
        .destIncrement = true,
    });
    _dmaRx->setCallback(spiRxDmaComplete, this);
    (void)_dmaRx->init();
  }

  // Save caller state
  _readDmaBuf = rxBuf;
  _readDmaLen = dataLen;
  _transferCb = callback;
  _transferCtx = context;

  // Assert NCS
  Gpio ncs(_desc.ncs);
  ncs.reset();

  // Phase 1: Send CMD+ADDR via LL polling
  {
    auto *spiX = HANDLE.Instance;
    // Ensure SPI is enabled (idempotent if already enabled from prior polling)
    LL_SPI_Enable(spiX);
    for (uint16_t i = 0; i < cmdLen; i++) {
      while (!LL_SPI_IsActiveFlag_TXP(spiX)) { }
      LL_SPI_TransmitData8(spiX, cmdBuf[i]);
    }
    // Wait for transfer complete
    while (!LL_SPI_IsActiveFlag_TXC(spiX)) { }
  }

  // Phase 2: Inter-command delay
  if (postCmdDelayUs > 0) {
    spiDelayUs(postCmdDelayUs);
  }

  // Phase 3: DMA data transfer — fill TX buffer with 0xFF dummy bytes
  std::memset(_txBuf, 0xFF, dataLen);

  auto *spiRegs = HANDLE.Instance;

  // Configure TSIZE and enable EOT interrupt (SPI will signal when done)
  LL_SPI_SetTransferSize(spiRegs, dataLen);
  LL_SPI_EnableIT_EOT(spiRegs);

  // Ensure SPI enabled (idempotent — Phase 1 polling left it on)
  LL_SPI_Enable(spiRegs);

  // Start RX DMA first (peripheral → memory)
  (void)_dmaRx->start(reinterpret_cast<uint32_t>(&spiRegs->RXDR),
                      reinterpret_cast<uint32_t>(_rxBuf), dataLen);

  // Then TX DMA (memory → peripheral, dummy bytes)
  (void)_dmaTx->start(reinterpret_cast<uint32_t>(_txBuf),
                      reinterpret_cast<uint32_t>(&spiRegs->TXDR), dataLen);

  // Start master transfer (CSTART)
  LL_SPI_StartMasterTransfer(spiRegs);

  return Result::Ok;
#else
  (void)cmdBuf;
  (void)cmdLen;
  (void)rxBuf;
  (void)dataLen;
  (void)postCmdDelayUs;
  (void)callback;
  (void)context;
  return Result::Error;
#endif
}

// ── DMAMUX request ID lookup for SPI DMA ──
// Returns DMAMUX request IDs for TX/RX on the given SPI instance.
// SPI6 uses BDMA (DMAMUX2) which is not supported by DmaManager;
// callers should check for SPI6 and return Unsupported.
static bool spiDmaRequestIds(SpiInstance spix, uint32_t &txReq,
                             uint32_t &rxReq) {
  switch (spix) {
  case SpiInstance::Spi1:
    txReq = DMA_REQUEST_SPI1_TX;
    rxReq = DMA_REQUEST_SPI1_RX;
    return true;
  case SpiInstance::Spi2:
    txReq = DMA_REQUEST_SPI2_TX;
    rxReq = DMA_REQUEST_SPI2_RX;
    return true;
  case SpiInstance::Spi3:
    txReq = DMA_REQUEST_SPI3_TX;
    rxReq = DMA_REQUEST_SPI3_RX;
    return true;
  case SpiInstance::Spi4:
    txReq = DMA_REQUEST_SPI4_TX;
    rxReq = DMA_REQUEST_SPI4_RX;
    return true;
  case SpiInstance::Spi5:
    txReq = DMA_REQUEST_SPI5_TX;
    rxReq = DMA_REQUEST_SPI5_RX;
    return true;
  // SPI6 uses BDMA — not supported by DmaManager
  default:
    return false;
  }
}

// ── SPI transfer complete handler (shared by EOT ISR and DMA TC) ──

static void spiTransferComplete(SpiBus *spi) {
  if (!spi) return;

  auto *hspi = &static_cast<HalSpi *>(spi->halHandle())->handle;

  // Disable EOT interrupt only (DMAEN stays set — configured once in init)
  LL_SPI_DisableIT_EOT(hspi->Instance);

  // Stop DMA channels so isBusy() returns false
  if (spi->_dmaTx) (void)spi->_dmaTx->stop();
  if (spi->_dmaRx) (void)spi->_dmaRx->stop();

  // Copy from DMA-safe _rxBuf to caller buffer (may be in DTCM)
  if (spi->_readDmaBuf && spi->_readDmaLen > 0) {
    std::memcpy(spi->_readDmaBuf, spi->rxBuf(), spi->_readDmaLen);
  }

  // De-assert NCS
  Gpio ncs(spi->ncsPinDesc());
  ncs.set();

  // Disable SPI — ready for next transfer
  LL_SPI_Disable(hspi->Instance);

  // Save and clear callback state before invoking
  auto cb = spi->_transferCb;
  auto ctx = spi->_transferCtx;
  spi->_readDmaBuf = nullptr;
  spi->_readDmaLen = 0;
  spi->_transferCb = nullptr;
  spi->_transferCtx = nullptr;

  if (cb) {
    cb(ctx);
  }
}

// ── Busy state (DMA status) ──

bool SpiBus::isBusy() const {
#if defined(STM32H7)
  return isTxBusy() || isRxBusy();
#else
  return false;
#endif
}

bool SpiBus::isTxBusy() const {
#if defined(STM32H7)
  return _dmaTx && _dmaTx->isBusy();
#else
  return false;
#endif
}

bool SpiBus::isRxBusy() const {
#if defined(STM32H7)
  return _dmaRx && _dmaRx->isBusy();
#else
  return false;
#endif
}

// ── DMA completion callbacks ──

static void spiTxDmaComplete(void *context) {
  auto *spi = static_cast<SpiBus *>(context);
  if (!spi)
    return;
  // TX DMA complete — nothing to clean (DMAEN stays set from init)
}

static void spiRxDmaComplete(void *context) {
  auto *spi = static_cast<SpiBus *>(context);
  if (!spi)
    return;

  // Stop DMA channels so isBusy() returns false
  if (spi->_dmaTx)
    (void)spi->_dmaTx->stop();
  if (spi->_dmaRx)
    (void)spi->_dmaRx->stop();

  // Copy from DMA-safe _rxBuf to caller buffer (may be in DTCM)
  if (spi->_readDmaBuf && spi->_readDmaLen > 0) {
    std::memcpy(spi->_readDmaBuf, spi->rxBuf(), spi->_readDmaLen);
  }

  // De-assert NCS
  Gpio ncs(spi->ncsPinDesc());
  ncs.set();

  // Save and clear callback state before invoking
  auto cb = spi->_transferCb;
  auto ctx = spi->_transferCtx;
  spi->_readDmaBuf = nullptr;
  spi->_readDmaLen = 0;
  spi->_transferCb = nullptr;
  spi->_transferCtx = nullptr;

  if (cb) {
    cb(ctx);
  }
}

extern "C" {

#if defined(STM32H7)

static void SPIx_IRQHandler(uint32_t spiIdx) {
  auto *instance = spiBusInstance[spiIdx];
  if (!instance) {
    return;
  }

  auto *SPIx = spiInstance[spiIdx];

  // ── End of Transfer (DMA complete) ──
  if (LL_SPI_IsActiveFlag_EOT(SPIx)) {
    LL_SPI_ClearFlag_EOT(SPIx);
    // If we have a pending async transfer, finalize it
    if (instance->_transferCb != nullptr) {
      spiTransferComplete(instance);
    }
    return; // EOT is terminal — no further processing needed
  }

  // ── Error handling (LL) ──
  if (LL_SPI_IsActiveFlag_OVR(SPIx)) {
    LL_SPI_ClearFlag_OVR(SPIx);
  }
  if (LL_SPI_IsActiveFlag_MODF(SPIx)) {
    LL_SPI_ClearFlag_MODF(SPIx);
  }

  // TODO: Handle data transfer interrupts (TXP, RXP, DXP) once
  //       interrupt-driven SPI is implemented alongside DMA.
  //       For now, polling mode handles all data movement.
}

#define spiIrqHandler(name, idx)                                               \
  void name(void) { SPIx_IRQHandler(idx); }                                    \
  struct dummy

spiIrqHandler(SPI1_IRQHandler, 0);
spiIrqHandler(SPI2_IRQHandler, 1);
spiIrqHandler(SPI3_IRQHandler, 2);
spiIrqHandler(SPI4_IRQHandler, 3);
spiIrqHandler(SPI5_IRQHandler, 4);
spiIrqHandler(SPI6_IRQHandler, 5);

#endif
}
