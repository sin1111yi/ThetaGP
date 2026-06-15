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

#include "gamepad/config/configmgr.h"
#include "gamepad/config/config_store.h"
#include "gamepad/profile/profile_store.h"

#include "utils/log/log.h"

#include "utils/json/json.h"

namespace ThetaGP::Gamepad::Config {

using Profile::PROFILE_JSON_MAX;
using Profile::ProfileStore;
using Profile::s_staging;

ConfigManager &ConfigManager::getInstance() {
  static ConfigManager instance;
  return instance;
}

bool ConfigManager::init() {
  if (!ProfileStore::getInstance().init()) {
    LOG_ERROR("ConfigManager: ProfileStore init failed");
    return false;
  }

  Profile::ProfileStatus status = ProfileStore::getInstance().getStatus();
  _activeId = status.activeId;

  uint16_t dataLen = 0;
  if (!ProfileStore::getInstance().loadActive(nullptr, &dataLen)) {
    LOG_WARN("ConfigManager: loadActive failed, using defaults");
    return false;
  }

  // Parse raw JSON from s_staging into _config
  if (dataLen > 0) {
    s_staging[dataLen] = '\0';
    parseProfile(reinterpret_cast<const char *>(s_staging), &_config);
  }

  LOG_INFO("ConfigManager: init OK, active=%u count=%u", _activeId,
           status.profileCount);
  return true;
}

bool ConfigManager::loadProfile(uint16_t profileId) {
  ProfileStore &store = ProfileStore::getInstance();
  if (profileId != _activeId) {
    if (!store.selectProfile(profileId))
      return false;
    _activeId = profileId;
  }
  uint16_t dataLen = 0;
  bool ok = store.loadActive(nullptr, &dataLen);
  if (ok && dataLen > 0) {
    s_staging[dataLen] = '\0';
    parseProfile(reinterpret_cast<const char *>(s_staging), &_config);
  }
  LOG_INFO("ConfigManager: load id=%u %s", profileId, ok ? "OK" : "FAIL");
  return ok;
}

bool ConfigManager::saveProfile() {
  ProfileStore &store = ProfileStore::getInstance();
  if (_activeId == 0) {
    LOG_WARN("ConfigManager: cannot save to factory Profile0");
    return false;
  }

  // Build JSON using frozen printf
  Json doc;
  doc.beginWrite(reinterpret_cast<char *>(s_staging), PROFILE_JSON_MAX);

  // ── map ──
  doc.printf("{ver:1,map:{socd:%d,four_way:%d,dpad:%d,"
             "inv_x:%B,inv_y:%B,inv_rx:%B,inv_ry:%B,swap:%B,btn_map:[",
             _config.socd_mode, _config.four_way_mode, _config.dpad_mode,
             _config.inv_x, _config.inv_y, _config.inv_rx, _config.inv_ry,
             _config.swap_sticks);
  for (uint8_t i = 0; i < 32; i++) {
    if (i > 0) doc.printf(",");
    doc.printf("%d", _config.btn_map[i]);
  }
  doc.printf("]");

  // ── stick ──
  doc.printf(",stick:{lx_dz:%d,ly_dz:%d,rx_dz:%d,ry_dz:%d,"
             "lx_sens:%d,ly_sens:%d,rx_sens:%d,ry_sens:%d,curve:%d,ema:%d}",
             _config.lx_dz, _config.ly_dz, _config.rx_dz, _config.ry_dz,
             _config.lx_sens, _config.ly_sens, _config.rx_sens, _config.ry_sens,
             _config.curve, _config.ema);

  // ── trig ──
  doc.printf(",trig:{lt_dz:%d,rt_dz:%d}", _config.lt_dz, _config.rt_dz);

  // ── usb ──
  doc.printf(",usb:{poll:%d}", _config.poll_rate);

  // ── led ──
  doc.printf(",led:{bri:%d,mode:%d,hue:%d,sat:%d,spd:%d}",
             _config.led_brightness, _config.led_mode,
             _config.led_hue, _config.led_saturation, _config.led_speed);

  // ── sys ──
  doc.printf(",sys:{log:%d,deb_samp:%d,deb_thr:%d}",
             _config.log_level, _config.debounce_samples,
             _config.debounce_threshold);

  // ── cal ──
  doc.printf(",cal:{lx_c:%d,ly_c:%d,rx_c:%d,ry_c:%d}",
             _config.cal_lx, _config.cal_ly, _config.cal_rx, _config.cal_ry);

  doc.printf("}");  // close root

  uint16_t jsonLen = static_cast<uint16_t>(doc.end());

  if (!store.modifyProfile(_activeId, reinterpret_cast<const char *>(s_staging),
                           jsonLen)) {
    LOG_ERROR("ConfigManager: save failed id=%u", _activeId);
    return false;
  }

  LOG_INFO("ConfigManager: save OK id=%u, len=%u", _activeId, jsonLen);
  return true;
}

uint16_t ConfigManager::activeProfileId() const { return _activeId; }

uint8_t ConfigManager::profileCount() const {
  return ProfileStore::getInstance().getStatus().profileCount;
}

bool ConfigManager::selectProfile(uint16_t pid) { return loadProfile(pid); }

Profile::ProfileStatus ConfigManager::getStatus() const {
  return ProfileStore::getInstance().getStatus();
}

} // namespace ThetaGP::Gamepad::Config
