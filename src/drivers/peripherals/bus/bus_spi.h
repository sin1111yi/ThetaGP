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

#pragma once

#include "build_info.h"

#include "drivers/peripherals/bus/bus.h"
#include "drivers/peripherals/gpio.h"
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace ThetaGP {
namespace Drivers {
namespace Peripheral {
namespace BUS {

enum class Instance {
  Spi1,
  Spi2,
  Spi3,
  Spi4,
  Spi5,
  Spi6,
};

enum class SpiNcs { BusSpiNcs1, BusSpiNcs2 };

struct SpiDesc {
  Instance spix;

  GPIO::PinDesc mosi;
  GPIO::PinDesc miso;
  GPIO::PinDesc sck;

  GPIO::Gpio ncs1;
  GPIO::Gpio ncs2;
};

class SpiBus : public Bus {
private:
  SpiDesc _spiDesc;
  bool _initialized = false;

  void enableClock() const;
  void configPins();

  void enableTxDMA();
  void enableRxDMA();

  RetVal writePolling(uint8_t *bytes, uint16_t num);
  RetVal writeDMA(uint8_t *bytes, uint16_t num);

  RetVal readPolling(uint8_t *bytes, uint16_t num);
  RetVal readDMA(uint8_t *bytes, uint16_t num);

public:
  SpiBus();
  ~SpiBus();
  SpiBus(const SpiDesc &spiDesc);
  SpiBus(Instance spix, GPIO::PinDesc mosi, GPIO::PinDesc miso,
         GPIO::PinDesc sck);

  void setNcs(SpiNcs ncsx, GPIO::PinDesc pin);
  void config();

  void init() override;
  void enableClock() override;

  RetVal write(uint8_t byte) override;
  RetVal write(uint8_t *bytes, uint16_t num) override;

  RetVal read(uint8_t *byte) override;
  RetVal read(uint8_t *bytes, uint16_t num) override;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
