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

#include "BoardConfig.h"

#include "tusb.h"

#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic_exti.h"
#include "drivers/peripherals/systick.h"
#include "drivers/peripherals/usb/usb.h"

using namespace ThetaGP::Drivers::Periph::GPIO;
using namespace ThetaGP::Drivers::Periph::NVIC_EXTI;
using namespace ThetaGP::Drivers::Periph::USB;

class Led : protected Gpio {
public:
  using Gpio::config;
  using Gpio::init;
  using Gpio::toggle;
  Led(const PinDesc &pinDesc) : Gpio(pinDesc) {}
};

Led led(LED0_PIN);
USB usb(USBSpeed::UsbHighSpeedExternalPHY, USBPeripheral::UsbULPI);

int main(void) {
  led.config(Mode::OutputPushPull, Pull::NoPull, Speed::Low);
  led.init();

  cycleCounterInit();
  NvicExti::preinit();

  while (1) {
  }
}
