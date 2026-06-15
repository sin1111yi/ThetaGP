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

#include "utils/json/json.h"

namespace ThetaGP::Gamepad::Config {

void parseProfile(const char *json, ConfigStore *cfg) {
  if (!json || !cfg) {
    LOG_ERROR("parseProfile: null args");
    return;
  }

  Json doc;
  doc.parse(json);

  // ── map ──
  cfg->socd_mode     = doc.getInt("map.socd",     cfg->socd_mode);
  cfg->four_way_mode = doc.getInt("map.four_way", cfg->four_way_mode);
  cfg->dpad_mode     = doc.getInt("map.dpad",     cfg->dpad_mode);
  cfg->inv_x         = doc.getInt("map.inv_x",    cfg->inv_x);
  cfg->inv_y         = doc.getInt("map.inv_y",    cfg->inv_y);
  cfg->inv_rx        = doc.getInt("map.inv_rx",   cfg->inv_rx);
  cfg->inv_ry        = doc.getInt("map.inv_ry",   cfg->inv_ry);
  cfg->swap_sticks   = doc.getInt("map.swap",     cfg->swap_sticks);

  // btn_map array
  int arrLen = doc.getArrLen("map.btn_map");
  if (arrLen > 0) {
    uint8_t idx = 0;
    for (int i = 0; i < arrLen && idx < 32; i++) {
      char path[32];
      snprintf(path, sizeof(path), "map.btn_map.%d", i);
      cfg->btn_map[idx] = static_cast<uint16_t>(doc.getInt(path));
      idx++;
    }
    while (idx < 32) {
      cfg->btn_map[idx++] = 0xFFFF;
    }
  }

  // ── stick ──
  cfg->lx_dz   = doc.getInt("stick.lx_dz",   cfg->lx_dz);
  cfg->ly_dz   = doc.getInt("stick.ly_dz",   cfg->ly_dz);
  cfg->rx_dz   = doc.getInt("stick.rx_dz",   cfg->rx_dz);
  cfg->ry_dz   = doc.getInt("stick.ry_dz",   cfg->ry_dz);
  cfg->lx_sens = doc.getInt("stick.lx_sens", cfg->lx_sens);
  cfg->ly_sens = doc.getInt("stick.ly_sens", cfg->ly_sens);
  cfg->rx_sens = doc.getInt("stick.rx_sens", cfg->rx_sens);
  cfg->ry_sens = doc.getInt("stick.ry_sens", cfg->ry_sens);
  cfg->curve   = doc.getInt("stick.curve",   cfg->curve);
  cfg->ema     = doc.getInt("stick.ema",     cfg->ema);

  // ── trig ──
  cfg->lt_dz = doc.getInt("trig.lt_dz", cfg->lt_dz);
  cfg->rt_dz = doc.getInt("trig.rt_dz", cfg->rt_dz);

  // ── usb ──
  cfg->poll_rate = doc.getInt("usb.poll", cfg->poll_rate);

  // ── led ──
  cfg->led_brightness = doc.getInt("led.bri",  cfg->led_brightness);
  cfg->led_mode       = doc.getInt("led.mode", cfg->led_mode);
  cfg->led_hue        = doc.getInt("led.hue",  cfg->led_hue);
  cfg->led_saturation = doc.getInt("led.sat",  cfg->led_saturation);
  cfg->led_speed      = doc.getInt("led.spd",  cfg->led_speed);

  // ── sys ──
  cfg->log_level          = doc.getInt("sys.log",       cfg->log_level);
  cfg->debounce_samples   = doc.getInt("sys.deb_samp",  cfg->debounce_samples);
  cfg->debounce_threshold = doc.getInt("sys.deb_thr",   cfg->debounce_threshold);

  // ── cal ──
  cfg->cal_lx = doc.getInt("cal.lx_c", cfg->cal_lx);
  cfg->cal_ly = doc.getInt("cal.ly_c", cfg->cal_ly);
  cfg->cal_rx = doc.getInt("cal.rx_c", cfg->cal_rx);
  cfg->cal_ry = doc.getInt("cal.ry_c", cfg->cal_ry);

  LOG_DEBUG("parseProfile: done");
}

} // namespace ThetaGP::Gamepad::Config
