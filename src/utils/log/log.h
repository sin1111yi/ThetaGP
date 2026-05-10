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
#define LOG_LV_ALWAYS 0x80

typedef enum {
  LOG_LV(Info)      = 0,
  LOG_LV(Warn)      = 1,
  LOG_LV(Error)     = 2,
  LOG_LV(Interface) = LOG_LV_ALWAYS | 0,
  LOG_LV(Debug)     = LOG_LV_ALWAYS | 1,
} LogLevel;

typedef void (*LogPrintFunc)(uint8_t *data, uint16_t len);

#if THETAGP_CFG_LOG_ENABLE

#if !defined(LOG_BUFFER_SIZE)
#define LOG_BUFFER_SIZE 256
#endif

void LogInit(LogPrintFunc func);
void LogPrint(LogLevel level, const char *file, uint16_t line,
              const char *format, va_list args);

static inline void _LOG(LogLevel level, const char *file, uint16_t line,
                        const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogPrint(level, file, line, fmt, args);
  va_end(args);
}

/* clang-format off */
#define LOG_DEBUG(format, ...)  _LOG(LOG_LV(Debug), __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)   _LOG(LOG_LV(Info), __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)   _LOG(LOG_LV(Warn), __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...)  _LOG(LOG_LV(Error), __FILE__, __LINE__, format, ##__VA_ARGS__)

#ifndef LOG_IF
#define LOG_IF(format, ...)     _LOG(LOG_LV(Interface), __FILE__, __LINE__, format, ##__VA_ARGS__)
#endif
/* clang-format on */

#else

static inline void LogInit(LogPrintFunc func) { (void)func; }

#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...)  ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_IF(...)    ((void)0)

#endif

#define LOG_INIT LogInit

#ifdef __cplusplus
}
#endif