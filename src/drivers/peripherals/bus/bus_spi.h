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

#include "Legacy/stm32_hal_legacy.h"
#include "build_info.h"

#include "drivers/peripherals/bus.h"
#include "drivers/peripherals/gpio.h"
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace ThetaGP {
namespace Drivers {
namespace Periph {
namespace Bus {

enum class SPIInstance {
  BusSpi1,
  BusSpi2,
  BusSpi3,
  BusSpi4,
  BusSpi5,
  BusSpi6,
};

enum class SPINcs { BusSpiNcs1, BusSpiNcs2 };

struct SPIDesc {
  void *handle;
  SPIInstance spix;

  GPIO::PinDesc mosi;
  GPIO::PinDesc miso;
  GPIO::PinDesc sck;

  GPIO::Gpio ncs1;
  GPIO::Gpio ncs2;
};

class BusSPI : public Bus {
private:
  SPIDesc _spiDesc;
  uint8_t *_pTxBuf;
  uint8_t *_pRxBuf;

  bool _initialized;

  void enableClock() const;
  void configPins();

  void enableTxDMA();
  void enableRxDMA();

  retval_t writePolling(uint8_t *bytes, uint16_t num);
  retval_t writeDMA(uint8_t *bytes, uint16_t num);

  retval_t readPolling(uint8_t *bytes, uint16_t num);
  retval_t readDMA(uint8_t *bytes, uint16_t num);

  void allocBuf();

public:
  BusSPI();
  BusSPI(const SPIDesc &spiDesc);
  BusSPI(SPIInstance spix, GPIO::PinDesc mosi, GPIO::PinDesc miso,
         GPIO::PinDesc sck);

  void setNcs(SPINcs ncsx, GPIO::PinDesc pin);
  void config();

  void init() override;
  void enable() override;

  retval_t write(uint8_t byte) override;
  retval_t write(uint8_t *bytes, uint16_t num) override;

  retval_t read(uint8_t *byte) override;
  retval_t read(uint8_t *bytes, uint16_t num) override;
};

} // namespace Bus
} // namespace Periph
} // namespace Drivers
} // namespace ThetaGP
