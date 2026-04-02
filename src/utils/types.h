#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  rvSucceed,
  rvFailed,
} retval_t;

// common types
#define	Pin_t		int32_t		// signed to accommodate for -1
#define Mask_t		uint32_t

#ifdef __cplusplus
}
#endif
