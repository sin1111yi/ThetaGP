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

namespace ThetaGP::Drivers::Peripheral::BUS {

enum class SpiInstance {
  Spi1,
  Spi2,
  Spi3,
  Spi4,
  Spi5,
  Spi6,
  SpiNone = 0xFF,
};

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
public:
  SpiBus(SpiInstance spix, GPIO::PinDesc clk, GPIO::PinDesc mosi,
         GPIO::PinDesc miso, GPIO::PinDesc ncs);
  explicit SpiBus(const SpiDesc &desc);
  ~SpiBus() override;

  SpiBus(const SpiBus &) = delete;
  SpiBus &operator=(const SpiBus &) = delete;

  void init() override;
  void enableClock() override;

  void enable() { _desc.ncs.reset(); }
  void disable() { _desc.ncs.set(); }

  void *halHandle() const { return _halHandle; }
  uint8_t *rxBuf() const { return _rxBuf; }

  bool isBusy() const;

  // Whether the transfer that just ended was stopped by a bus fault: a caller
  // handed an asynchronous completion reads this to tell it from a clean one.
  [[nodiscard]] bool transferFailed() const { return _spiTransferError; }

  // ── DMA state ──
  DMA::DmaChannel *_dmaTx = nullptr;
  DMA::DmaChannel *_dmaRx = nullptr;

  // ── Async transfer state ──
  const uint8_t *_asyncSrc = nullptr;
  uint8_t *_asyncDst = nullptr;
  const uint8_t *_asyncSrcOrig = nullptr;
  uint8_t *_asyncDstOrig = nullptr;
  uint16_t _asyncRemaining = 0;
  uint16_t _asyncChunkLen = 0;
  uint16_t _asyncTotalLen = 0;
  TransferCallback _asyncCb = nullptr;
  void *_asyncCtx = nullptr;
  volatile bool _spiTransferDone = true;
  volatile bool _spiTransferError = false;
  // The status register as the fault path found it, kept for the log the task
  // writes once the transfer has returned.
  volatile uint32_t _spiTransferFault = 0;

  void spiInternalInitStream(const uint8_t *txData, uint8_t *rxData,
                              uint16_t len);
  void spiInternalStartDMA(TransferCallback cb, void *ctx,
                            uint16_t totalLen);
  void spiInternalStopDMA();

private:
  void configPins();

  Result transmitReceiveImpl(TransferCallback cb, void *ctx,
                       const uint8_t *txData, uint8_t *rxData,
                       uint32_t len) override;

  Result spiInternalReadWriteBufPolled(const uint8_t *txData,
                                        uint8_t *rxData, uint16_t len);

  SpiDesc _desc;
  void *_halHandle = nullptr;

  uint32_t _streamTxSrc = 0;
  uint32_t _streamRxDst = 0;
  uint16_t _streamTxLen = 0;
  uint16_t _streamRxLen = 0;
  bool _streamTxInc = false;
  bool _streamRxInc = false;
};

} // namespace ThetaGP::Drivers::Peripheral::BUS
