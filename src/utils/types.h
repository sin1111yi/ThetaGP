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
 * @brief Result type with [[nodiscard]] enforcement
 *
 * Modern C++ class wrapping operation result codes.
 * Use isOk() / isError() or compare against Result::Value constants.
 */
class [[nodiscard]] Result {
public:
  enum Value : int32_t {
    Ok = 0,            // Operation succeeded
    Error = -1,        // Operation failed
    Timeout = -2,      // Operation timed out
    Busy = -3,         // Resource busy
    NoMemory = -4,     // Out of memory
    InvalidParam = -5, // Invalid parameter
    NotReady = -6,     // Device not ready
    Unsupported = -7,  // Operation unsupported
  };

  constexpr Result() = default;
  constexpr Result(Value v) : _value(v) {}

  constexpr bool operator==(Value v) const { return _value == v; }
  constexpr bool operator!=(Value v) const { return _value != v; }
  constexpr bool operator==(const Result &) const = default;
  constexpr bool operator!=(const Result &) const = default;

  [[nodiscard]] constexpr bool isOk() const { return _value == Ok; }
  [[nodiscard]] constexpr bool isError() const { return _value != Ok; }
  [[nodiscard]] constexpr Value value() const { return _value; }

  [[nodiscard]] const char *toString() const;

private:
  Value _value = Ok;
};

/**
 * @brief Check if result indicates success
 */
[[nodiscard]] constexpr inline bool isOk(Result r) { return r.isOk(); }

/**
 * @brief Check if result indicates error
 */
[[nodiscard]] constexpr inline bool isError(Result r) { return r.isError(); }

} // namespace ThetaGP

#endif // __cplusplus
