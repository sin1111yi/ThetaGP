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

#include "drivers/peripherals/bus.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/gpio.h"

#include <array>

namespace ThetaGP::Drivers::Periph::Bus {

using namespace GPIO;

constexpr uint16_t busSPITxBufSize = 64;
constexpr uint16_t busSPIRxBufSize = 192;

void enableBusSPIClock(SPIInstance spix) {
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

static constexpr uint32_t kSpiAlternate = 0x05;

BusSPI::BusSPI() {
  setType(Type::Spi);

  _initialized = false;
}

BusSPI::BusSPI(const SPIDesc &spiDesc) {
  setType(Type::Spi);

  _spiDesc = spiDesc;

  _initialized = false;
}

BusSPI::BusSPI(SPIInstance spix, PinDesc mosi, PinDesc miso, PinDesc sck) {
  setType(Type::Spi);

  _spiDesc.spix = spix;
  _spiDesc.mosi = mosi;
  _spiDesc.miso = miso;
  _spiDesc.sck = sck;

  _initialized = false;
}

void BusSPI::enableClock() const { enableBusSPIClock(_spiDesc.spix); }
void BusSPI::configPins() {
#if defined(STM32H7)
  GPIO_InitTypeDef gpioInit{
      .Pin = 0x0000,
      .Mode = static_cast<uint32_t>(GPIO::Mode::AlternateFunctionPushPull),
      .Pull = static_cast<uint32_t>(Pull::NoPull),
      .Speed = static_cast<uint32_t>(Speed::High),
      .Alternate = kSpiAlternate};

  Gpio::enableClock(_spiDesc.mosi);
  gpioInit.Pin = Gpio::getPinMask(_spiDesc.mosi);
  HAL_GPIO_Init(
      reinterpret_cast<GPIO_TypeDef *>(Gpio::getPortAddress(_spiDesc.mosi)),
      &gpioInit);

  Gpio::enableClock(_spiDesc.miso);
  gpioInit.Pin = Gpio::getPinMask(_spiDesc.miso);
  HAL_GPIO_Init(
      reinterpret_cast<GPIO_TypeDef *>(Gpio::getPortAddress(_spiDesc.mosi)),
      &gpioInit);

  Gpio::enableClock(_spiDesc.sck);
  gpioInit.Pin = Gpio::getPinMask(_spiDesc.sck);
  HAL_GPIO_Init(
      reinterpret_cast<GPIO_TypeDef *>(Gpio::getPortAddress(_spiDesc.mosi)),
      &gpioInit);
#endif
}

void BusSPI::setNcs(SPINcs ncsx, PinDesc pin) {

  Gpio &ncs = (ncsx == SPINcs::BusSpiNcs1 ? _spiDesc.ncs1 : _spiDesc.ncs2);

  ncs = GPIO::Gpio(pin);
  ncs.config(GPIO::Mode::OutputPushPull, Pull::NoPull, Speed::High);
  ncs.init();
}

void BusSPI::init() {

}

} // namespace ThetaGP::Drivers::Periph::Bus
