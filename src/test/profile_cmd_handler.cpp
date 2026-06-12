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

#include "test/profile_cmd_handler.h"
#include "test/dispatcher.h"
#include "test/framelayer.h"

#include "drivers/device/flash/profile_flash.h"
#include "gamepad/config/config_store.h"
#include "drivers/device/flash/flash_w25qxx.h"

#include "utils/log/log.h"

#include "tusb.h"

#include <cstring>

namespace ThetaGP::Test {

using namespace ThetaGP::Drivers::Device;
using namespace ThetaGP::Gamepad::Config;

// Static context for RAW capture callback
static uint16_t s_pendingProfileId = 0;

// ── Helpers ──

static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  return 0;
}

static uint16_t hexDecode(const char *hex, uint8_t *buf, uint16_t maxLen) {
  if (!hex || !buf) return 0;
  size_t hexLen = strlen(hex);
  uint16_t byteLen = (hexLen / 2 < maxLen) ? (hexLen / 2) : maxLen;
  for (uint16_t i = 0; i < byteLen; i++) {
    buf[i] = (hexNibble(hex[i * 2]) << 4) | hexNibble(hex[i * 2 + 1]);
  }
  return byteLen;
}

static void sendError(const char *cmd, JsonDocument &doc,
                      int errorCode, const char *reason) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;
  resp["status"] = "error";
  resp["error_code"] = errorCode;
  resp["reason"] = reason;
  FrameLayer::getInstance().sendResponse(resp);
}

static uint16_t serializeConfigToStaging() {
  JsonDocument doc;
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

  return static_cast<uint16_t>(
      serializeJson(doc, reinterpret_cast<char *>(s_staging), PROFILE_JSON_MAX));
}

// ── findProfileAddress ──

static bool findProfileAddress(uint16_t profileId, uint32_t &outAddr,
                               uint16_t &outLen) {
  if (profileId == 0) {
    outAddr = PROFILE0_ADDR;
    auto &flash = FlashW25qxx::getInstance();
    flash.read(PROFILE0_ADDR, s_staging, PROFILE_JSON_MAX);
    uint16_t len = 0;
    for (uint16_t i = 0; i < PROFILE_JSON_MAX; i++) {
      if (s_staging[i] == 0 || s_staging[i] == 0xFF) break;
      len = i + 1;
    }
    outLen = len;
    return (len > 0);
  }

  auto &flash = FlashW25qxx::getInstance();
  uint16_t bestSeq = 0;
  uint32_t addr = 0;

  for (uint16_t i = 0; i < ADDR_RING_SLOTS; i++) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);
    AddressEntry entry;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&entry),
                    sizeof(AddressEntry))) {
      continue;
    }
    if (entry.profileId == PROFILE_ID_NONE) {
      continue;
    }
    if (entry.profileId == profileId && entry.seq > bestSeq) {
      bestSeq = entry.seq;
      addr = entry.address;
    }
  }

  if (addr == 0) {
    return false;
  }

  outAddr = addr;

  flash.read(addr, s_staging, PROFILE_JSON_MAX);
  uint16_t len = 0;
  for (uint16_t i = 0; i < PROFILE_JSON_MAX; i++) {
    if (s_staging[i] == 0 || s_staging[i] == 0xFF) break;
    len = i + 1;
  }
  outLen = len;
  return true;
}

// ── onStagingDone ──
// Callback invoked by FrameLayer after RAW capture fills s_staging during
// profile.start. Uses s_pendingProfileId to decide writeFactoryProfile (id=0)
// vs createProfile (id=1-15).

static void onStagingDone(const uint8_t *buf, uint16_t len) {
  JsonDocument resp;
  resp["cmd"] = "profile.start";
  resp["queued"] = 0;

  const char *json = reinterpret_cast<const char *>(buf);

  if (s_pendingProfileId == 0) {
    if (!ProfileStore::getInstance().writeFactoryProfile(json, len)) {
      resp["status"] = "error";
      resp["reason"] = "writeFactoryProfile failed";
      FrameLayer::getInstance().sendResponse(resp);
      LOG_ERROR("ProfileCmdHandler: writeFactoryProfile failed");
      return;
    }
    resp["status"] = "ok";
    resp["profile_id"] = 0;
  } else {
    uint16_t newId = 0;
    if (!ProfileStore::getInstance().createProfile(json, len, &newId)) {
      resp["status"] = "error";
      resp["reason"] = "createProfile failed";
      FrameLayer::getInstance().sendResponse(resp);
      LOG_ERROR("ProfileCmdHandler: createProfile failed");
      return;
    }
    resp["status"] = "ok";
    resp["profile_id"] = newId;
  }

  LOG_INFO("ProfileCmdHandler: profile.start done, id=%u", s_pendingProfileId);
  FrameLayer::getInstance().sendResponse(resp);
}

// ── profile.start ──

static void handleProfileStart(const char *cmd, JsonDocument &doc) {
  uint16_t rawLen = doc["len"] | 0;
  if (rawLen == 0 || rawLen > 2048) {
    sendError(cmd, doc, 1, "invalid or missing len (1-2048)");
    return;
  }

  uint16_t profileId = doc["profileId"] | 0;
  if (profileId > 15) {
    sendError(cmd, doc, 1, "profileId out of range (0-15)");
    return;
  }

  s_pendingProfileId = profileId;

  JsonDocument ack;
  ack["cmd"] = cmd;
  ack["queued"] = doc["queued"].as<int>() + 1;
  ack["status"] = "ok";
  ack["len"] = rawLen;
  ack["profileId"] = profileId;
  FrameLayer::getInstance().sendResponse(ack);

  FrameLayer::getInstance().startRawCapture(s_staging, rawLen, onStagingDone);
}

// ── profile.get ──

static void handleProfileGet(const char *cmd, JsonDocument &doc) {
  uint16_t profileId = doc["id"] | 0xFFFF;
  if (profileId > 15) {
    sendError(cmd, doc, 1, "invalid or missing id (0-15)");
    return;
  }

  uint32_t addr = 0;
  uint16_t dataLen = 0;
  if (!findProfileAddress(profileId, addr, dataLen) || dataLen == 0) {
    sendError(cmd, doc, 1, "profile not found or empty");
    return;
  }

  JsonDocument hdr;
  hdr["cmd"] = "profile.start";
  hdr["len"] = dataLen;
  char hdrBuf[128];
  uint16_t hdrLen = serializeJson(hdr, hdrBuf, sizeof(hdrBuf) - 2);
  hdrBuf[hdrLen++] = '\r';
  hdrBuf[hdrLen++] = '\n';
  tud_cdc_write(hdrBuf, hdrLen);
  tud_cdc_write_flush();

  uint16_t sent = 0;
  while (sent < dataLen) {
    uint16_t chunk = (dataLen - sent < 64) ? (dataLen - sent) : 64;
    uint32_t written = tud_cdc_write(s_staging + sent, chunk);
    sent += static_cast<uint16_t>(written);
    if (written < chunk) {
      tud_cdc_write_flush();
    }
  }
  tud_cdc_write_flush();

  JsonDocument trailer;
  trailer["cmd"] = "profile.end";
  trailer["id"] = profileId;
  FrameLayer::getInstance().sendResponse(trailer);
}

// ── profile.list ──

static void handleProfileList(const char *cmd, JsonDocument &doc) {
  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;

  ProfileStatus status = ProfileStore::getInstance().getStatus();
  resp["status"] = "ok";
  resp["active"] = status.activeId;

  auto &flash = FlashW25qxx::getInstance();
  JsonArray profiles = resp["profiles"].to<JsonArray>();

  uint32_t addrMap[16] = {0};
  uint16_t seqMap[16] = {0};

  for (uint16_t i = 0; i < ADDR_RING_SLOTS; i++) {
    uint32_t slotAddr = ADDR_RING_BASE + i * sizeof(AddressEntry);
    AddressEntry entry;
    if (!flash.read(slotAddr, reinterpret_cast<uint8_t *>(&entry),
                    sizeof(AddressEntry))) {
      continue;
    }
    if (entry.profileId == PROFILE_ID_NONE) continue;
    if (entry.profileId > 15) continue;
    if (entry.seq > seqMap[entry.profileId]) {
      seqMap[entry.profileId] = entry.seq;
      addrMap[entry.profileId] = entry.address;
    }
  }

  for (uint16_t id = 0; id <= 15; id++) {
    if (id == 0 || addrMap[id] != 0) {
      JsonObject p = profiles.add<JsonObject>();
      p["id"] = id;
      p["active"] = (id == status.activeId);
    }
  }

  FrameLayer::getInstance().sendResponse(resp);
}

// ── profile.create ──

static void handleProfileCreate(const char *cmd, JsonDocument &doc) {
  const char *dataHex = doc["data_hex"] | "";

  if (strlen(dataHex) == 0) {
    sendError(cmd, doc, 1, "data_hex field required");
    return;
  }

  uint16_t len = hexDecode(dataHex, s_staging, PROFILE_JSON_MAX);
  if (len == 0) {
    sendError(cmd, doc, 1, "no data decoded from data_hex");
    return;
  }

  if (len < PROFILE_JSON_MAX) {
    s_staging[len] = '\0';
  }

  uint16_t newId = 0;
  if (!ProfileStore::getInstance().createProfile(
          reinterpret_cast<const char *>(s_staging), len, &newId)) {
    sendError(cmd, doc, 1, "createProfile failed");
    return;
  }

  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;
  resp["status"] = "ok";
  resp["profile_id"] = newId;
  resp["len"] = len;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── profile.delete ──

static void handleProfileDelete(const char *cmd, JsonDocument &doc) {
  uint16_t profileId = doc["id"] | 0xFFFF;
  if (profileId == 0 || profileId > 15) {
    sendError(cmd, doc, 1, "invalid id (1-15), cannot delete Profile0");
    return;
  }

  if (!ProfileStore::getInstance().deleteProfile(profileId)) {
    sendError(cmd, doc, 1, "deleteProfile failed (profile may not exist)");
    return;
  }

  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;
  resp["status"] = "ok";
  resp["id"] = profileId;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── profile.select ──

static void handleProfileSelect(const char *cmd, JsonDocument &doc) {
  uint16_t profileId = doc["id"] | 0xFFFF;
  if (profileId > 15) {
    sendError(cmd, doc, 1, "invalid or missing id (0-15)");
    return;
  }

  if (!ProfileStore::getInstance().selectProfile(profileId)) {
    sendError(cmd, doc, 1, "selectProfile failed");
    return;
  }

  uint16_t dataLen = 0;
  ProfileStore::getInstance().loadActive(nullptr, &dataLen);

  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;
  resp["status"] = "ok";
  resp["id"] = profileId;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── profile.status ──

static void handleProfileStatus(const char *cmd, JsonDocument &doc) {
  ProfileStatus status = ProfileStore::getInstance().getStatus();

  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;
  resp["status"] = "ok";
  resp["active_profile_id"] = status.activeId;
  resp["profile_count"] = status.profileCount;
  resp["next_addr"] = status.nextAddr;
  resp["boot_meta_seq"] = status.bootMetaSeq;
  resp["address_ring_seq"] = status.addressRingSeq;
  resp["total_sectors"] = status.totalSectors;
  resp["used_sectors"] = status.usedSectors;
  resp["free_sectors"] = status.freeSectors;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── profile.save ──

static void handleProfileSave(const char *cmd, JsonDocument &doc) {
  ProfileStatus status = ProfileStore::getInstance().getStatus();
  uint16_t activeId = status.activeId;

  if (activeId == 0) {
    sendError(cmd, doc, 1, "cannot save to factory Profile0 (read-only)");
    return;
  }

  uint16_t jsonLen = serializeConfigToStaging();
  if (jsonLen == 0) {
    sendError(cmd, doc, 1, "serializeConfig failed");
    return;
  }

  if (!ProfileStore::getInstance().modifyProfile(
          activeId, reinterpret_cast<const char *>(s_staging), jsonLen)) {
    sendError(cmd, doc, 1, "modifyProfile failed");
    return;
  }

  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;
  resp["status"] = "ok";
  resp["id"] = activeId;
  resp["len"] = jsonLen;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── profile.load ──

static void handleProfileLoad(const char *cmd, JsonDocument &doc) {
  uint16_t profileId = doc["id"] | 0xFFFF;

  ProfileStore &store = ProfileStore::getInstance();
  ProfileStatus status = store.getStatus();

  if (profileId > 15) {
    profileId = status.activeId;
  }

  if (profileId != status.activeId) {
    if (!store.selectProfile(profileId)) {
      sendError(cmd, doc, 1, "selectProfile failed for requested id");
      return;
    }
  }

  uint16_t dataLen = 0;
  if (!store.loadActive(nullptr, &dataLen)) {
    sendError(cmd, doc, 1, "loadActive failed");
    return;
  }

  JsonDocument resp;
  resp["cmd"] = cmd;
  resp["queued"] = doc["queued"].as<int>() + 1;
  resp["status"] = "ok";
  resp["id"] = profileId;
  resp["len"] = dataLen;
  FrameLayer::getInstance().sendResponse(resp);
}

// ── Domain Dispatch ──

void ProfileCmdHandler::handleProfile(const char *cmd, JsonDocument &doc) {
  LOG_DEBUG("ProfileCmdHandler: cmd='%s'", cmd);

  if (strcmp(cmd, "profile.start") == 0) {
    handleProfileStart(cmd, doc);
  } else if (strcmp(cmd, "profile.get") == 0) {
    handleProfileGet(cmd, doc);
  } else if (strcmp(cmd, "profile.list") == 0) {
    handleProfileList(cmd, doc);
  } else if (strcmp(cmd, "profile.create") == 0) {
    handleProfileCreate(cmd, doc);
  } else if (strcmp(cmd, "profile.delete") == 0) {
    handleProfileDelete(cmd, doc);
  } else if (strcmp(cmd, "profile.select") == 0) {
    handleProfileSelect(cmd, doc);
  } else if (strcmp(cmd, "profile.status") == 0) {
    handleProfileStatus(cmd, doc);
  } else if (strcmp(cmd, "profile.save") == 0) {
    handleProfileSave(cmd, doc);
  } else if (strcmp(cmd, "profile.load") == 0) {
    handleProfileLoad(cmd, doc);
  } else {
    JsonDocument resp;
    resp["cmd"] = cmd;
    resp["queued"] = doc["queued"].as<int>() + 1;
    resp["status"] = "error";
    resp["error_code"] = static_cast<int>(ThetaGP::Result::Unsupported);
    resp["reason"] = "unknown profile command";
    FrameLayer::getInstance().sendResponse(resp);
  }
}

// ── Registration ──

ProfileCmdHandler &ProfileCmdHandler::getInstance() {
  static ProfileCmdHandler instance;
  return instance;
}

void ProfileCmdHandler::registerHandlers() {
  Dispatcher::getInstance().registerHandler("profile", handleProfile);
  LOG_INFO("ProfileCmdHandler registered: profile. domain");
}

} // namespace ThetaGP::Test
