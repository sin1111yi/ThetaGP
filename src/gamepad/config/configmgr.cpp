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
#include "drivers/device/flash/profile_flash.h"
#include "gamepad/config/config_store.h"
#include "utils/log/log.h"

namespace ThetaGP::Gamepad::Config {

using Drivers::Device::ProfileStore;
using Drivers::Device::s_staging;
using Drivers::Device::PROFILE_JSON_MAX;

ConfigManager &ConfigManager::getInstance() {
  static ConfigManager instance;
  return instance;
}

bool ConfigManager::init() {
  if (!ProfileStore::getInstance().init()) {
    LOG_ERROR("ConfigManager: ProfileStore init failed");
    return false;
  }

  Drivers::Device::ProfileStatus status = ProfileStore::getInstance().getStatus();
  _activeId = status.activeId;

  uint16_t dataLen = 0;
  if (!ProfileStore::getInstance().loadActive(nullptr, &dataLen)) {
    LOG_WARN("ConfigManager: loadActive failed, using defaults");
    return false;
  }

  LOG_INFO("ConfigManager: init OK, active=%u count=%u", _activeId, status.profileCount);
  return true;
}

bool ConfigManager::loadProfile(uint16_t profileId) {
  ProfileStore &store = ProfileStore::getInstance();
  if (profileId != _activeId) {
    if (!store.selectProfile(profileId)) return false;
    _activeId = profileId;
  }
  uint16_t dataLen = 0;
  bool ok = store.loadActive(nullptr, &dataLen);
  LOG_INFO("ConfigManager: load id=%u %s", profileId, ok ? "OK" : "FAIL");
  return ok;
}

bool ConfigManager::saveProfile() {
  ProfileStore &store = ProfileStore::getInstance();
  if (_activeId == 0) {
    LOG_WARN("ConfigManager: cannot save to factory Profile0");
    return false;
  }

  StaticJsonDocument<2048> doc;

  JsonObject map = doc["map"].to<JsonObject>();
  map["socd"]     = s_config.socd_mode;
  map["four_way"] = s_config.four_way_mode;
  map["dpad"]     = s_config.dpad_mode;
  map["inv_x"]    = s_config.inv_x;
  map["inv_y"]    = s_config.inv_y;
  map["inv_rx"]   = s_config.inv_rx;
  map["inv_ry"]   = s_config.inv_ry;
  map["swap"]     = s_config.swap_sticks;

  JsonArray btnArr = map["btn_map"].to<JsonArray>();
  for (uint8_t i = 0; i < 32; i++) {
    btnArr.add(s_config.btn_map[i]);
  }

  JsonObject stick = doc["stick"].to<JsonObject>();
  stick["lx_dz"]   = s_config.lx_dz;
  stick["ly_dz"]   = s_config.ly_dz;
  stick["rx_dz"]   = s_config.rx_dz;
  stick["ry_dz"]   = s_config.ry_dz;
  stick["lx_sens"] = s_config.lx_sens;
  stick["ly_sens"] = s_config.ly_sens;
  stick["rx_sens"] = s_config.rx_sens;
  stick["ry_sens"] = s_config.ry_sens;
  stick["curve"]   = s_config.curve;
  stick["ema"]     = s_config.ema;

  JsonObject trig = doc["trig"].to<JsonObject>();
  trig["lt_dz"] = s_config.lt_dz;
  trig["rt_dz"] = s_config.rt_dz;

  JsonObject usb = doc["usb"].to<JsonObject>();
  usb["poll"] = s_config.poll_rate;

  JsonObject led = doc["led"].to<JsonObject>();
  led["bri"]  = s_config.led_brightness;
  led["mode"] = s_config.led_mode;
  led["hue"]  = s_config.led_hue;
  led["sat"]  = s_config.led_saturation;
  led["spd"]  = s_config.led_speed;

  JsonObject sys = doc["sys"].to<JsonObject>();
  sys["log"]      = s_config.log_level;
  sys["deb_samp"] = s_config.debounce_samples;
  sys["deb_thr"]  = s_config.debounce_threshold;

  JsonObject cal = doc["cal"].to<JsonObject>();
  cal["lx_c"] = s_config.cal_lx;
  cal["ly_c"] = s_config.cal_ly;
  cal["rx_c"] = s_config.cal_rx;
  cal["ry_c"] = s_config.cal_ry;

  uint16_t jsonLen = serializeJson(doc, reinterpret_cast<char *>(s_staging), PROFILE_JSON_MAX);

  if (!store.modifyProfile(_activeId, reinterpret_cast<const char *>(s_staging), jsonLen)) {
    LOG_ERROR("ConfigManager: save failed id=%u", _activeId);
    return false;
  }

  LOG_INFO("ConfigManager: save OK id=%u, len=%u", _activeId, jsonLen);
  return true;
}

uint16_t ConfigManager::activeProfileId() const {
  return _activeId;
}

uint8_t ConfigManager::profileCount() const {
  return ProfileStore::getInstance().getStatus().profileCount;
}

ConfigStore &ConfigManager::config() {
  return s_config;
}

bool ConfigManager::selectProfile(uint16_t pid) {
  return loadProfile(pid);
}

Drivers::Device::ProfileStatus ConfigManager::getStatus() const {
  return ProfileStore::getInstance().getStatus();
}

} // namespace ThetaGP::Gamepad::Config
