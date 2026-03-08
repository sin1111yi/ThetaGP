#pragma once

#include "utils/utils.h"

#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/gpio.h"

#include <functional>

#define NVIC_PROIRITY_BASE_WIDTH (2)
#define NVIC_PROIRITY_SUB_WIDTH (4 - NVIC_PROIRITY_BASE_WIDTH)

#define EXTI_IRQ_GROUPS 7

// Absorb the difference in IMR and PR assignments to registers

#if defined(STM32H7)
#define EXTI_REG_IMR (EXTI_D1->IMR1)
#define EXTI_REG_PR (EXTI_D1->PR1)
#else
#define EXTI_REG_IMR (EXTI->IMR)
#define EXTI_REG_PR (EXTI->PR)
#endif

enum class NvicPriority : uint8_t {
  PriorityVeryHigh,
  PriorityHigh,
  PriorityMedium,
  PriorityLow,
  PriorityVeryLow
};

// Please make sure there are hardware debounce circuits for EXTI.
class NvicExti : protected GpioDefine::Gpio {
private:
  GpioDefine::Mode _triggerSrc;
  NvicPriority _priority;

public:
  using ExtiCallback = std::function<void(NvicExti *self)>;
  ExtiCallback _callback;

  NvicExti();
  explicit NvicExti(GpioDefine::PinDesc pinDesc, GpioDefine::Mode triggerSrc,
                    NvicPriority priority);
  explicit NvicExti(GpioDefine::Port port, GpioDefine::Pin pin,
                    GpioDefine::Mode triggerSrc, NvicPriority priority);

  void init() override;
  void setCallback(ExtiCallback cb);
  void release(void);
  void enable(void);
  void disable(void);

  using Gpio::isInitialized;
  using Gpio::read;
};