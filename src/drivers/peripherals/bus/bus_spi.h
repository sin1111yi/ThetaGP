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

enum class SpiInstance {
  Spi1,
  Spi2,
  Spi3,
  Spi4,
  Spi5,
  Spi6,
};

enum class SpiBusIO { CLK, MOSI, MISO };

struct SpiDesc {
  SpiInstance spix;

  GPIO::PinDesc busPinDesc[3];
  GPIO::PinDesc ncs;
};

class SpiBus : public Bus {
private:
  uint32_t _bufSize = 16;
  SpiDesc _desc;
  void *_halHandle = nullptr;

  void configPins();

  void enableTxDMA();
  void enableRxDMA();

  // ── Subclass hooks (refactored: only Sync implemented) ──
  RetVal writeSync(const uint8_t *data, uint16_t num) override;
  RetVal readSync(uint8_t *data, uint16_t num) override;

  // DMA stubs removed — default Bus::writeAsync/readAsync return Unsupported.
  // When DMA support is needed, override writeAsync/readAsync here.

public:
  SpiBus(SpiInstance spix, GPIO::PinDesc clk, GPIO::PinDesc mosi,
         GPIO::PinDesc miso, GPIO::PinDesc ncs);
  explicit SpiBus(const SpiDesc &desc);
  ~SpiBus() override;

  SpiBus(const SpiBus &) = delete;
  SpiBus &operator=(const SpiBus &) = delete;

  void configBufSize(uint32_t txBufSize, uint32_t rxBufSize);

  /**
   * @brief Full-duplex SPI transfer (MOSI + MISO simultaneously)
   *
   * This is SPI-specific and not part of the base Bus interface.
   * Pass txData=nullptr to send dummy bytes (0xFF).
   * Pass rxData=nullptr to discard received bytes.
   */
  RetVal transfer(const uint8_t *txData, uint8_t *rxData, uint16_t len);

  void init() override;
  void enableClock() override;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
