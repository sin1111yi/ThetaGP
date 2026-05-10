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

enum class SpiBusIO { CLK, MOSI, MISO };

struct SpiDesc {
  Instance spix;

  GPIO::PinDesc busPinDesc[3];
  GPIO::PinDesc ncs;
};

class SpiBus : public Bus {
private:
  uint32_t _bufSize = 16;
  SpiDesc _desc;
  void *_halHandle = nullptr;

  void enableClock() const;
  void configPins();

  void enableTxDMA();
  void enableRxDMA();

  RetVal writeBytesPolling(uint8_t *bytes, uint16_t num) override;
  RetVal writeBytesDMA(uint8_t *bytes, uint16_t num) override;
  RetVal readBytesPolling(uint8_t *bytes, uint16_t num) override;
  RetVal readBytesDMA(uint8_t *bytes, uint16_t num) override;

public:
  SpiBus(Instance spix, GPIO::PinDesc clk, GPIO::PinDesc mosi,
         GPIO::PinDesc miso, GPIO::PinDesc ncs);
  explicit SpiBus(const SpiDesc &desc);
  ~SpiBus() = default;

  void configBufSize(uint32_t txBufSize, uint32_t rxBufSize);

  void init() override;
  void enableClock() override;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
