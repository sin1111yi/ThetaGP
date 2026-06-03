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

namespace ThetaGP::Drivers::Peripheral::DMA {
class DmaChannel;
}

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
  SpiNone = 0xFF,
};

// Abstract bus index → PeripheralsManager array index
#define BUS_SPI_1 0
#define BUS_SPI_2 1
#define BUS_SPI_3 2
#define BUS_SPI_4 3
#define BUS_SPI_5 4
#define BUS_SPI_6 5

enum class SpiBusIO { CLK, MOSI, MISO };

struct SpiDesc {
  SpiInstance spix;

  GPIO::PinDesc busPinDesc[3];
  GPIO::Gpio ncs;
};

class SpiBus : public Bus {
private:
  SpiDesc _desc;
  void *_halHandle = nullptr;

  void configPins();

  // ── Subclass hooks (SPI is always full-duplex, uses transfer/transferAsync) ──
  Result writeSync(const uint8_t *data, uint16_t num) override;
  Result readSync(uint8_t *data, uint16_t num) override;

public:
  SpiBus(SpiInstance spix, GPIO::PinDesc clk, GPIO::PinDesc mosi,
         GPIO::PinDesc miso, GPIO::PinDesc ncs);
  explicit SpiBus(const SpiDesc &desc);
  ~SpiBus() override;

  SpiBus(const SpiBus &) = delete;
  SpiBus &operator=(const SpiBus &) = delete;

  void init() override;
  void enableClock() override;

  /**
   * @brief Full-duplex SPI transfer (MOSI + MISO simultaneously)
   *
   * Transfers @p len bytes in total, split into chunks of @p chunkLen.
   * Each chunk copies data through the internal _txBuf/_rxBuf buffers
   * (must be ≤ _bufSize). NCS is held asserted for the entire multi-
   * chunk transfer.
   *
   * Pass txData=nullptr to send dummy bytes (0xFF).
   * Pass rxData=nullptr to discard received bytes.
   * chunkLen must be > 0 and ≤ _bufSize.
   */
  Result transfer(const uint8_t *txData, uint8_t *rxData, uint16_t len,
                  uint16_t chunkLen);

  /**
   * @brief Two-phase asynchronous SPI transfer
   *
   * Phase 1: CMD+ADDR sent via polling (blocking HAL_SPI_Transmit)
   * Phase 2: data phase via DMA (TX dummy bytes, RX into caller buffer)
   *
   * NCS is asserted before Phase 1 and de-asserted after DMA completion.
   *
   * @param cmdBuf        Command/address buffer (sent via polling)
   * @param cmdLen        Length of cmdBuf in bytes
   * @param rxBuf         Caller's receive buffer (may be DTCM — memcpy'd)
   * @param dataLen       Number of data bytes to transfer via DMA
   * @param postCmdDelayUs  Busy-wait delay after CMD phase (µs)
   * @param callback      Called from ISR on completion or error
   * @param context       Opaque context for callback
   */
  Result transferAsync(const uint8_t *cmdBuf, uint16_t cmdLen, uint8_t *rxBuf,
                       uint16_t dataLen, uint8_t postCmdDelayUs,
                       void (*callback)(void *context), void *context);

  // ── Public accessors for static ISR callbacks ──
  void *halHandle() const { return _halHandle; }
  uint8_t *rxBuf() const { return _rxBuf; }

  bool isBusy() const;
  bool isTxBusy() const;
  bool isRxBusy() const;

  // ── DMA state (accessed by static ISR callbacks in .cpp) ──
  DMA::DmaChannel *_dmaTx = nullptr;
  DMA::DmaChannel *_dmaRx = nullptr;
  uint8_t *_readDmaBuf = nullptr;
  uint16_t _readDmaLen = 0;
  void (*_transferCb)(void *) = nullptr;
  void *_transferCtx = nullptr;
};

} // namespace BUS
} // namespace Peripheral
} // namespace Drivers
} // namespace ThetaGP
