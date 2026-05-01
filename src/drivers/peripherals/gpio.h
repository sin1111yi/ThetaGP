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
#include <cstdint>

namespace ThetaGP::Drivers::Peripheral::GPIO {

enum class Port : uint16_t {
  PortA,
  PortB,
  PortC,
  PortD,
  PortE,
  PortF,
  PortG,
  PortH,
#if defined(GPIOI)
  PortI,
#endif
  PortJ,
  PortK,

  PortNone = 0xFFFF,
};

enum class Pin : uint16_t {
  Pin0,
  Pin1,
  Pin2,
  Pin3,
  Pin4,
  Pin5,
  Pin6,
  Pin7,
  Pin8,
  Pin9,
  Pin10,
  Pin11,
  Pin12,
  Pin13,
  Pin14,
  Pin15,

  PinNone = 0xFFFF,
};

enum class Speed : uint32_t {
  Low = GPIO_SPEED_FREQ_LOW,
  Medium = GPIO_SPEED_FREQ_MEDIUM,
  High = GPIO_SPEED_FREQ_HIGH,
  VeryHigh = GPIO_SPEED_FREQ_VERY_HIGH
};

enum class Mode : uint32_t {
  Input = GPIO_MODE_INPUT,
  OutputPushPull = GPIO_MODE_OUTPUT_PP,
  OutputOpenDrain = GPIO_MODE_OUTPUT_OD,
  AlternateFunctionPushPull = GPIO_MODE_AF_PP,
  AlternateFunctionOpenDrain = GPIO_MODE_AF_OD,
  Analog = GPIO_MODE_ANALOG,
  InterruptRising = GPIO_MODE_IT_RISING,
  InterruptFalling = GPIO_MODE_IT_FALLING,
  InterruptRisingFalling = GPIO_MODE_IT_RISING_FALLING,
};

enum class Pull : uint32_t {
  NoPull = GPIO_NOPULL,
  PullUp = GPIO_PULLUP,
  PullDown = GPIO_PULLDOWN
};

enum class PinState : bool { Reset = GPIO_PIN_RESET, Set = GPIO_PIN_SET };

/**
 * @brief Pin descriptor for board configuration macros
 */
struct PinDesc {
  Port port;
  Pin pin;
};

class Gpio {
private:
  struct Config {
    Port port = Port::PortNone;
    Pin pin = Pin::PinNone;
    Mode mode = Mode::Input;
    Pull pull = Pull::NoPull;
    Speed speed = Speed::Low;
    uint32_t alternate = 0;
  };

  Config _config;
  bool _initialized = false;

public:
  Gpio();
  Gpio(const PinDesc &pinDesc);
  Gpio(Port port, Pin pin);

  // Configuration
  void config(Mode mode, Pull pull, Speed speed);
  void config(Mode mode, Pull pull, Speed speed, uint32_t alternate);

  void init();

  void enableClock() const;
  static void enableClock(const PinDesc &pinDesc);

  void *getPortAddress() const;
  static void *getPortAddress(const PinDesc &pinDesc);

  uint16_t getPinMask() const;
  static uint16_t getPinMask(const PinDesc &pinDesc);

  void write(PinState state);
  PinState read() const;
  void toggle();
  void set();
  void reset();

  // Convenience methods
  void high() { set(); }
  void low() { reset(); }

  // Status
  bool isInitialized() const { return _initialized; }

  // Accessors for derived/composed classes
  const Config &getConfig() const { return _config; }
};

} // namespace ThetaGP::Drivers::Peripheral::GPIO
