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

#include "gamepad/config/config_store.h"

#include "utils/log/log.h"

namespace ThetaGP::Gamepad::Config {

COMMON_ZERO_INIT ConfigStore s_config;

void parseProfile(const char *json, ConfigStore *cfg) {
  if (!json || !cfg) {
    LOG_ERROR("parseProfile: null args");
    return;
  }

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    LOG_ERROR("parseProfile: JSON parse error: %s", err.c_str());
    return;
  }

  // ── map ──
  JsonObject map = doc["map"];
  if (!map.isNull()) {
    cfg->socd_mode     = map["socd"]    | cfg->socd_mode;
    cfg->four_way_mode = map["four_way"]| cfg->four_way_mode;
    cfg->dpad_mode     = map["dpad"]    | cfg->dpad_mode;
    cfg->inv_x         = map["inv_x"]   | cfg->inv_x;
    cfg->inv_y         = map["inv_y"]   | cfg->inv_y;
    cfg->inv_rx        = map["inv_rx"]  | cfg->inv_rx;
    cfg->inv_ry        = map["inv_ry"]  | cfg->inv_ry;
    cfg->swap_sticks   = map["swap"]    | cfg->swap_sticks;

    JsonArray btnArr = map["btn_map"];
    if (!btnArr.isNull()) {
      uint8_t idx = 0;
      for (JsonVariant v : btnArr) {
        if (idx >= 32) break;
        cfg->btn_map[idx] = v.as<uint16_t>();
        idx++;
      }
      while (idx < 32) {
        cfg->btn_map[idx++] = 0xFFFF;
      }
    }
  }

  // ── stick ──
  JsonObject stick = doc["stick"];
  if (!stick.isNull()) {
    cfg->lx_dz   = stick["lx_dz"]   | cfg->lx_dz;
    cfg->ly_dz   = stick["ly_dz"]   | cfg->ly_dz;
    cfg->rx_dz   = stick["rx_dz"]   | cfg->rx_dz;
    cfg->ry_dz   = stick["ry_dz"]   | cfg->ry_dz;
    cfg->lx_sens = stick["lx_sens"] | cfg->lx_sens;
    cfg->ly_sens = stick["ly_sens"] | cfg->ly_sens;
    cfg->rx_sens = stick["rx_sens"] | cfg->rx_sens;
    cfg->ry_sens = stick["ry_sens"] | cfg->ry_sens;
    cfg->curve   = stick["curve"]   | cfg->curve;
    cfg->ema     = stick["ema"]     | cfg->ema;
  }

  // ── trig ──
  JsonObject trig = doc["trig"];
  if (!trig.isNull()) {
    cfg->lt_dz = trig["lt_dz"] | cfg->lt_dz;
    cfg->rt_dz = trig["rt_dz"] | cfg->rt_dz;
  }

  // ── usb ──
  JsonObject usb = doc["usb"];
  if (!usb.isNull()) {
    cfg->poll_rate = usb["poll"] | cfg->poll_rate;
  }

  // ── led ──
  JsonObject led = doc["led"];
  if (!led.isNull()) {
    cfg->led_brightness = led["bri"]  | cfg->led_brightness;
    cfg->led_mode       = led["mode"] | cfg->led_mode;
    cfg->led_hue        = led["hue"]  | cfg->led_hue;
    cfg->led_saturation = led["sat"]  | cfg->led_saturation;
    cfg->led_speed      = led["spd"]  | cfg->led_speed;
  }

  // ── sys ──
  JsonObject sys = doc["sys"];
  if (!sys.isNull()) {
    cfg->log_level          = sys["log"]       | cfg->log_level;
    cfg->debounce_samples   = sys["deb_samp"]  | cfg->debounce_samples;
    cfg->debounce_threshold = sys["deb_thr"]   | cfg->debounce_threshold;
  }

  // ── cal ──
  JsonObject cal = doc["cal"];
  if (!cal.isNull()) {
    cfg->cal_lx = cal["lx_c"] | cfg->cal_lx;
    cfg->cal_ly = cal["ly_c"] | cfg->cal_ly;
    cfg->cal_rx = cal["rx_c"] | cfg->cal_rx;
    cfg->cal_ry = cal["ry_c"] | cfg->cal_ry;
  }

  LOG_DEBUG("parseProfile: done");
}

} // namespace ThetaGP::Gamepad::Config
