#include "drivers/device/keypad.h"
#include "BoardConfig.h"
#include "drivers/peripherals/gpio.h"

namespace ThetaGP::Drivers::Device {

using namespace Peripheral::GPIO;
using namespace Peripheral::TIMER;

Keypad::Keypad() : Device(DeviceType::Keypad, 0), _readInput(nullptr) {
  for (auto &sampler : _samplers) {
    sampler.history = 0;
    sampler.stableState = KeyState::Released;
  }
}

void Keypad::init() {
  if (_initialized)
    return;

  initPins();

  _readInput = &Keypad::readInputScanMatrix;

  auto setupTimer = [this](HardwareTimer &timer,
                           Peripheral::TIMER::Instance instance) {
    timer.config(instance, KeypadConfig::DEFAULT_SCAN_FREQ);
    timer.setCallback(
        [](void *ctx) {
          auto *keypad = static_cast<Keypad *>(ctx);
          if (keypad)
            keypad->scanCallback();
        },
        this);
    timer.init();
    return timer.isInitialized();
  };

  if (!setupTimer(_scanTimer, Timer6)) {
    if (!setupTimer(_scanTimer, Timer7)) {
      return;
    }
  }

  _initialized = true;
}

void Keypad::timerCallback(void *context) {
  auto *keypad = static_cast<Keypad *>(context);
  if (keypad) {
    keypad->scanCallback();
  }
}

void Keypad::scanCallback() {
  uint32_t mask = 0;
  (this->*_readInput)(&mask);

  // Update sample history for all 32 keys
  for (size_t i = 0; i < MAX_KEYS; i++) {
    KeySampler &s = _samplers[i];
    const bool pressed = (mask & (1U << i)) != 0;
    s.history = ((s.history << 1) | pressed) & 0xFFFF;
  }

  _scanCount = (_scanCount + 1) % KeypadConfig::DEBOUNCE_SAMPLES;
  if (_scanCount != 0) {
    return;
  }

  // Majority vote and update state
  uint32_t debouncedMask = 0;

  for (size_t i = 0; i < MAX_KEYS; i++) {
    KeySampler &s = _samplers[i];
    const uint8_t count = __builtin_popcount(s.history);
    s.stableState = count >= KeypadConfig::DEBOUNCE_THRESHOLD
                        ? KeyState::Pressed
                        : KeyState::Released;

    if (s.stableState == KeyState::Pressed) {
      debouncedMask |= (1U << i);
    }
  }

  // Update output buffer
  const uint8_t nextWrite = 1 - _writeBuffer;
  _outputBuffer[nextWrite].pressedMask = debouncedMask;
  _outputBuffer[nextWrite].sequenceNum++;

  // Atomic buffer swap
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  _readBuffer = nextWrite;
  _writeBuffer = 1 - _readBuffer;
  __set_PRIMASK(primask);
}

void Keypad::readInputScanMatrix(uint32_t *mask) {
  const bool activeLow = (_active == KeypadConfig::Active::Low);
  const PinState driveState = activeLow ? PinState::Set : PinState::Reset;
  const PinState senseState = activeLow ? PinState::Reset : PinState::Set;
  const PinState idleState = activeLow ? PinState::Reset : PinState::Set;

  for (size_t d = 0; d < DRIVE_PIN_NUM; d++) {
    Gpio driveGpio(_drivePins[d]);
    driveGpio.write(driveState);

    for (uint32_t i = 0; i < KeypadConfig::GPIO_STABILIZE_DELAY_CYCLES; i++) {
      __NOP();
    }

    for (size_t s = 0; s < SENSE_PIN_NUM; s++) {
      Gpio senseGpio(_sensePins[s]);
      if (senseGpio.read() == senseState) {
        const uint8_t keyId = getKeyId(d, s);
        if (isValidKey(keyId)) {
          *mask |= (1U << keyId);
        }
      }
    }

    driveGpio.write(idleState);
  }
}

void Keypad::initPins() {
#if defined(KEYPAD_DRIVE_IO_LIST) && defined(KEYPAD_SENSE_IO_LIST)
  const PinDesc drivePinsTmp[] = {KEYPAD_DRIVE_IO_LIST};
  const PinDesc sensePinsTmp[] = {KEYPAD_SENSE_IO_LIST};

  const PinState idleState =
      (_active == KeypadConfig::Active::Low) ? PinState::Reset : PinState::Set;

  for (size_t i = 0; i < DRIVE_PIN_NUM; i++) {
    _drivePins[i] = drivePinsTmp[i];
    Gpio gpio(_drivePins[i]);
    gpio.config(Mode::OutputPushPull, Pull::NoPull, Speed::VeryHigh);
    gpio.init();
    gpio.write(idleState);
  }

  for (size_t i = 0; i < SENSE_PIN_NUM; i++) {
    _sensePins[i] = sensePinsTmp[i];
    Gpio gpio(_sensePins[i]);
    gpio.config(Mode::Input, Pull::PullUp, Speed::High);
    gpio.init();
  }
#endif
}

const uint32_t *Keypad::getPressedMask() const {
  return &_outputBuffer[_readBuffer].pressedMask;
}

uint32_t Keypad::getSequenceNum() const {
  return _outputBuffer[_readBuffer].sequenceNum;
}

uint32_t Keypad::getPressedMaskValue() const {
  return _outputBuffer[_readBuffer].pressedMask;
}

bool Keypad::isKeyPressed(uint8_t keyId) const {
  if (keyId >= MAX_KEYS)
    return false;
  return (_outputBuffer[_readBuffer].pressedMask & (1U << keyId)) != 0;
}

} // namespace ThetaGP::Drivers::Device
