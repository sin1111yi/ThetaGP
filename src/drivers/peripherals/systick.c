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

#include "drivers/peripherals/systick.h"

#include "build_info.h"

#include "build/atomic.h"
#include "utils/time.h"

#define DWT_LAR_UNLOCK_VALUE 0xC5ACCE55

// cycles per microsecond
static uint32_t usTicks = 0;
static float usTicksInv = 0.0f;
// current uptime for 1kHz systick timer. will rollover
// after 49 days. hopefully we won't care.
static volatile uint32_t sysTickUptime = 0;
static volatile uint32_t sysTickValStamp = 0;
// cached value of RCC->CSR
uint32_t cachedRccCsrValue;
static uint32_t cpuClockFrequency = 0;

static volatile int sysTickPending = 0;

#if defined(STM32H7)

#include "drivers/peripherals/nvic.h"

void cycleCounterInit(void) {
#if defined(USE_HAL_DRIVER)
  cpuClockFrequency = HAL_RCC_GetSysClockFreq();
#else
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);
  cpuClockFrequency = clocks.SYSCLK_Frequency;
#endif
  usTicks = cpuClockFrequency / 1000000;
  usTicksInv = 1e6f / cpuClockFrequency;

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

#if defined(DWT_LAR_UNLOCK_VALUE)
#if defined(STM32H7)
  ITM->LAR = DWT_LAR_UNLOCK_VALUE;
#elif defined(STM32F7)
  DWT->LAR = DWT_LAR_UNLOCK_VALUE;
#elif defined(STM32F4)
  // Note: DWT_Type does not contain LAR member.
#define DWT_LAR
  __O uint32_t *DWTLAR = (uint32_t *)(DWT_BASE + 0x0FB0);
  *(DWTLAR) = DWT_LAR_UNLOCK_VALUE;
#endif
#endif

  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void SysTick_Handler(void) {
  ATOMIC_BLOCK(NVIC_PRIO_MAX) {
    sysTickUptime++;
    sysTickValStamp = SysTick->VAL;
    sysTickPending = 0;
    (void)(SysTick->CTRL);
  }
#ifdef USE_HAL_DRIVER
  // used by the HAL for some timekeeping and timeouts,
  // should always be 1ms
  HAL_IncTick();
#endif
}

uint32_t microsISR(void) {
  register uint32_t ms, pending, cycle_cnt;

  ATOMIC_BLOCK(NVIC_PRIO_MAX) {
    cycle_cnt = SysTick->VAL;

    if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) {
      // Update pending.
      // Record it for multiple calls within the same
      // rollover period (Will be cleared when serviced).
      // Note that multiple rollovers are not considered.

      sysTickPending = 1;

      // Read VAL again to ensure the value is read after
      // the rollover.

      cycle_cnt = SysTick->VAL;
    }

    ms = sysTickUptime;
    pending = sysTickPending;
  }

  return ((ms + pending) * 1000) + (usTicks * 1000 - cycle_cnt) / usTicks;
}

uint32_t micros(void) {
  register uint32_t ms, cycle_cnt;

  // Call microsISR() in interrupt and elevated (non-zero)
  // BASEPRI context

  if ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) || (__get_BASEPRI())) {
    return microsISR();
  }

  do {
    ms = sysTickUptime;
    cycle_cnt = SysTick->VAL;
  } while (ms != sysTickUptime || cycle_cnt > sysTickValStamp);

  return (ms * 1000) + (usTicks * 1000 - cycle_cnt) / usTicks;
}

uint32_t getCycleCounter(void) { return DWT->CYCCNT; }
#else
// TODO: Implement for other MCUs
#endif

int32_t clockCyclesToMicros(int32_t clockCycles) {
  return clockCycles / usTicks;
}

float clockCyclesToMicrosf(int32_t clockCycles) {
  return clockCycles * usTicksInv;
}

// Note that this conversion is signed as this is used for
// periods rather than absolute timestamps
int32_t clockCyclesTo10thMicros(int32_t clockCycles) {
  return 10 * clockCycles / (int32_t)usTicks;
}

// Note that this conversion is signed as this is used for
// periods rather than absolute timestamps
int32_t clockCyclesTo100thMicros(int32_t clockCycles) {
  return 100 * clockCycles / (int32_t)usTicks;
}

uint32_t clockMicrosToCycles(uint32_t micros) { return micros * usTicks; }

// Return system uptime in milliseconds (rollover in 49
// days)
uint32_t millis(void) { return sysTickUptime; }

#if 1
void delay_us(uint32_t us) {
  uint32_t now = micros();
  while (micros() - now < us)
    ;
}
#else
void delayMicroseconds(uint32_t us) {
  uint32_t elapsed = 0;
  uint32_t lastCount = SysTick->VAL;

  for (;;) {
    register uint32_t current_count = SysTick->VAL;
    uint32_t elapsed_us;

    // measure the time elapsed since the last time we
    // checked
    elapsed += current_count - lastCount;
    lastCount = current_count;

    // convert to microseconds
    elapsed_us = elapsed / usTicks;
    if (elapsed_us >= us)
      break;

    // reduce the delay by the elapsed time
    us -= elapsed_us;

    // keep fractional microseconds for the next iteration
    elapsed %= usTicks;
  }
}
#endif

void delay_ms(uint32_t ms) {
  while (ms--)
    delay_us(1000);
}

uint32_t time_us_32(void) { return (uint64_t)millis(); }
