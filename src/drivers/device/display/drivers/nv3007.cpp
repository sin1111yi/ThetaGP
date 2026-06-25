/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "drivers/device/display/drivers/nv3007.h"
#include "drivers/device/devmem.h"
#include "drivers/peripherals/systick.h"
#include "utils/mempool/mempoolmanager.h"

namespace ThetaGP::Drivers::Device::DisplayDrv {

namespace GPIO = Peripheral::GPIO;
using namespace Peripheral::BUS;

// ── Init command list from LVGL lv_nv3007.c ─────────────────────────────────

static constexpr uint8_t CMD_DELAY = 0xFF;
static constexpr uint8_t CMD_EOF   = 0xFE;

static const uint8_t initCmds[] = {
    0x9a, 1, 0x08,
    0x9b, 1, 0x08,
    0x9c, 1, 0xb0,
    0x9d, 1, 0x16,
    0x9e, 1, 0xc4,
    0x8f, 2, 0x55, 0x04,
    0x84, 1, 0x90,
    0x83, 1, 0x7b,
    0x85, 1, 0x33,
    0x60, 1, 0x00,
    0x70, 1, 0x00,
    0x61, 1, 0x02,
    0x71, 1, 0x02,
    0x62, 1, 0x04,
    0x72, 1, 0x04,
    0x6c, 1, 0x29,
    0x7c, 1, 0x29,
    0x6d, 1, 0x31,
    0x7d, 1, 0x31,
    0x6e, 1, 0x0f,
    0x7e, 1, 0x0f,
    0x66, 1, 0x21,
    0x76, 1, 0x21,
    0x68, 1, 0x3A,
    0x78, 1, 0x3A,
    0x63, 1, 0x07,
    0x73, 1, 0x07,
    0x64, 1, 0x05,
    0x74, 1, 0x05,
    0x65, 1, 0x02,
    0x75, 1, 0x02,
    0x67, 1, 0x23,
    0x77, 1, 0x23,
    0x69, 1, 0x08,
    0x79, 1, 0x08,
    0x6a, 1, 0x13,
    0x7a, 1, 0x13,
    0x6b, 1, 0x13,
    0x7b, 1, 0x13,
    0x6f, 1, 0x00,
    0x7f, 1, 0x00,
    0x50, 1, 0x00,
    0x52, 1, 0xd6,
    0x53, 1, 0x08,
    0x54, 1, 0x08,
    0x55, 1, 0x1e,
    0x56, 1, 0x1c,
    0xa0, 3, 0x2b, 0x24, 0x00,
    0xa1, 1, 0x87,
    0xa2, 1, 0x86,
    0xa5, 1, 0x00,
    0xa6, 1, 0x00,
    0xa7, 1, 0x00,
    0xa8, 1, 0x36,
    0xa9, 1, 0x7e,
    0xaa, 1, 0x7e,
    0xB9, 1, 0x85,
    0xBA, 1, 0x84,
    0xBB, 1, 0x83,
    0xBC, 1, 0x82,
    0xBD, 1, 0x81,
    0xBE, 1, 0x80,
    0xBF, 1, 0x01,
    0xC0, 1, 0x02,
    0xc1, 1, 0x00,
    0xc2, 1, 0x00,
    0xc3, 1, 0x00,
    0xc4, 1, 0x33,
    0xc5, 1, 0x7e,
    0xc6, 1, 0x7e,
    0xC8, 2, 0x33, 0x33,
    0xC9, 1, 0x68,
    0xCA, 1, 0x69,
    0xCB, 1, 0x6a,
    0xCC, 1, 0x6b,
    0xCD, 2, 0x33, 0x33,
    0xCE, 1, 0x6c,
    0xCF, 1, 0x6d,
    0xD0, 1, 0x6e,
    0xD1, 1, 0x6f,
    0xAB, 2, 0x03, 0x67,
    0xAC, 2, 0x03, 0x6b,
    0xAD, 2, 0x03, 0x68,
    0xAE, 2, 0x03, 0x6c,
    0xb3, 1, 0x00,
    0xb4, 1, 0x00,
    0xb5, 1, 0x00,
    0xB6, 1, 0x32,
    0xB7, 1, 0x7e,
    0xB8, 1, 0x7e,
    0xe0, 1, 0x00,
    0xe1, 2, 0x03, 0x0f,
    0xe2, 1, 0x04,
    0xe3, 1, 0x01,
    0xe4, 1, 0x0e,
    0xe5, 1, 0x01,
    0xe6, 1, 0x19,
    0xe7, 1, 0x10,
    0xe8, 1, 0x10,
    0xea, 1, 0x12,
    0xeb, 1, 0xd0,
    0xec, 1, 0x04,
    0xed, 1, 0x07,
    0xee, 1, 0x07,
    0xef, 1, 0x09,
    0xf0, 1, 0xd0,
    0xf1, 1, 0x0e,
    0xF9, 1, 0x17,
    0xf2, 4, 0x2c, 0x1b, 0x0b, 0x20,
    0xe9, 1, 0x29,
    0xec, 1, 0x04,
    0x35, 1, 0x00,
    0x44, 2, 0x00, 0x10,
    0x46, 1, 0x10,
    CMD_EOF,
};

static const uint8_t initCmds2[] = {
    0x3a, 1, 0x05,       // COLMOD: 16-bit/pixel
    0x11, 0,             // SLPOUT
    CMD_DELAY, 22,       // delay 220ms
    0x29, 0,             // DISPON
    CMD_EOF,
};

// ── Constructor ─────────────────────────────────────────────────────────────

Nv3007::Nv3007(SpiBus &spi, const GPIO::PinDesc &dcPin,
               const GPIO::PinDesc &resPin, const GPIO::PinDesc &blkPin)
    : _spi(spi), _dc(dcPin), _res(resPin), _blk(blkPin) {
}

// ── Public API ──────────────────────────────────────────────────────────────

void Nv3007::init() {
  // Configure GPIOs
  _dc.config(GPIO::Mode::OutputPushPull, GPIO::Pull::NoPull, GPIO::Speed::High);
  _dc.init();
  _dc.write(GPIO::PinState::Reset);

  _blk.config(GPIO::Mode::OutputPushPull, GPIO::Pull::NoPull, GPIO::Speed::Low);
  _blk.init();

  // Allocate DMA-safe SPI buffer
  constexpr uint32_t BUF_SIZE = 256;
  auto *txBuf = static_cast<uint8_t *>(Mempool::MempoolManager::alloc(
      DevMem::getInstance().poolId(), BUF_SIZE));
  _spi.setBuffers(txBuf, nullptr, BUF_SIZE);
  _spi.init();

  // Hardware reset
  reset();

  // Enable backlight
  _blk.write(GPIO::PinState::Set);

  // Send init sequence
  sendCommand(0xFF); sendData(0xA5);
  sendCommandList(initCmds);
  sendCommand(0xFF); sendData(0x00);
  sendCommandList(initCmds2);
}

void Nv3007::setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint16_t x2 = x + w - 1;
  uint16_t y2 = y + h - 1;

  sendCommand(0x2A);  // CASET
  sendData(x >> 8); sendData(x & 0xFF);
  sendData(x2 >> 8); sendData(x2 & 0xFF);

  sendCommand(0x2B);  // RASET
  sendData(y >> 8); sendData(y & 0xFF);
  sendData(y2 >> 8); sendData(y2 & 0xFF);

  sendCommand(0x2C);  // RAMWR
}

void Nv3007::writePixels(const uint8_t *data, uint32_t len) {
  _dc.write(GPIO::PinState::Set);
  (void)_spi.write(data, len);
}

// ── Private helpers ─────────────────────────────────────────────────────────

void Nv3007::sendCommand(uint8_t cmd) {
  _dc.write(GPIO::PinState::Reset);
  (void)_spi.write(&cmd, 1);
}

void Nv3007::sendData(uint8_t data) {
  _dc.write(GPIO::PinState::Set);
  (void)_spi.write(&data, 1);
}

void Nv3007::sendCommandList(const uint8_t *list) {
  while (true) {
    uint8_t cmd = *list++;
    if (cmd == CMD_EOF) break;
    if (cmd == CMD_DELAY) {
      uint8_t ms = *list++ * 10;
      delay_ms(ms);
      continue;
    }
    uint8_t nParams = *list++;
    sendCommand(cmd);
    for (uint8_t i = 0; i < nParams; i++)
      sendData(*list++);
  }
}

void Nv3007::reset() {
  _res.config(GPIO::Mode::OutputPushPull, GPIO::Pull::NoPull, GPIO::Speed::Low);
  _res.init();
  _res.write(GPIO::PinState::Reset);
  delay_ms(10);
  _res.write(GPIO::PinState::Set);
  delay_ms(120);
}

} // namespace ThetaGP::Drivers::Device::Display
