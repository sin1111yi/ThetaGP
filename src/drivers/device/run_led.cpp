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

#include "run_led.h"

namespace ThetaGP::Drivers::Device {

using namespace Peripheral::GPIO;

RunLed::RunLed()
    : Device("run_led"),
      _led(PinDesc LED0_PIN) {
}

void RunLed::init() {
  _led.config(Mode::OutputPushPull, Pull::NoPull, Speed::Low);
  _led.init();
  _led.write(PinState::Reset);
  _phaseIdx = 0;
  _phaseStartUs = 0;
  _initialized = true;
}

void RunLed::setEffect(Effect effect) {
  _effect = effect;
  _phaseIdx = 0;
  _phaseStartUs = 0;
}

void RunLed::setState(bool on) {
  _effect = Effect::Manual;
  _manualState = on;
}

void RunLed::update(uint32_t currentTimeUs) {
  if (!isInitialized())
    return;

  bool on;

  if (_effect == Effect::Manual) {
    on = _manualState;
  } else {
    auto idx = static_cast<uint8_t>(_effect) - 1;
    const Phase &p = EFFECT_TABLE[idx][_phaseIdx];
    if (p.durationUs == 0) {
      _phaseIdx = 0;
      _phaseStartUs = currentTimeUs;
      return;
    }

    on = p.state;

    if (currentTimeUs - _phaseStartUs >= p.durationUs) {
      _phaseStartUs = currentTimeUs;
      _phaseIdx++;
      if (EFFECT_TABLE[idx][_phaseIdx].durationUs == 0)
        _phaseIdx = 0;
    }
  }

  PinState target = on ? PinState::Set : PinState::Reset;
#if defined(LED0_ACTIVE_LOW) && LED0_ACTIVE_LOW
  target = (target == PinState::Set) ? PinState::Reset : PinState::Set;
#endif
  _led.write(target);
}

} // namespace ThetaGP::Drivers::Device
