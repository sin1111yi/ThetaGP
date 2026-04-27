/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "BoardConfig.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_LV(level) CONTACT(LOG_LV_, level)

typedef enum {
  LOG_LV(Interface) = 0,
  LOG_LV(Debug),
  LOG_LV(Error),
  LOG_LV(Warn),
  LOG_LV(Info),
} LogLevel;

typedef void (*LogPrintFunc)(uint8_t *data, uint16_t len);

#if THETAGP_CFG_LOG_ENABLE

#if !defined(LOG_BUFFER_SIZE)
#define LOG_BUFFER_SIZE 256
#endif

void LogInit(LogPrintFunc func);
void LogPrint(LogLevel level, const char *file, const char *format,
              va_list args);

static inline void _LOG(LogLevel level, char *file, char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogPrint(level, file, fmt, args);
  va_end(args);
}

static inline int _LOG2(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogPrint(LOG_LV(Interface), NULL, fmt, args);
  va_end(args);

  return 0;
}

/* clang-format off */
#define LOG_DEBUG(format, ...)  _LOG(LOG_LV(Debug), __FILE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)   _LOG(LOG_LV(Info), __FILE__, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)   _LOG(LOG_LV(Warn), __FILE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...)  _LOG(LOG_LV(Error), __FILE__, format, ##__VA_ARGS__)
#define LOG_IF(format, ...)     _LOG2(format, ##__VA_ARGS__)
/* clang-format on */

#else

static inline void LogInit(LogPrintFunc func) { (void)func; }

#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...)  ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_ERROR(...) ((void)0)

#endif

#ifdef __cplusplus
}
#endif