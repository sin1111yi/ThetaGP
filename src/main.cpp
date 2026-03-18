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
#include "drivers/peripherals/gpio.h"
#include "drivers/peripherals/nvic_exti.h"
#include "drivers/peripherals/systick.h"

using namespace ThetaGP::Drivers::Periph::GPIO;
using namespace ThetaGP::Drivers::Periph::NVIC_EXTI;

class Led : protected Gpio {
public:
  using Gpio::init;
  using Gpio::config;
  using Gpio::toggle;
  Led(const PinDesc &pinDesc) : Gpio(pinDesc) {}
};

Led led(LED0_PIN);
NvicExti PC13(Port::PortC, Pin::Pin13, Mode::InterruptFalling,
              NvicPriority::PriorityMedium);

int main(void) {
  led.config(Mode::OutputPushPull, Pull::NoPull, Speed::Low);
  led.init();

  cycleCounterInit();
  HAL_NVIC_SetPriorityGrouping(7 - NVIC_PROIRITY_SUB_WIDTH);

  PC13.setCallback([](NvicExti *self) {
    UNUSED(self);
    led.toggle();
  });
  PC13.init();
  PC13.enable();

  while (1) {
    led.toggle();
    delay_ms(1000);
  }
}
