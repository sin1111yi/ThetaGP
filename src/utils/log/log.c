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

#include <string.h>

#include "utils/log/log.h"

#if THETAGP_CFG_LOG_ENABLE

static const char *g_RootPath = NULL;
static LogPrintFunc g_PrintCallback = NULL;
static LogLevel g_LogLevel
#if defined(THETAGP_CFG_LOG_LEVEL)
    = LOG_LV(THETAGP_CFG_LOG_LEVEL);
#else
    = LOG_LV(Error);
#endif

static const char *LogRelativePath(const char *file) {
  if (!g_RootPath || !*g_RootPath) {
    return file;
  }

  const char *path = file;
  size_t base_len = strlen(g_RootPath);

  if (strncmp(file, g_RootPath, base_len) == 0) {
    path = file + base_len;

    if (*path == '/' || *path == '\\') {
      path++;
    }
  }

  return path;
}

static const char *LogLevelHint(LogLevel level) {
  switch (level) {
  case LOG_LV(Debug):
    return "DEBUG";
  case LOG_LV(Info):
    return "INFO ";
  case LOG_LV(Warn):
    return "WARN ";
  case LOG_LV(Error):
    return "ERROR";
  case LOG_LV(Interface):
    return "LOGIF";
  default:
    return "     ";
  }
}

static void LogSetRootPath(void) {
#if defined(THETAGP_SRC_PATH)
  g_RootPath = THETAGP_SRC_PATH;
#else
#warning "Using null as LOG root path!"
  g_RootPath = NULL;
#endif
}

void LogPrint(LogLevel level, const char *file, const char *format,
              va_list args) {
  if (level > g_LogLevel && level != LOG_LV(Interface))
    return;

  const char *level_str = LogLevelHint(level);
  const char *relative_file = LogRelativePath(file);
  uint16_t prefix_len = 0;

  static char buffer[LOG_BUFFER_SIZE];
  memset(buffer, 0, sizeof(buffer));

  if (level == LOG_LV(Interface)) {
    UNUSED(level_str);
    prefix_len = snprintf(buffer, sizeof(buffer), "[%s]: ", level_str);
  } else {
    prefix_len = snprintf(buffer, sizeof(buffer), "[%s][%s]: ", level_str,
                          relative_file);
  }

  if (prefix_len > 0 && prefix_len < (int)sizeof(buffer)) {
    vsnprintf(buffer + prefix_len, sizeof(buffer) - prefix_len, format, args);
  }

  buffer[sizeof(buffer) - 1] = '\0';

  if (g_PrintCallback != NULL)
    g_PrintCallback((uint8_t *)buffer, strlen(buffer));
}

void LogInit(LogPrintFunc func) {
  g_PrintCallback = func;
  LogSetRootPath();
}

#endif