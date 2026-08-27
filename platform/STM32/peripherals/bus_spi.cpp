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

#include "build_info.h"

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/dma.h"
#include "drivers/peripherals/dmamgr.h"
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

#define SPI_HANDLE (static_cast<HalSpi *>(_halHandle)->handle)


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
COMMON_ZERO_INIT static std::array<SpiBus *, SPI_IRQ_GROUPS> spiBusInstance = {};

const std::array<SPI_TypeDef *, SPI_IRQ_GROUPS> spiInstance = {
    SPI1, SPI2, SPI3, SPI4, SPI5, SPI6};

constexpr std::array<IRQn_Type, SPI_IRQ_GROUPS> spiGroupIRQn = {
    SPI1_IRQn, SPI2_IRQn, SPI3_IRQn, SPI4_IRQn, SPI5_IRQn, SPI6_IRQn};
#endif

// ── DMA dummy targets (for TX/RX when no src/dst provided) ────

COMMON_ZERO_INIT static uint8_t s_dmaDummyTxByte = 0xFF;
COMMON_ZERO_INIT static uint8_t s_dmaDummyRxByte;


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
  _desc.ncs = Gpio(ncs);
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
  default:
    break;
  }

  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);

  enableBusSPIClock(_desc.spix);
}

void SpiBus::configPins() {
#if defined(STM32H7)
  _desc.ncs.config(GPIO::Mode::OutputPushPull, Pull::NoPull, Speed::High);
  _desc.ncs.init();
  _desc.ncs.set();

  for (const auto &pinDesc : _desc.busPinDesc) {
    uint32_t alternate = lookupSpiAf(_desc.spix, pinDesc.port, pinDesc.pin);
    Gpio gpio(pinDesc);
    gpio.config(GPIO::Mode::AlternateFunctionPushPull, Pull::NoPull,
                Speed::High, alternate);
    gpio.init();
  }
#endif
}


void SpiBus::init() {
  if (_txBuf == nullptr || _rxBuf == nullptr) {
    LOG_ERROR("SPI%u: init skipped (txBuf=%p rxBuf=%p)",
              static_cast<unsigned>(_desc.spix) + 1,
              static_cast<void *>(_txBuf), static_cast<void *>(_rxBuf));
    return;
  }
  std::memset(_rxBuf, 0, _bufSize * sizeof(uint8_t));
  std::memset(_txBuf, 0, _bufSize * sizeof(uint8_t));

  enableClock();
  configPins();
#if defined(STM32H7)
  const auto spiIdx = static_cast<uint32_t>(_desc.spix);
  std::memset(&SPI_HANDLE, 0, sizeof(SPI_HANDLE));
  SPI_HANDLE.Instance = spiInstance[spiIdx];

  switch (_desc.spix) {
  case SpiInstance::Spi1: __HAL_RCC_SPI1_FORCE_RESET(); break;
  case SpiInstance::Spi2: __HAL_RCC_SPI2_FORCE_RESET(); break;
  case SpiInstance::Spi3: __HAL_RCC_SPI3_FORCE_RESET(); break;
  case SpiInstance::Spi4: __HAL_RCC_SPI4_FORCE_RESET(); break;
  case SpiInstance::Spi5: __HAL_RCC_SPI5_FORCE_RESET(); break;
  case SpiInstance::Spi6: __HAL_RCC_SPI6_FORCE_RESET(); break;
  default: break;
  }
  __NOP(); __NOP();
  switch (_desc.spix) {
  case SpiInstance::Spi1: __HAL_RCC_SPI1_RELEASE_RESET(); break;
  case SpiInstance::Spi2: __HAL_RCC_SPI2_RELEASE_RESET(); break;
  case SpiInstance::Spi3: __HAL_RCC_SPI3_RELEASE_RESET(); break;
  case SpiInstance::Spi4: __HAL_RCC_SPI4_RELEASE_RESET(); break;
  case SpiInstance::Spi5: __HAL_RCC_SPI5_RELEASE_RESET(); break;
  case SpiInstance::Spi6: __HAL_RCC_SPI6_RELEASE_RESET(); break;
  default: break;
  }
  __NOP(); __NOP();
  SPI_HANDLE.Init.Mode = SPI_MODE_MASTER;
  SPI_HANDLE.Init.Direction = SPI_DIRECTION_2LINES;
  SPI_HANDLE.Init.DataSize = SPI_DATASIZE_8BIT;
  SPI_HANDLE.Init.CLKPolarity = SPI_POLARITY_LOW;
  SPI_HANDLE.Init.CLKPhase = SPI_PHASE_1EDGE;
  SPI_HANDLE.Init.NSS = SPI_NSS_SOFT;
  SPI_HANDLE.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  SPI_HANDLE.Init.FirstBit = SPI_FIRSTBIT_MSB;
  SPI_HANDLE.Init.TIMode = SPI_TIMODE_DISABLE;
  SPI_HANDLE.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  SPI_HANDLE.Init.CRCPolynomial = 0x0;
  SPI_HANDLE.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;

  SPI_HANDLE.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  SPI_HANDLE.Init.TxCRCInitializationPattern =
      SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  SPI_HANDLE.Init.RxCRCInitializationPattern =
      SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  SPI_HANDLE.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  SPI_HANDLE.Init.MasterInterDataIdleness =
      SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  SPI_HANDLE.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  SPI_HANDLE.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  SPI_HANDLE.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&SPI_HANDLE) != HAL_OK) {
    LOG_ERROR("SPI%u init failed", spiIdx + 1);
    return;
  }

  const auto spiPrio =
      static_cast<uint32_t>(NVIC_EXTI::NvicPriority::PriorityMedium);
  HAL_NVIC_SetPriority(spiGroupIRQn[spiIdx], NVIC_PRIORITY_BASE(spiPrio),
                       NVIC_PRIORITY_SUB(spiPrio));
  HAL_NVIC_EnableIRQ(spiGroupIRQn[spiIdx]);

  uint32_t txRequestId = 0;
  uint32_t rxRequestId = 0;
  switch (_desc.spix) {
  case SpiInstance::Spi1:
      txRequestId = DMA_REQUEST_SPI1_TX;
      rxRequestId = DMA_REQUEST_SPI1_RX;
      break;
    case SpiInstance::Spi2:
      txRequestId = DMA_REQUEST_SPI2_TX;
      rxRequestId = DMA_REQUEST_SPI2_RX;
      break;
    case SpiInstance::Spi3:
      txRequestId = DMA_REQUEST_SPI3_TX;
      rxRequestId = DMA_REQUEST_SPI3_RX;
      break;
    case SpiInstance::Spi4:
      txRequestId = DMA_REQUEST_SPI4_TX;
      rxRequestId = DMA_REQUEST_SPI4_RX;
      break;
    case SpiInstance::Spi5:
      txRequestId = DMA_REQUEST_SPI5_TX;
      rxRequestId = DMA_REQUEST_SPI5_RX;
      break;
    case SpiInstance::Spi6:
      break;
    default:
      break;
    }

    _dmaTx = DMA::DmaManager::getInstance().allocate(DMA::Controller::Dma1,
                                                     txRequestId);
    if (_dmaTx) {
      _dmaTx->configure({
          .direction = DMA::Direction::MemoryToPeripheral,
          .srcDataWidth = DMA::DataWidth::Byte,
          .destDataWidth = DMA::DataWidth::Byte,
          .priority = DMA::Priority::Medium,
          .srcIncrement = true,
          .destIncrement = false,
      });
      (void)_dmaTx->init();
    } else {
      LOG_WARN("SPI%u: DMA TX allocation failed", spiIdx + 1);
    }

    _dmaRx = DMA::DmaManager::getInstance().allocate(DMA::Controller::Dma1,
                                                     rxRequestId);
    if (_dmaRx) {
      _dmaRx->configure({
          .direction = DMA::Direction::PeripheralToMemory,
          .srcDataWidth = DMA::DataWidth::Byte,
          .destDataWidth = DMA::DataWidth::Byte,
          .priority = DMA::Priority::Medium,
          .srcIncrement = false,
          .destIncrement = true,
      });
      (void)_dmaRx->init();
    } else {
      LOG_WARN("SPI%u: DMA RX allocation failed", spiIdx + 1);
    }

    if (_dmaTx && _dmaRx) {
      spiBusInstance[spiIdx] = this;
    }
#endif

  Bus::init();
}

bool SpiBus::isBusy() const {
  return (_dmaTx && _dmaTx->isBusy()) || (_dmaRx && _dmaRx->isBusy());
}

// Betaflight-style internal DMA primitives

void SpiBus::spiInternalInitStream(const uint8_t *txData, uint8_t *rxData,
                                    uint16_t len) {
  _streamTxLen = 0;
  _streamRxLen = 0;

  if (_dmaTx) {
    if (txData) {
      _streamTxSrc = reinterpret_cast<uint32_t>(txData);
      _streamTxInc = true;
    } else {
      _streamTxSrc = reinterpret_cast<uint32_t>(&s_dmaDummyTxByte);
      _streamTxInc = false;
    }
    _streamTxLen = len;
  }

  if (_dmaRx) {
    if (rxData) {
      _streamRxDst = reinterpret_cast<uint32_t>(rxData);
      _streamRxInc = true;
    } else {
      _streamRxDst = reinterpret_cast<uint32_t>(&s_dmaDummyRxByte);
      _streamRxInc = false;
    }
    _streamRxLen = len;
  }
}

void SpiBus::spiInternalStartDMA(TransferCallback cb, void *ctx,
                                  uint16_t totalLen) {
  auto *SPIx = SPI_HANDLE.Instance;

  if (_dmaTx && _streamTxLen > 0) {
    _dmaTx->stop();
    _dmaTx->configure({
        .direction = DMA::Direction::MemoryToPeripheral,
        .srcDataWidth = DMA::DataWidth::Byte,
        .destDataWidth = DMA::DataWidth::Byte,
        .priority = DMA::Priority::Medium,
        .srcIncrement = _streamTxInc,
        .destIncrement = false,
    });
    _dmaTx->start(_streamTxSrc,
                  reinterpret_cast<uint32_t>(&SPIx->TXDR),
                  _streamTxLen);
  }

  if (_dmaRx && _streamRxLen > 0) {
    _dmaRx->stop();
    _dmaRx->configure({
        .direction = DMA::Direction::PeripheralToMemory,
        .srcDataWidth = DMA::DataWidth::Byte,
        .destDataWidth = DMA::DataWidth::Byte,
        .priority = DMA::Priority::Medium,
        .srcIncrement = false,
        .destIncrement = _streamRxInc,
    });
    _dmaRx->start(reinterpret_cast<uint32_t>(&SPIx->RXDR),
                  _streamRxDst,
                  _streamRxLen);
  }

  MODIFY_REG(SPIx->CR2, SPI_CR2_TSIZE, totalLen);
  SET_BIT(SPIx->CFG1, SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN);
  SET_BIT(SPIx->IER, SPI_IER_EOTIE);
  SET_BIT(SPIx->CR1, SPI_CR1_SPE);
  SET_BIT(SPIx->CR1, SPI_CR1_CSTART);

  _asyncCb = cb;
  _asyncCtx = ctx;
}

void SpiBus::spiInternalStopDMA() {
  auto *SPIx = SPI_HANDLE.Instance;

  CLEAR_BIT(SPIx->CFG1, SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN);
  CLEAR_BIT(SPIx->IER, SPI_IER_EOTIE);
  CLEAR_BIT(SPIx->CR1, SPI_CR1_SPE);

  if (_dmaTx) { (void)_dmaTx->stop(); }
  if (_dmaRx) { (void)_dmaRx->stop(); }
}

// Polling fallback (LL, no DMA)

Result SpiBus::spiInternalReadWriteBufPolled(const uint8_t *txData,
                                              uint8_t *rxData,
                                              uint16_t len) {
#if defined(STM32H7)
  auto *SPIx = SPI_HANDLE.Instance;
  LL_SPI_SetTransferSize(SPIx, len);
  LL_SPI_Enable(SPIx);
  LL_SPI_StartMasterTransfer(SPIx);

  while (len > 0) {
    while (!LL_SPI_IsActiveFlag_TXP(SPIx)) {}
    uint8_t b = txData ? *(txData++) : 0xFF;
    LL_SPI_TransmitData8(SPIx, b);

    while (!LL_SPI_IsActiveFlag_RXP(SPIx)) {}
    b = LL_SPI_ReceiveData8(SPIx);
    if (rxData) { *(rxData++) = b; }
    --len;
  }

  while (!LL_SPI_IsActiveFlag_EOT(SPIx)) {}
  LL_SPI_ClearFlag_EOT(SPIx);
  LL_SPI_ClearFlag_TXTF(SPIx);
  LL_SPI_Disable(SPIx);
#endif
  return Result::Ok;
}

// transmitReceiveImpl — the single hook, dispatched by _mode

Result SpiBus::transmitReceiveImpl(TransferCallback cb, void *ctx,
                             const uint8_t *txData, uint8_t *rxData,
                             uint16_t len) {
  if (len == 0) return Result::InvalidParam;
  if (!_initialized) return Result::NotReady;

  if (_mode == Mode::Polling) {
    if (cb != nullptr)     return Result::Unsupported;
    return spiInternalReadWriteBufPolled(txData, rxData, len);
  }

  if (!_dmaTx || !_dmaRx) {
    // Fallback: LL polling (no DMA available)
    if (cb != nullptr) return Result::Unsupported;
    return spiInternalReadWriteBufPolled(txData, rxData, len);
  }

  _asyncSrc = txData;
  _asyncDst = rxData;
  _asyncSrcOrig = txData;
  _asyncDstOrig = rxData;
  _asyncRemaining = len;
  _asyncTotalLen = len;

  uint16_t chunk = (len > static_cast<uint16_t>(_bufSize))
                       ? static_cast<uint16_t>(_bufSize)
                       : len;

  if (txData) {
    (void)std::memcpy(_txBuf, txData, chunk);
  } else {
    (void)std::memset(_txBuf, 0xFF, chunk);
  }

  _asyncChunkLen = chunk;
  _asyncRemaining -= chunk;
  if (txData) { _asyncSrc += chunk; }

  _spiTransferDone = false;

  spiInternalInitStream(_txBuf, _rxBuf, chunk);
  spiInternalStartDMA(cb, ctx, chunk);

  if (cb == nullptr) {
    while (!_spiTransferDone) {}
    return Result::Ok;
  }

  return Result::Ok;
}

// EOT ISR handler — chunk chaining + Repeat support

extern "C" {

#if defined(STM32H7)

static void SPIx_IRQHandler(uint32_t spiIdx) {
  auto *spi = spiBusInstance[spiIdx];
  if (!spi) return;

  auto *hal = static_cast<HalSpi *>(spi->halHandle());
  auto *SPIx = hal->handle.Instance;
  uint32_t sr = SPIx->SR;

  if (sr & SPI_SR_EOT) {
    SET_BIT(SPIx->IFCR, SPI_IFCR_EOTC);
    SET_BIT(SPIx->IFCR, SPI_IFCR_TXTFC);

    CLEAR_BIT(SPIx->CFG1, SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN);
    CLEAR_BIT(SPIx->IER, SPI_IER_EOTIE);
    CLEAR_BIT(SPIx->CR1, SPI_CR1_SPE);

    if (spi->_dmaTx) { (void)spi->_dmaTx->stop(); }
    if (spi->_dmaRx) { (void)spi->_dmaRx->stop(); }

    // Copy received data from internal RX buffer to caller's dst
    // (only for multi-chunk — first chunk is in _rxBuf, subsequent
    //  chunks also land in _rxBuf and get memcpy'd here)
    if (spi->_asyncDst && spi->_asyncChunkLen > 0) {
      (void)std::memcpy(spi->_asyncDst, spi->rxBuf(),
                         spi->_asyncChunkLen);
    }

    if (spi->_asyncRemaining > 0) {
      uint16_t chunk =
          (spi->_asyncRemaining > static_cast<uint16_t>(spi->bufSize()))
              ? static_cast<uint16_t>(spi->bufSize())
              : spi->_asyncRemaining;

      if (spi->_asyncSrc) {
        (void)std::memcpy(spi->txBuf(), spi->_asyncSrc, chunk);
        spi->_asyncSrc += chunk;
      } else {
        (void)std::memset(spi->txBuf(), 0xFF, chunk);
      }

      auto *spiDst = spi->_asyncDst;
      if (spiDst) spiDst += spi->_asyncChunkLen;

      spi->_asyncChunkLen = chunk;
      spi->_asyncRemaining -= chunk;
      spi->_asyncDst = spiDst;

      spi->spiInternalInitStream(spi->txBuf(), spi->rxBuf(), chunk);
      spi->spiInternalStartDMA(spi->_asyncCb, spi->_asyncCtx, chunk);
      return;
    }

    // ── All chunks complete — check callback for Repeat ────
    bool repeat = false;
    if (spi->_asyncCb) {
      auto status = spi->_asyncCb(spi->_asyncCtx);
      if (status == TransferStatus::Repeat) {
        repeat = true;
      }
    }

    if (repeat) {
      spi->_asyncSrc = spi->_asyncSrcOrig;
      spi->_asyncDst = spi->_asyncDstOrig;
      spi->_asyncRemaining = spi->_asyncTotalLen;

      uint16_t chunk =
          (spi->_asyncTotalLen > static_cast<uint16_t>(spi->bufSize()))
              ? static_cast<uint16_t>(spi->bufSize())
              : spi->_asyncTotalLen;

      if (spi->_asyncSrcOrig) {
        (void)std::memcpy(spi->txBuf(), spi->_asyncSrcOrig, chunk);
        spi->_asyncSrc += chunk;
      } else {
        (void)std::memset(spi->txBuf(), 0xFF, chunk);
      }
      spi->_asyncDst = spi->_asyncDstOrig;
      if (spi->_asyncDst) { spi->_asyncDst += chunk; }
      spi->_asyncChunkLen = chunk;
      spi->_asyncRemaining -= chunk;

      spi->spiInternalInitStream(spi->txBuf(), spi->rxBuf(), chunk);
      spi->spiInternalStartDMA(spi->_asyncCb, spi->_asyncCtx, chunk);
    } else {
      spi->_spiTransferDone = true;
    }
  }

  if (sr & (SPI_SR_OVR | SPI_SR_MODF | SPI_SR_UDR | SPI_SR_TIFRE)) {
    SET_BIT(SPIx->IFCR, SPI_IFCR_OVRC | SPI_IFCR_MODFC);
    SET_BIT(SPIx->IFCR, SPI_IFCR_UDRC | SPI_IFCR_TIFREC);
  }
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

} // extern "C"
