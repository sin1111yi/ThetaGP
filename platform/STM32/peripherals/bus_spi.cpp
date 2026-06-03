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
DMA_BSS static std::array<SpiBus *, SPI_IRQ_GROUPS> spiBusInstance = {};

const std::array<SPI_TypeDef *, SPI_IRQ_GROUPS> spiInstance = {
    SPI1, SPI2, SPI3, SPI4, SPI5, SPI6};

constexpr std::array<IRQn_Type, SPI_IRQ_GROUPS> spiGroupIRQn = {
    SPI1_IRQn, SPI2_IRQn, SPI3_IRQn, SPI4_IRQn, SPI5_IRQn, SPI6_IRQn};
#endif

// ── DMA completion callbacks (forward decl for init()) ──
static void spiTxDmaComplete(void *context);
static void spiRxDmaComplete(void *context);

// ── DMAMUX request ID lookup (forward decl, no longer used after polling
// refactor) ── static bool spiDmaRequestIds(...); Removed after transferAsync
// stubbed.

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

  for (const auto &pinDesc : _desc.busPinDesc) {
    uint32_t alternate = lookupSpiAf(_desc.spix, pinDesc.port, pinDesc.pin);
    Gpio gpio(pinDesc);
    gpio.config(GPIO::Mode::AlternateFunctionPushPull, Pull::NoPull,
                Speed::High, alternate);
    gpio.init();
  }

  _desc.ncs.config(GPIO::Mode::OutputPushPull, Pull::NoPull, Speed::High);
  _desc.ncs.init();

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
  SPI_HANDLE.Instance = spiInstance[spiIdx];
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

  if (_mode == Mode::Asynchronous) {
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
      // unsupport fallback to Synchronous
      break;
    default:
      break;
    }

    // TX
    _dmaTx = DMA::DmaManager::getInstance().allocate(DMA::Controller::Dma1,
                                                     txRequestId);
    if (!_dmaTx) { /* handle error */
    }
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

    // RX
    _dmaRx = DMA::DmaManager::getInstance().allocate(DMA::Controller::Dma1,
                                                     rxRequestId);
    if (!_dmaRx) { /* handle error */
    }
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
#endif

  Bus::init();
}

// ── Synchronous write (delegates to transfer) ──

Result SpiBus::writeSync(const uint8_t *data, uint16_t num) {
  return transfer(data, nullptr, num);
}

// ── Synchronous read (delegates to transfer) ──

Result SpiBus::readSync(uint8_t *data, uint16_t num) {
  return transfer(nullptr, data, num);
}

// ── Full-duplex transfer (LL polling) ──
Result SpiBus::transfer(const uint8_t *txData, uint8_t *rxData, uint16_t len) {
#if defined(STM32H7)
  if (!_initialized || len == 0)
    return Result::InvalidParam;

  _desc.ncs.reset();

  auto *SPIx = SPI_HANDLE.Instance;

  // ── Stage TX data into internal buffer ──
  // If txData is provided, copy into _txBuf; otherwise fill with 0xFF.
  if (txData != nullptr) {
    (void)std::memcpy(_txBuf, txData, len);
  } else {
    (void)std::memset(_txBuf, 0xFF, len);
  }

  // ── Enhanced mode: TSIZE + SPE + CSTART ──
  MODIFY_REG(SPIx->CR2, SPI_CR2_TSIZE, len);
  SET_BIT(SPIx->CR1, SPI_CR1_SPE);
  SET_BIT(SPIx->CR1, SPI_CR1_CSTART);

  uint16_t txCnt = len;
  uint16_t rxCnt = len;
  const uint8_t *txSrc = _txBuf;
  uint8_t *rxDst = _rxBuf;

  // FIFO depth for STM32H7 high-end SPI (all instances): 16 data items
  constexpr uint16_t kSpiFifoDepth = 16U;

  while (txCnt > 0U || rxCnt > 0U) {
    // ── TX path ──
    if (LL_SPI_IsActiveFlag_TXP(SPIx) && txCnt > 0U &&
        (rxCnt < txCnt + kSpiFifoDepth)) {
      LL_SPI_TransmitData8(SPIx, *txSrc);
      txSrc++;
      txCnt--;
    }

    // ── RX path ──
    if (rxCnt > 0U && LL_SPI_IsActiveFlag_RXP(SPIx)) {
      *rxDst = LL_SPI_ReceiveData8(SPIx);
      rxDst++;
      rxCnt--;
    }
  }

  // Wait for End-of-Transfer
  while (!LL_SPI_IsActiveFlag_EOT(SPIx)) {
  }

  // ── Cleanup ──
  LL_SPI_ClearFlag_EOT(SPIx);
  LL_SPI_ClearFlag_TXTF(SPIx);
  CLEAR_BIT(SPIx->CR1, SPI_CR1_SPE);

  // ── Copy received data back to caller (if requested) ──
  if (rxData != nullptr) {
    (void)std::memcpy(rxData, _rxBuf, len);
  }

  _desc.ncs.set();
  return Result::Ok;
#else
  (void)txData;
  (void)rxData;
  (void)len;
  return Result::Error;
#endif
}

Result SpiBus::transferAsync(const uint8_t *cmdBuf, uint16_t cmdLen,
                             uint8_t *rxBuf, uint16_t dataLen,
                             uint8_t postCmdDelayUs,
                             void (*callback)(void *context), void *context) {
  (void)cmdBuf;
  (void)cmdLen;
  (void)rxBuf;
  (void)dataLen;
  (void)postCmdDelayUs;
  (void)callback;
  (void)context;
  return Result::Unsupported;
}

static void spiTransferComplete(SpiBus *spi) { (void)spi; }

bool SpiBus::isBusy() const { return false; }

bool SpiBus::isTxBusy() const { return false; }

bool SpiBus::isRxBusy() const { return false; }

// ── DMA completion callbacks ──

static void spiTxDmaComplete(void *context) {}

static void spiRxDmaComplete(void *context) {}

extern "C" {

#if defined(STM32H7)

static void SPIx_IRQHandler(uint32_t spiIdx) {
  auto *instance = spiBusInstance[spiIdx];
  if (!instance) {
    return;
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
}
