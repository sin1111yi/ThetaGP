#include "BoardConfig.h"

#include "tusb.h"

#include "drivers/peripherals/systick.h"
#include "drivers/peripherals/usb/usb.h"

#include "stm32h7xx.h"

using namespace ThetaGP::Drivers::Periph;

#ifdef __cplusplus
extern "C" {
#endif

#if defined(STM32H7)

// =============================================================================
// Interrupt Handlers
// =============================================================================

// Despite being call USB2_OTG_FS on some MCUs
// OTG_FS is marked as RHPort0 by TinyUSB to be consistent across stm32 port
void OTG_FS_IRQHandler(void) {
  tusb_int_handler(0, true);
}

// Despite being call USB1_OTG_HS on some MCUs
// OTG_HS is marked as RHPort1 by TinyUSB to be consistent across stm32 port
void OTG_HS_IRQHandler(void) {
  tusb_int_handler(1, true);
}

// =============================================================================
// Time API
// =============================================================================

uint32_t tusb_time_millis_api(void) {
  return millis();
}

void tusb_time_delay_ms_api(uint32_t ms) {
  delay_ms(ms);
}

// =============================================================================
// D-Cache API (for DMA operations)
// =============================================================================

// Round up size to cache line size (32 bytes for Cortex-M7)
static inline uint32_t round_up_to_cache_line(uint32_t size) {
  const uint32_t cache_line_size = 32;
  if (size & (cache_line_size - 1)) {
    size = (size & ~(cache_line_size - 1)) + cache_line_size;
  }
  return size;
}

// Check if address is in cacheable memory region
static inline bool is_cacheable_memory(uintptr_t addr) {
  // Check if D-Cache is enabled
  if (!(SCB->CCR & SCB_CCR_DC_Msk)) {
    return false;
  }

  // DTCM is not cacheable (USB DMA can't access DTCM anyway)
  if (addr >= 0x20000000 && addr <= 0x2001FFFF) {
    return false;
  }

  return true;
}

void tusb_app_dcache_flush(uintptr_t addr, uint32_t data_size) {
  if (!is_cacheable_memory(addr)) {
    return;
  }

  data_size = round_up_to_cache_line(data_size);
  SCB_CleanDCache_by_Addr((uint32_t*)addr, (int32_t)data_size);
}

void tusb_app_dcache_invalidate(uintptr_t addr, uint32_t data_size) {
  if (!is_cacheable_memory(addr)) {
    return;
  }

  data_size = round_up_to_cache_line(data_size);
  SCB_InvalidateDCache_by_Addr((void*)addr, (int32_t)data_size);
}

// =============================================================================
// TinyUSB DCD/HCD Memory API (required by DWC2 driver)
// =============================================================================

bool dcd_dcache_clean(const void* addr, uint32_t data_size) {
  if (!is_cacheable_memory((uintptr_t)addr)) {
    return false;
  }
  data_size = round_up_to_cache_line(data_size);
  SCB_CleanDCache_by_Addr((uint32_t*)addr, (int32_t)data_size);
  return true;
}

bool dcd_dcache_invalidate(const void* addr, uint32_t data_size) {
  if (!is_cacheable_memory((uintptr_t)addr)) {
    return false;
  }
  data_size = round_up_to_cache_line(data_size);
  SCB_InvalidateDCache_by_Addr((void*)addr, (int32_t)data_size);
  return true;
}

bool dcd_dcache_clean_invalidate(const void* addr, uint32_t data_size) {
  if (!is_cacheable_memory((uintptr_t)addr)) {
    return false;
  }
  data_size = round_up_to_cache_line(data_size);
  SCB_CleanInvalidateDCache_by_Addr((uint32_t*)addr, (int32_t)data_size);
  return true;
}

bool hcd_dcache_clean(const void* addr, uint32_t data_size) {
  return dcd_dcache_clean(addr, data_size);
}

bool hcd_dcache_invalidate(const void* addr, uint32_t data_size) {
  return dcd_dcache_invalidate(addr, data_size);
}

bool hcd_dcache_clean_invalidate(const void* addr, uint32_t data_size) {
  return dcd_dcache_clean_invalidate(addr, data_size);
}

#endif

#ifdef __cplusplus
}
#endif
