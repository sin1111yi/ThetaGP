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

#pragma once

#include "drivers/device/display/driver.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/gpio.h"

namespace ThetaGP::Drivers::Device::DisplayDrv {

/// NV3007 (ST7789-like) SPI display driver.
/// Init sequence ported from LVGL lv_nv3007.c.
class Nv3007 : public DisplayDriver {
public:
  Nv3007(Peripheral::BUS::SpiBus &spi,
         const Peripheral::GPIO::PinDesc &dcPin,
         const Peripheral::GPIO::PinDesc &resPin,
         const Peripheral::GPIO::PinDesc &blkPin);

  void init() override;
  void setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;
  void writePixels(const uint8_t *data, uint32_t len) override;
  uint16_t width() const override { return 240; }
  uint16_t height() const override { return 320; }

private:
  void sendCommand(uint8_t cmd);
  void sendData(uint8_t data);
  void sendCommandList(const uint8_t *list);
  void reset();

  Peripheral::BUS::SpiBus &_spi;
  Peripheral::GPIO::Gpio _dc;
  Peripheral::GPIO::Gpio _res;
  Peripheral::GPIO::Gpio _blk;
};

} // namespace ThetaGP::Drivers::Device::DisplayDrv
