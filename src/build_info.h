#pragma once

#include "target/target_platform.h"

#include "BoardConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAST_CODE   DTCM_RAM_DATA
#define COMMON_CODE RAM_DATA

#ifdef __cplusplus
}
#endif
