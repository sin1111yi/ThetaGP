#include "drivers/peripherals/nvic_exti.h"
#include "drivers/peripherals/gpio.h"

#include <array>

namespace NvicExtiDefine {

std::array<NvicExti *, 16> extiInstances = {};

constexpr std::array<uint8_t, 16> extiGroups = {0, 1, 2, 3, 4, 5, 5, 5,
                                                5, 5, 6, 6, 6, 6, 6, 6};
std::array<uint8_t, EXTI_IRQ_GROUPS> extiGroupPriority = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

#if defined(STM32H7)
constexpr std::array<IRQn_Type, EXTI_IRQ_GROUPS> extiGroupIRQn = {
    EXTI0_IRQn, EXTI1_IRQn,   EXTI2_IRQn,    EXTI3_IRQn,
    EXTI4_IRQn, EXTI9_5_IRQn, EXTI15_10_IRQn};
#else
#warning "Unknown CPU"
#endif

NvicExti::NvicExti()
    : _triggerSrc(GpioDefine::Mode::Input),
      _priority(NvicPriority::PriorityLow) {}

NvicExti::NvicExti(GpioDefine::PinDesc pinDesc, GpioDefine::Mode triggerSrc,
                   NvicPriority priority)
    : Gpio(pinDesc), _triggerSrc(triggerSrc), _priority(priority) {}

NvicExti::NvicExti(GpioDefine::Port port, GpioDefine::Pin pin,
                   GpioDefine::Mode triggerSrc, NvicPriority priority)
    : Gpio(port, pin), _triggerSrc(triggerSrc), _priority(priority) {}

void NvicExti::init() {
  Gpio::config(_triggerSrc, GpioDefine::Pull::PullUp, GpioDefine::Speed::High);
  Gpio::init();

  const uint32_t pinIdx = static_cast<uint8_t>(_config.pin);
  int group = extiGroups[pinIdx];

  if (pinIdx >= extiInstances.size()) {
    return;
  }

  extiInstances[pinIdx] = this;
  disable();

#if defined(STM32H7)
  GPIO_InitTypeDef gpioInit{.Pin = getPinMask(),
                            .Mode =
                                static_cast<uint32_t>(_triggerSrc) |
                                static_cast<uint32_t>(GpioDefine::Mode::Input) |
                                static_cast<uint32_t>(_config.mode),
                            .Pull = static_cast<uint32_t>(_config.pull),
                            .Speed = static_cast<uint32_t>(_config.speed),
                            .Alternate = _config.alternate};

  HAL_GPIO_Init(getPortAddress(), &gpioInit);

  if (extiGroupPriority[group] > static_cast<uint8_t>(_priority)) {
    extiGroupPriority[group] = static_cast<uint8_t>(_priority);
    HAL_NVIC_SetPriority(extiGroupIRQn[group],
                         NVIC_PRIORITY_BASE(static_cast<uint32_t>(_priority)),
                         NVIC_PRIORITY_SUB(static_cast<uint32_t>(_priority)));
    HAL_NVIC_EnableIRQ(extiGroupIRQn[group]);
  }
#endif
}

void NvicExti::setCallback(ExtiCallback cb) { _callback = std::move(cb); }

void NvicExti::disable(void) {
#if defined(STM32H7)
  uint32_t extiLine = 1 << static_cast<uint8_t>(_config.pin);

  if (!extiLine)
    return;

  EXTI_REG_IMR &= ~extiLine;
  EXTI_REG_PR = extiLine;
#else
#error "Unknown CPU"
#endif
}

void NvicExti::enable(void) {
#if defined(STM32H7)
  uint32_t extiLine = 1 << static_cast<uint8_t>(_config.pin);

  if (!extiLine) {
    return;
  }

  EXTI_REG_IMR |= extiLine;
#else
#error "Unknown CPU"
#endif
}

void NvicExti::release(void) {
  disable();

  const uint32_t chIdx = static_cast<uint8_t>(_config.pin);

  if (chIdx >= extiInstances.size()) {
    return;
  }

  extiInstances[chIdx] = nullptr;
}

} // namespace NvicExtiDefine

extern "C" {

// first 16 bits only, see also definition of extiChannels.
#define EXTI_EVENT_MASK 0xFFFF

static void EXTI_IRQnHandler(uint32_t mask) {
  uint32_t exti_active = (EXTI_REG_IMR & EXTI_REG_PR) & mask;

  EXTI_REG_PR = exti_active; // clear pending mask (by writing 1)

  while (exti_active) {
    uint32_t idx = 31 - __builtin_clz(exti_active);
    uint32_t bit = 1U << idx;
    auto *extiInstance = NvicExtiDefine::extiInstances[idx];
    if (extiInstance && extiInstance->_callback) {
      extiInstance->_callback(extiInstance);
    }
    exti_active &= ~bit;
  }
}

#define extiIrqHandler(name, mask)                                             \
  void name(void) { EXTI_IRQnHandler(mask & EXTI_EVENT_MASK); }                \
  struct dummy

extiIrqHandler(EXTI0_IRQHandler, 0x0001);
extiIrqHandler(EXTI1_IRQHandler, 0x0002);
extiIrqHandler(EXTI2_IRQHandler, 0x0004);
extiIrqHandler(EXTI3_IRQHandler, 0x0008);
extiIrqHandler(EXTI4_IRQHandler, 0x0010);
extiIrqHandler(EXTI9_5_IRQHandler, 0x03e0);
extiIrqHandler(EXTI15_10_IRQHandler, 0xfc00);
}