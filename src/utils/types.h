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

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// common types
#define Pin_t  int32_t // signed to accommodate for -1
#define Mask_t uint32_t

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace ThetaGP {

/**
 * @brief Return value status codes
 *
 * Modern C++ enum class for type safety
 */
enum class RetVal : int32_t {
  Ok = 0,            // Operation succeeded
  Error = -1,        // Operation failed
  Timeout = -2,      // Operation timed out
  Busy = -3,         // Resource busy
  NoMemory = -4,     // Out of memory
  InvalidParam = -5, // Invalid parameter
  NotReady = -6,     // Device not ready
  Unsupported = -7,  // Operation unsupported
};

/**
 * @brief Check if return value indicates success
 */
constexpr inline bool isOk(RetVal rv) { return rv == RetVal::Ok; }

/**
 * @brief Check if return value indicates error
 */
constexpr inline bool isError(RetVal rv) { return rv != RetVal::Ok; }

} // namespace ThetaGP

// C compatibility macro
#define RETVAL_OK  static_cast<int32_t>(ThetaGP::RetVal::Ok)
#define RETVAL_ERR static_cast<int32_t>(ThetaGP::RetVal::Error)

#endif // __cplusplus
