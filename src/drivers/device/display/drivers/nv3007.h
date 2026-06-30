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

#include "build_info.h"
#include "drivers/device/display/driver.h"
#include "drivers/peripherals/bus/bus_spi.h"
#include "drivers/peripherals/gpio.h"

namespace ThetaGP::Drivers::Device {

class Nv3007 : public DisplayDriver {
public:
  static constexpr uint16_t WIDTH = 428;
  static constexpr uint16_t HEIGHT = 154;

  static Nv3007 &getInstance(Peripheral::BUS::SpiBus &spi,
                              const Peripheral::GPIO::PinDesc &dcPin,
                              const Peripheral::GPIO::PinDesc &resPin,
                              const Peripheral::GPIO::PinDesc &blkPin);

  void init() override;
  void setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;
  void writePixels(const uint8_t *data, uint32_t len) override;
  uint16_t width() const override { return WIDTH; }
  uint16_t height() const override { return HEIGHT; }
  void flush() override;
  uint8_t *getFramebuffer() override { return _framebuffer; }
  void setRotation(uint8_t madctl);

private:
  Nv3007(Peripheral::BUS::SpiBus &spi,
         const Peripheral::GPIO::PinDesc &dcPin,
         const Peripheral::GPIO::PinDesc &resPin,
         const Peripheral::GPIO::PinDesc &blkPin);
  void sendCommand(uint8_t cmd);
  void sendData(uint8_t data);
  void sendCommandList(const uint8_t *list);
  void hwReset();
  void swReset();

  COMMON_ZERO_INIT static uint8_t _framebuffer[WIDTH * HEIGHT * 2];

  Peripheral::BUS::SpiBus &_spi;
  Peripheral::GPIO::Gpio _dc;
  Peripheral::GPIO::Gpio _res;
  Peripheral::GPIO::Gpio _blk;
  uint8_t _madctl = 0;
};

} // namespace ThetaGP::Drivers::Device
