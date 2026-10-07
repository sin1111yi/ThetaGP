/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "build_info.h"

#include "drivers/peripherals/nvic.h"
#include "drivers/peripherals/nvic_exti.h"
#include "drivers/peripherals/timer.h"

#include "drivers/peripherals/dma_manager.h"
#include "drivers/peripherals/gpio.h"

#include "utils/log/log.h"

#include <array>

#if defined(STM32H7)
#define TIM_IRQ_GROUPS 17
#endif

using namespace ThetaGP;
using namespace ThetaGP::Drivers::Peripheral;
using namespace ThetaGP::Drivers::Peripheral::GPIO;
using namespace ThetaGP::Drivers::Peripheral::TIMER;

struct HalTimer {
  TIM_HandleTypeDef htim;
};

#define HANDLE (static_cast<HalTimer *>(_halHandle)->htim)

#if defined(STM32H7)

// Static registry for ISR dispatch
COMMON_ZERO_INIT static std::array<HardwareTimer *, TIM_IRQ_GROUPS> hwTimerInstance = {};

const std::array<TIM_TypeDef *, TIM_IRQ_GROUPS> timerInstance = {
    TIM1,  TIM2,  TIM3,  TIM4,  TIM5,  TIM6,    TIM7,    TIM8,   TIM12,
    TIM13, TIM14, TIM15, TIM16, TIM17, nullptr, nullptr, nullptr // Placeholders
};

constexpr std::array<IRQn_Type, TIM_IRQ_GROUPS> timerGroupIRQn = {
    TIM1_UP_IRQn,
    TIM2_IRQn,
    TIM3_IRQn,
    TIM4_IRQn,
    TIM5_IRQn,
    TIM6_DAC_IRQn,
    TIM7_IRQn,
    TIM8_BRK_TIM12_IRQn,
    TIM8_UP_TIM13_IRQn,
    TIM8_UP_TIM13_IRQn,
    TIM8_TRG_COM_TIM14_IRQn,
    TIM15_IRQn,
    TIM16_IRQn,
    TIM17_IRQn,
    static_cast<IRQn_Type>(-1), // Placeholder
    static_cast<IRQn_Type>(-1), // Placeholder
    static_cast<IRQn_Type>(-1), // Placeholder
};
#else
#error "Unknown CPU"
#endif

void HardwareTimer::enableClock() const {
  using ClockFunc = void (*)();

  static constexpr std::array<ClockFunc, 17> clockEnableTable = {{
#if defined(STM32H7)
      []() { __HAL_RCC_TIM1_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM2_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM3_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM4_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM5_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM6_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM7_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM8_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM12_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM13_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM14_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM15_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM16_CLK_ENABLE(); },
      []() { __HAL_RCC_TIM17_CLK_ENABLE(); },
      []() {},
      []() {},
      []() {},
#else
      /* clang-format off */
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
      []() {},
  /* clang-format on */
#endif
  }};

  const auto index = static_cast<size_t>(_state.instance);
  if (index < clockEnableTable.size()) {
    clockEnableTable[index]();
  }
}

HardwareTimer::HardwareTimer() { _halHandle = new HalTimer(); }

HardwareTimer::HardwareTimer(Instance instance) : HardwareTimer() {
  _state.instance = instance;
}

uint32_t HardwareTimer::toHalTriggerEvent(TriggerEvent evt) {
  static constexpr uint32_t map[] = {
      TIM_TRGO_RESET,  TIM_TRGO_ENABLE, TIM_TRGO_UPDATE, TIM_TRGO_OC1,
      TIM_TRGO_OC1REF, TIM_TRGO_OC2REF, TIM_TRGO_OC3REF, TIM_TRGO_OC4REF,
  };
  return map[static_cast<size_t>(evt)];
}

void HardwareTimer::config(Instance instance, uint32_t frequency) {
  _state.instance = instance;
  _state.targetFrequency = frequency;

  HANDLE.Init.CounterMode = TIM_COUNTERMODE_UP;
  HANDLE.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  HANDLE.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  HANDLE.Init.RepetitionCounter = 0;
}

void HardwareTimer::config(Instance instance, uint32_t frequency,
                           NVIC_EXTI::NvicPriority prio) {
  config(instance, frequency);
  _state.priority = prio;
}

void HardwareTimer::config(Instance instance, uint32_t frequency,
                           NVIC_EXTI::NvicPriority prio,
                           TriggerEvent triggerEvent) {
  config(instance, frequency, prio);
  _state.triggerEvent = triggerEvent;
}

/**
 * @brief Get timer clock frequency based on timer instance
 */
uint32_t HardwareTimer::getTimerClock() const {
  uint32_t timerClock;

  if (_state.instance == Instance::Timer6 ||
      _state.instance == Instance::Timer7) {
    timerClock = HAL_RCC_GetPCLK1Freq();
    if (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) {
      timerClock *= 2;
    }
  } else if (_state.instance == Instance::Timer1 ||
             _state.instance == Instance::Timer8) {
    timerClock = HAL_RCC_GetPCLK2Freq();
    if (RCC->D2CFGR & RCC_D2CFGR_D2PPRE2) {
      timerClock *= 2;
    }
  } else {
    timerClock = HAL_RCC_GetPCLK1Freq();
    if (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) {
      timerClock *= 2;
    }
  }

  return timerClock;
}

/**
 * @brief Calculate prescaler and period based on target frequency
 *
 * Formula: frequency = timerClock / ((prescaler+1) * (period+1))
 *
 * @param frequency Target frequency in Hz
 */
void HardwareTimer::calculatePrescalerAndPeriod(uint32_t frequency) {
  if (frequency == 0) {
    return;
  }

  uint32_t timerClock = getTimerClock();

  // Calculate total divisor
  // (prescaler+1) * (period+1) = timerClock / frequency
  uint32_t totalDivisor = timerClock / frequency;

  // Strategy: Find suitable prescaler and period combination
  // Priority: Keep period within 16-bit range (1-65535)
  // Prefer smaller prescaler for better resolution

  uint32_t prescaler = 0;
  uint32_t period = totalDivisor - 1;

  // If period exceeds 16-bit range, increase prescaler
  while (period > 65535 && prescaler < 65535) {
    prescaler++;
    period = totalDivisor / (prescaler + 1) - 1;
  }

  // Boundary check: if period still too large, clamp it
  if (period > 65535) {
    period = 65535;
    prescaler = totalDivisor / 65536 - 1;
  }

  // Boundary check: if frequency is too high, use minimum values
  if (prescaler == 0 && period == 0) {
    period = 1; // Minimum period
  }

  HANDLE.Init.Prescaler = prescaler;
  HANDLE.Init.Period = period;
}

void HardwareTimer::setCallback(TimerCallbackFunc cb, void *context) {
  _callback = cb;
  _context = context;
}

void HardwareTimer::init() {
  if (_state.initialized) {
    return;
  }

#if defined(STM32H7)
  if (_state.targetFrequency != 0) {
    calculatePrescalerAndPeriod(_state.targetFrequency);
  } else {
    return;
  }

  TIM_MasterConfigTypeDef sMasterConfig = TIM_MasterConfigTypeDef{};

  const auto timerIdx = static_cast<size_t>(_state.instance);

  if (timerIdx >= timerInstance.size() || !timerInstance[timerIdx]) {
    return;
  }

  HANDLE.Instance = timerInstance[timerIdx];

  if (HANDLE.Instance == nullptr) {
    return;
  }

  enableClock();
  HAL_NVIC_SetPriority(
      timerGroupIRQn[timerIdx],
      NVIC_PRIORITY_BASE(static_cast<uint32_t>(_state.priority)),
      NVIC_PRIORITY_SUB(static_cast<uint32_t>(_state.priority)));
  HAL_NVIC_EnableIRQ(timerGroupIRQn[timerIdx]);

  if (HAL_TIM_Base_Init(&HANDLE) != HAL_OK) {
    return;
  }

  sMasterConfig.MasterOutputTrigger =
      HardwareTimer::toHalTriggerEvent(_state.triggerEvent);
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&HANDLE, &sMasterConfig);

  if (timerIdx < hwTimerInstance.size()) {
    hwTimerInstance[timerIdx] = this;
  }

  if (static_cast<int>(timerGroupIRQn[timerIdx]) < 0) {
    return;
  }

  _state.initialized = true;
  _state.running = false;
#endif
}

void HardwareTimer::start() {
  if (!_state.initialized || _state.running) {
    return;
  }

  if (HAL_TIM_Base_Start_IT(&HANDLE) == HAL_OK) {
    _state.running = true;
  }
}

void HardwareTimer::stop() {
  if (!_state.running) {
    return;
  }

  HAL_TIM_Base_Stop_IT(&HANDLE);
  _state.running = false;
}

// ── PWM output ──

namespace {

// The timer a channel belongs to, the channel's selector, and the DMA request
// its update event raises.
struct ChannelMap {
  TimerChannel channel;
  Instance instance;
  uint32_t llChannel;
  uint32_t dmamuxRequest;
};

constexpr ChannelMap kChannelMap[] = {
    {TimerChannel::Tim1Ch4, Instance::Timer1, LL_TIM_CHANNEL_CH4,
     DMA_REQUEST_TIM1_UP},
};

// The pins each channel comes out of.
struct ChannelPinAf {
  TimerChannel channel;
  GPIO::Port port;
  GPIO::Pin pin;
  uint8_t af;
};

constexpr ChannelPinAf kChannelPinAf[] = {
    {TimerChannel::Tim1Ch4, Port::PortE, Pin::Pin14, 1},
};

const ChannelMap *lookupChannel(TimerChannel channel) {
  for (const auto &entry : kChannelMap) {
    if (entry.channel == channel) {
      return &entry;
    }
  }
  return nullptr;
}

uint8_t lookupAf(TimerChannel channel, Port port, Pin pin) {
  for (const auto &entry : kChannelPinAf) {
    if (entry.channel == channel && entry.port == port && entry.pin == pin) {
      return entry.af;
    }
  }
  return 0xFF;
}

// The compare register a channel's duty is written to.
uint32_t compareAddress(TIM_TypeDef *timer, TimerChannel channel) {
  switch (channel) {
  case TimerChannel::Tim1Ch4:
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&timer->CCR4));
  default:
    return 0;
  }
}

void setCompare(TIM_TypeDef *timer, TimerChannel channel, uint32_t duty) {
  switch (channel) {
  case TimerChannel::Tim1Ch4:
    LL_TIM_OC_SetCompareCH4(timer, duty);
    break;
  default:
    break;
  }
}

} // namespace

bool HardwareTimer::initPwm(TimerChannel channel, const GPIO::PinDesc &pin,
                            uint32_t frequency) {
  const ChannelMap *map = lookupChannel(channel);
  if (map == nullptr) {
    LOG_ERROR("timer: channel is not one the platform brings up");
    return false;
  }

  const uint8_t af = lookupAf(channel, pin.port, pin.pin);
  if (af == 0xFF) {
    LOG_ERROR("timer: no alternate function for this pin on this channel");
    return false;
  }

  Gpio line(pin);
  line.config(GPIO::Mode::AlternateFunctionPushPull, GPIO::Pull::NoPull,
              GPIO::Speed::High, af);
  line.init();

  // The carrier is the base timer a periodic interrupt would run on: the same
  // configuration path, with the counter left stopped for startSequence().
  config(map->instance, frequency);
  init();
  if (!_state.initialized) {
    LOG_ERROR("timer: %lu Hz is not a carrier this timer can make",
              static_cast<uint32_t>(frequency));
    return false;
  }

  const uint32_t period = static_cast<uint32_t>(HANDLE.Init.Period) + 1;
  if (period > 0xFFFF) {
    LOG_ERROR("timer: the carrier's period does not fit a duty");
    return false;
  }
  _periodTicks = static_cast<uint16_t>(period);

  TIM_TypeDef *timer = timerInstance[static_cast<size_t>(map->instance)];

  LL_TIM_OC_SetMode(timer, map->llChannel, LL_TIM_OCMODE_PWM1);
  LL_TIM_OC_SetPolarity(timer, map->llChannel, LL_TIM_OCPOLARITY_HIGH);
  LL_TIM_OC_EnablePreload(timer, map->llChannel);
  setCompare(timer, channel, 0);
  LL_TIM_CC_EnableChannel(timer, map->llChannel);
  LL_TIM_EnableDMAReq_UPDATE(timer);
  // An advanced-control timer holds its outputs off until the main output is on.
  LL_TIM_EnableAllOutputs(timer);

  _pwmDma = DMA::DmaManager::getInstance().allocate(DMA::Controller::Dma1,
                                                    map->dmamuxRequest);
  if (_pwmDma == nullptr) {
    LOG_ERROR("timer: no DMA stream left on DMA1");
    return false;
  }

  // The sequence is short and the carrier's timing holds only while it runs.
  DMA::DmaConfig cfg;
  cfg.direction = DMA::Direction::MemoryToPeripheral;
  cfg.srcDataWidth = DMA::DataWidth::HalfWord;
  cfg.destDataWidth = DMA::DataWidth::HalfWord;
  cfg.priority = DMA::Priority::High;
  cfg.srcIncrement = true;
  cfg.destIncrement = false;

  _pwmDma->configure(cfg);
  if (_pwmDma->init() != Result::Ok) {
    LOG_ERROR("timer: DMA stream init failed");
    return false;
  }

  _pwmChannel = channel;
  _state.initialized = true;
  return true;
}

bool HardwareTimer::startSequence(const uint16_t *duties, uint16_t count) {
  const ChannelMap *map = lookupChannel(_pwmChannel);
  if (map == nullptr || duties == nullptr || count == 0 || isBusy()) {
    return false;
  }

  TIM_TypeDef *timer = timerInstance[static_cast<size_t>(map->instance)];

  // The first duty reaches the compare register before the counter runs, so the
  // sequence opens on it instead of on a stale low period; the DMA is armed
  // after that event, so the request it raises finds no transfer to start. The
  // transfer then carries the duties after the first, one per period. An update
  // request fires whatever the compare register holds, so a zero-duty tail
  // reaches the channel too.
  LL_TIM_DisableCounter(timer);
  LL_TIM_SetCounter(timer, 0);
  setCompare(timer, _pwmChannel, duties[0]);
  LL_TIM_GenerateEvent_UPDATE(timer);

  const Result started =
      _pwmDma->start(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&duties[1])),
                     compareAddress(timer, _pwmChannel),
                     static_cast<uint16_t>(count - 1));
  if (started != Result::Ok) {
    return false;
  }

  LL_TIM_EnableCounter(timer);
  return true;
}

bool HardwareTimer::isBusy() const {
  return _pwmDma != nullptr && _pwmDma->isBusy();
}

extern "C" {

#if defined(STM32H7)

#define TIM_EVENT_MASK 0xFF

#if defined(STM32H7)
#define TIM_REG_DIER TIMx->DIER
#define TIM_REG_SR   TIMx->SR
#endif

static void TIMx_IRQHandler(uint32_t timMask) {
  uint32_t timerIdx = __builtin_ctz(timMask);
  TIM_TypeDef *TIMx = timerInstance[timerIdx];

  if (!TIMx)
    return;

  uint32_t tim_active = (TIM_REG_DIER & TIM_REG_SR) & TIM_EVENT_MASK;

  auto *instance = hwTimerInstance[timerIdx];
  TIM_REG_SR = ~tim_active; // Clear the interrupt flags

  if (instance) {
    instance->callback();
  }
}

#define timerIrqHandler(name, timerx)                                          \
  void name(void) { TIMx_IRQHandler(timerx); }                                 \
  struct dummy

#define TIMER(x) (1U << static_cast<uint32_t>(Instance::Timer##x))

timerIrqHandler(TIM1_BRK_IRQHandler, TIMER(1));
timerIrqHandler(TIM1_UP_IRQHandler, TIMER(1));
timerIrqHandler(TIM1_TRG_COM_IRQHandler, TIMER(1));
timerIrqHandler(TIM1_CC_IRQHandler, TIMER(1));
timerIrqHandler(TIM2_IRQHandler, TIMER(2));
timerIrqHandler(TIM3_IRQHandler, TIMER(3));
timerIrqHandler(TIM4_IRQHandler, TIMER(4));
timerIrqHandler(TIM5_IRQHandler, TIMER(5));
timerIrqHandler(TIM6_DAC_IRQHandler, TIMER(6));
timerIrqHandler(TIM7_IRQHandler, TIMER(7));
timerIrqHandler(TIM8_BRK_TIM12_IRQHandler, TIMER(8) | TIMER(12));
timerIrqHandler(TIM8_UP_TIM13_IRQHandler, TIMER(8) | TIMER(13));
timerIrqHandler(TIM8_TRG_COM_TIM14_IRQHandler, TIMER(8) | TIMER(14));
// timerIrqHandler(TIM8_CC_IRQHandler, TIMER(8));
timerIrqHandler(TIM15_IRQHandler, TIMER(15));
timerIrqHandler(TIM16_IRQHandler, TIMER(16));
timerIrqHandler(TIM17_IRQHandler, TIMER(17));
#endif
}
