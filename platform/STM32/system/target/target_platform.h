#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(STM32H7)

#define USB_DTCM_RAM
#define USB_ITCM_RAM
#define USB_RAM_D2
#define USB_RAM_D3

#define DTCM_RAM_DATA   __atrribute__((section(".dtcmram_data")))
#define RAM_DATA        __attribute__((section(".ram_data")))
#define RAM_D2_DATA     __attribute__((section(".ram_d2_data")))
#define RAM_D3_DATA     __attribute__((section(".ram_d3_data")))

#else

#define DTCM_RAM_DATA
#define RAM_DATA
#define RAM_D2_DATA
#define RAM_D3_DATA

#endif

void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif
