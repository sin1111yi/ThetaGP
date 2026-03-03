#pragma once

#define NVIC_PRIORITYGROUP NVIC_PRIORITYGROUP_2
#define NVIC_BUILD_PRIORITY(base, sub)                                         \
  (((((base) << (4 - (7 - (PLATFORM_NVIC_PRIORITYGROUP)))) |                   \
     ((sub) & (0x0f >> (7 - (PLATFORM_NVIC_PRIORITYGROUP)))))                  \
    << 4) &                                                                    \
   0xf0)
#define NVIC_PRIORITY_BASE(prio)                                               \
  (((prio) >> (4 - (7 - (PLATFORM_NVIC_PRIORITYGROUP)))) >> 4)
#define NVIC_PRIORITY_SUB(prio)                                                \
  (((prio) & (0x0f >> (7 - (PLATFORM_NVIC_PRIORITYGROUP)))) >> 4)

#define NVIC_PROIRITY_BASE_WIDTH (2)
#define NVIC_PRIORITY_SUB_WIDTH (4 - NVIC_PROIRITY_BASE_WIDTH)

void SystemClock_Config(void);
