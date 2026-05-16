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
#include "drivers/peripherals/gpio.h"

#include <array>
#include <cstring>

#if defined(STM32H7)
#define SPI_IRQ_GROUPS 6
#endif

using namespace ThetaGP::Drivers::Peripheral::BUS;
using namespace ThetaGP::Drivers::Peripheral::GPIO;
using ThetaGP::RetVal;

struct HalSpi {
  SPI_HandleTypeDef handle;
};

#define HANDLE (static_cast<HalSpi *>(_halHandle)->handle)

static constexpr struct {
  Instance spi;
  Port port;
  Pin pin;
  uint8_t af;
} spiPinAfTable[] = {
    {Instance::Spi1, Port::PortA, Pin::Pin5, 5},
    {Instance::Spi1, Port::PortA, Pin::Pin6, 5},
    {Instance::Spi1, Port::PortA, Pin::Pin7, 5},
    {Instance::Spi1, Port::PortB, Pin::Pin3, 5},
    {Instance::Spi1, Port::PortB, Pin::Pin4, 5},
    {Instance::Spi1, Port::PortB, Pin::Pin5, 5},
    {Instance::Spi1, Port::PortE, Pin::Pin13, 5},
    {Instance::Spi1, Port::PortE, Pin::Pin14, 5},
    {Instance::Spi1, Port::PortE, Pin::Pin15, 5},
    {Instance::Spi2, Port::PortB, Pin::Pin10, 5},
    {Instance::Spi2, Port::PortB, Pin::Pin13, 5},
    {Instance::Spi2, Port::PortB, Pin::Pin14, 5},
    {Instance::Spi2, Port::PortB, Pin::Pin15, 5},
    {Instance::Spi2, Port::PortC, Pin::Pin2, 5},
    {Instance::Spi2, Port::PortD, Pin::Pin1, 6},
    {Instance::Spi2, Port::PortD, Pin::Pin3, 6},
    {Instance::Spi2, Port::PortD, Pin::Pin4, 6},
    {Instance::Spi2, Port::PortI, Pin::Pin1, 5},
    {Instance::Spi2, Port::PortI, Pin::Pin2, 5},
    {Instance::Spi2, Port::PortI, Pin::Pin3, 5},
    {Instance::Spi3, Port::PortB, Pin::Pin3, 6},
    {Instance::Spi3, Port::PortB, Pin::Pin4, 6},
    {Instance::Spi3, Port::PortB, Pin::Pin5, 6},
    {Instance::Spi3, Port::PortC, Pin::Pin10, 6},
    {Instance::Spi3, Port::PortC, Pin::Pin11, 6},
    {Instance::Spi3, Port::PortC, Pin::Pin12, 6},
    {Instance::Spi3, Port::PortD, Pin::Pin6, 5},
    {Instance::Spi4, Port::PortE, Pin::Pin2, 5},
    {Instance::Spi4, Port::PortE, Pin::Pin5, 5},
    {Instance::Spi4, Port::PortE, Pin::Pin6, 5},
    {Instance::Spi4, Port::PortE, Pin::Pin12, 5},
    {Instance::Spi4, Port::PortE, Pin::Pin13, 5},
    {Instance::Spi4, Port::PortE, Pin::Pin14, 5},
    {Instance::Spi5, Port::PortF, Pin::Pin7, 5},
    {Instance::Spi5, Port::PortF, Pin::Pin8, 5},
    {Instance::Spi5, Port::PortF, Pin::Pin9, 5},
    {Instance::Spi5, Port::PortH, Pin::Pin6, 5},
    {Instance::Spi5, Port::PortH, Pin::Pin7, 5},
    {Instance::Spi6, Port::PortG, Pin::Pin12, 5},
    {Instance::Spi6, Port::PortG, Pin::Pin13, 5},
    {Instance::Spi6, Port::PortG, Pin::Pin14, 5},
    {Instance::Spi6, Port::PortB, Pin::Pin3, 7},
    {Instance::Spi6, Port::PortB, Pin::Pin4, 7},
    {Instance::Spi6, Port::PortB, Pin::Pin5, 7},
    {Instance::Spi6, Port::PortA, Pin::Pin5, 8},
    {Instance::Spi6, Port::PortA, Pin::Pin6, 8},
    {Instance::Spi6, Port::PortA, Pin::Pin7, 8},
};

static uint8_t lookupSpiAf(Instance spi, Port port, Pin pin) {
  for (auto &entry : spiPinAfTable) {
    if (entry.spi == spi && entry.port == port && entry.pin == pin)
      return entry.af;
  }
  return 0;
}

#if defined(STM32H7)
static std::array<SpiBus *, SPI_IRQ_GROUPS> spiBusInstance = {};

const std::array<SPI_TypeDef *, SPI_IRQ_GROUPS> spiInstance = {
    SPI1, SPI2, SPI3, SPI4, SPI5, SPI6};

constexpr std::array<IRQn_Type, SPI_IRQ_GROUPS> spiGroupIRQn = {
    SPI1_IRQn, SPI2_IRQn, SPI3_IRQn, SPI4_IRQn, SPI5_IRQn, SPI6_IRQn};
#endif

void enableBusSPIClock(Instance spix) {
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

SpiBus::SpiBus(Instance spix, PinDesc clk, PinDesc mosi, PinDesc miso,
               PinDesc ncs) {
  _halHandle = new HalSpi();
  setType(Type::Spi);
  _desc.spix = spix;
  _desc.busPinDesc[static_cast<uint32_t>(SpiBusIO::CLK)] = clk;
  _desc.busPinDesc[static_cast<uint32_t>(SpiBusIO::MOSI)] = mosi;
  _desc.busPinDesc[static_cast<uint32_t>(SpiBusIO::MISO)] = miso;
  _desc.ncs = ncs;

  _pTxBufSize = _bufSize;
  _pRxBufSize = _bufSize;
}

SpiBus::SpiBus(const SpiDesc &desc) {
  _halHandle = new HalSpi();
  setType(Type::Spi);
  _desc = desc;

  _pTxBufSize = _bufSize;
  _pRxBufSize = _bufSize;
}

SpiBus::~SpiBus() {
  if (_halHandle) {
    delete static_cast<HalSpi *>(_halHandle);
    _halHandle = nullptr;
  }
}

void SpiBus::enableClock() const {
  RCC_PeriphCLKInitTypeDef periphClkInitStruct;
  std::memset(&periphClkInitStruct, 0, sizeof(RCC_PeriphCLKInitTypeDef));

  switch (_desc.spix) {
  case Instance::Spi1:
  case Instance::Spi2:
  case Instance::Spi3:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI123;
    periphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
    break;
  case Instance::Spi4:
  case Instance::Spi5:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI45;
    periphClkInitStruct.Spi45ClockSelection = RCC_SPI45CLKSOURCE_PLL2;
    break;
  case Instance::Spi6:
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI6;
    periphClkInitStruct.Spi6ClockSelection = RCC_SPI6CLKSOURCE_PLL2;
    break;
  }

  HAL_RCCEx_PeriphCLKConfig(&periphClkInitStruct);

  enableBusSPIClock(_desc.spix);
}

void SpiBus::configBufSize(uint32_t txBufSize, uint32_t rxBufSize) {
  if (txBufSize != 0)
    _pTxBufSize = txBufSize;

  if (rxBufSize != 0)
    _pRxBufSize = rxBufSize;
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
  gpio.config(GPIO::Mode::AlternateFunctionPushPull, Pull::NoPull, Speed::High);
  gpio.init();

#endif
}

void SpiBus::init() {
  allocBuf(_pTxBufSize, _pRxBufSize);
  std::memset(_pRxBuf, 0, _pTxBufSize * sizeof(uint8_t));
  std::memset(_pTxBuf, 0, _pRxBufSize * sizeof(uint8_t));

  enableClock();
  configPins();
#if defined(STM32H7)
  const auto spiIdx = static_cast<uint32_t>(_desc.spix);
  HANDLE.Instance = spiInstance[spiIdx];
  HANDLE.Init.Mode = SPI_MODE_MASTER;
  HANDLE.Init.Direction = SPI_DIRECTION_2LINES;
  HANDLE.Init.DataSize = SPI_DATASIZE_8BIT;
  HANDLE.Init.CLKPolarity = SPI_POLARITY_LOW;
  HANDLE.Init.CLKPhase = SPI_PHASE_1EDGE;
  HANDLE.Init.NSS = SPI_NSS_SOFT;
  HANDLE.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  HANDLE.Init.FirstBit = SPI_FIRSTBIT_MSB;
  HANDLE.Init.TIMode = SPI_TIMODE_DISABLE;
  HANDLE.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  HANDLE.Init.CRCPolynomial = 0x0;
  HANDLE.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  HANDLE.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  HANDLE.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  HANDLE.Init.TxCRCInitializationPattern =
      SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  HANDLE.Init.RxCRCInitializationPattern =
      SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  HANDLE.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  HANDLE.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  HANDLE.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  HAL_SPI_Init(&HANDLE);
#endif

  Bus::init();
}

// ── Synchronous write (was writeBytesPolling) ──

RetVal SpiBus::writeSync(const uint8_t *data, uint16_t num) {
#if defined(STM32H7)
  if (_initialized && data && num > 0 && num <= _pTxBufSize) {
    // Convert to non-const for HAL (HAL_SPI_Transmit takes non-const)
    std::memcpy(_pTxBuf, data, num);
    if (HAL_SPI_Transmit(&HANDLE, _pTxBuf, num, HAL_MAX_DELAY) != HAL_OK) {
      return RetVal::Error;
    }
    return RetVal::Ok;
  }
#endif
  return RetVal::Error;
}

// ── Synchronous read (was readBytesPolling) ──

RetVal SpiBus::readSync(uint8_t *data, uint16_t num) {
#if defined(STM32H7)
  if (_initialized && data && num > 0) {
    if (HAL_SPI_Receive(&HANDLE, data, num, HAL_MAX_DELAY) != HAL_OK) {
      return RetVal::Error;
    }
    return RetVal::Ok;
  }
#endif
  return RetVal::Error;
}

// ── Full-duplex transfer (SPI-specific) ──
// Handles NCS assertion/deassertion and simultaneous TX/RX.

RetVal SpiBus::transfer(const uint8_t *txData, uint8_t *rxData, uint16_t len) {
#if defined(STM32H7)
  if (!_initialized || len == 0) {
    return RetVal::InvalidParam;
  }

  // Assert NCS (chip select) — output low
  Gpio ncs(_desc.ncs);
  ncs.reset();

  if (txData != nullptr && rxData != nullptr) {
    // Full-duplex: send txData, receive into rxData
    if (len > _pTxBufSize) {
      ncs.set();
      return RetVal::InvalidParam;
    }
    std::memcpy(_pTxBuf, txData, len);
    if (HAL_SPI_TransmitReceive(&HANDLE, _pTxBuf, rxData, len,
                                HAL_MAX_DELAY) != HAL_OK) {
      ncs.set();
      return RetVal::Error;
    }
  } else if (txData != nullptr) {
    // TX only (MOSI only)
    if (len > _pTxBufSize) {
      ncs.set();
      return RetVal::InvalidParam;
    }
    std::memcpy(_pTxBuf, txData, len);
    if (HAL_SPI_Transmit(&HANDLE, _pTxBuf, len, HAL_MAX_DELAY) != HAL_OK) {
      ncs.set();
      return RetVal::Error;
    }
  } else if (rxData != nullptr) {
    // RX only (send dummy 0xFF to clock in data)
    if (len > _pRxBufSize) {
      ncs.set();
      return RetVal::InvalidParam;
    }
    if (HAL_SPI_Receive(&HANDLE, rxData, len, HAL_MAX_DELAY) != HAL_OK) {
      ncs.set();
      return RetVal::Error;
    }
  } else {
    // Both null: send dummy 0xFF bytes, discard RX
    if (len > _pTxBufSize) {
      ncs.set();
      return RetVal::InvalidParam;
    }
    std::memset(_pTxBuf, 0xFF, len);
    if (HAL_SPI_Transmit(&HANDLE, _pTxBuf, len, HAL_MAX_DELAY) != HAL_OK) {
      ncs.set();
      return RetVal::Error;
    }
  }

  // De-assert NCS (chip select) — output high
  ncs.set();

  return RetVal::Ok;
#else
  (void)txData;
  (void)rxData;
  (void)len;
  return RetVal::Error;
#endif
}
