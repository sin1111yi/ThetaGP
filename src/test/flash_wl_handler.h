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
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <ArduinoJson.h>

namespace ThetaGP::Test {

#ifdef THETAGP_ENABLE_TEST_API

/**
 * FlashWlHandler -- handles commands in the `flash.` and `wl.` domains.
 *
 * Supported commands:
 *   flash.read       {addr, len}       -- Read raw bytes from flash at address
 *   flash.write      {addr, data_hex}  -- Write raw bytes to flash at address
 *   flash.sector_erase {addr}          -- Erase a 4KB flash sector
 *   wl.store  {key, data_hex}       -- Wear-level store (max 512 bytes payload)
 *   wl.load   {key}                 -- Wear-level load
 *   wl.erase  {key}                 -- Wear-level erase (mark as STALE)
 *   wl.status {}                    -- Report wear-leveling status + layout
 *   wl.meta_get {}                  -- Read meta sector content (JSON)
 *   wl.meta_write {version, firmware_meta_hex} -- Write meta sector (first boot test)
 *   wl.full_scan {}                 -- Full flash re-scan, return valid count
 *   wl.info {}                      -- Flash layout (addresses, sector counts)
 */
class FlashWlHandler {
public:
  FlashWlHandler() = default;
  FlashWlHandler(const FlashWlHandler &) = delete;
  FlashWlHandler &operator=(const FlashWlHandler &) = delete;

  static FlashWlHandler &getInstance();
  static void handleFlash(const char *cmd, JsonDocument &doc);
  static void handleWl(const char *cmd, JsonDocument &doc);
  static void registerHandlers();
};

#else

// Production mode no-op stub
class FlashWlHandler {
public:
  static FlashWlHandler &getInstance() { static FlashWlHandler i; return i; }
  static void handleFlash(const char *, JsonDocument &) {}
  static void handleWl(const char *, JsonDocument &) {}
  static void registerHandlers() {}
};

#endif

} // namespace ThetaGP::Test
