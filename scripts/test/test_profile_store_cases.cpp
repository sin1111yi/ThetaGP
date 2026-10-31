#include "drivers/device/flash/flash_base.h"
#include "gamepad/profile/profile_store.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace ThetaGP::Gamepad::Profile;
namespace {
std::array<uint8_t, 8 * PROFILE_SECTOR_SIZE> bytes;
bool failErase = false;
bool failRead = false;
unsigned checks = 0;

void check(bool ok) {
  assert(ok);
  ++checks;
}

void reset() {
  bytes.fill(0xFF);
  failErase = false;
  failRead = false;
  check(ProfileStore::getInstance().init());
  check(ProfileStore::getInstance().writeFactoryProfile("factory", 7));
}

void expectBody(uint16_t id, const char *expected) {
  ProfileText body;
  check(ProfileStore::getInstance().readProfile(id, &body));
  check(body.len == std::strlen(expected));
  check(std::memcmp(body.data, expected, body.len) == 0);
}
}

namespace ThetaGP::Drivers::Device {
FlashBase &FlashBase::getInstance() {
  static FlashBase flash;
  return flash;
}
const FlashInfo &FlashBase::getInfo() const {
  static const FlashInfo info{bytes.size()};
  return info;
}
bool FlashBase::read(uint32_t address, uint8_t *data, uint32_t length) {
  if (failRead) return false;
  if (address > bytes.size() || length > bytes.size() - address) return false;
  std::memcpy(data, bytes.data() + address, length);
  return true;
}
bool FlashBase::write(uint32_t address, const uint8_t *data, uint32_t length) {
  if (address > bytes.size() || length > bytes.size() - address) return false;
  for (uint32_t i = 0; i < length; ++i) {
    if ((bytes[address + i] & data[i]) != data[i]) return false;
  }
  for (uint32_t i = 0; i < length; ++i) bytes[address + i] &= data[i];
  return true;
}
bool FlashBase::eraseSector(uint32_t address) {
  if (failErase || address >= bytes.size()) return false;
  address &= ~(PROFILE_SECTOR_SIZE - 1);
  std::fill_n(bytes.begin() + address, PROFILE_SECTOR_SIZE, 0xFF);
  return true;
}
}

int main() {
  auto &store = ProfileStore::getInstance();
  reset();
  uint16_t id = 0;
  check(store.createProfile("user", 4, &id));
  const auto before = store.getStatus();
  const auto metadata = bytes;
  check(store.replaceFactoryProfile("new-factory", 11));
  check(store.getStatus().activeId == id);
  check(store.getStatus().bootMetaSeq == before.bootMetaSeq);
  check(std::equal(bytes.begin(), bytes.begin() + PROFILE_SECTOR_SIZE,
                   metadata.begin()));
  check(store.init());
  check(store.getStatus().activeId == id);
  expectBody(0, "new-factory");
  expectBody(PROFILE_ID_ACTIVE, "user");

  // Metadata wrap preserves the caller's input and the newest selection.
  for (unsigned i = 0; i < 300; ++i) check(store.selectProfile(i % 2 ? id : 0));
  check(store.selectProfile(0));
  check(store.init());
  check(store.getStatus().activeId == 0);
  check(store.selectProfile(id));
  check(store.init());
  check(store.getStatus().activeId == id);

  // Each save uses the same staging buffer as the firmware's upload path.
  for (unsigned i = 0; i < 550; ++i) {
    char expected[32];
    std::snprintf(expected, sizeof(expected), "new-body-%u", i);
    std::strcpy(reinterpret_cast<char *>(s_staging), expected);
    check(store.modifyProfile(id, reinterpret_cast<char *>(s_staging),
                              std::strlen(expected)));
    expectBody(id, expected);
    check(store.init());
    expectBody(PROFILE_ID_ACTIVE, expected);
  }

  // Creating a new profile also retains its staged body across compaction.
  while (store.getStatus().nextAddr < bytes.size())
    check(store.modifyProfile(id, "old", 3));
  std::strcpy(reinterpret_cast<char *>(s_staging), "second-user");
  uint16_t second = 0;
  check(store.createProfile(reinterpret_cast<char *>(s_staging), 11, &second));
  expectBody(second, "second-user");
  check(store.init());
  expectBody(PROFILE_ID_ACTIVE, "second-user");
  check(store.selectProfile(0));
  check(store.compaction());
  check(store.init());
  check(store.getStatus().activeId == 0);
  expectBody(id, "old");
  expectBody(second, "second-user");

  // A full metadata ring does not overwrite an upload awaiting its address entry.
  reset();
  for (unsigned i = 1; i < BOOTMETA_SLOTS; ++i) check(store.selectProfile(0));
  std::strcpy(reinterpret_cast<char *>(s_staging), "staged-user");
  check(store.createProfile(reinterpret_cast<char *>(s_staging), 11, &id));
  expectBody(id, "staged-user");
  check(store.init());
  expectBody(PROFILE_ID_ACTIVE, "staged-user");

  // The largest accepted body survives a move without truncation.
  std::memset(s_staging, 'x', PROFILE_JSON_MAX);
  check(store.modifyProfile(id, reinterpret_cast<char *>(s_staging), PROFILE_JSON_MAX));
  check(store.compaction());
  check(store.init());
  ProfileText large;
  check(store.readProfile(id, &large));
  check(large.len == PROFILE_JSON_MAX);
  check(std::all_of(large.data, large.data + large.len, [](char c) { return c == 'x'; }));

  // A failed ring erase refuses the operation without writing past the ring.
  reset();
  for (unsigned i = 1; i < BOOTMETA_SLOTS; ++i) check(store.selectProfile(0));
  const auto full = bytes;
  failErase = true;
  check(!store.selectProfile(0));
  check(bytes == full);
  failErase = false;
  check(store.selectProfile(0));
  const auto readable = bytes;
  failRead = true;
  check(!store.selectProfile(0));
  check(bytes == readable);
  failRead = false;

  // A factory body whose record was lost is recorded again by the repair: the
  // body is readable through the fixed address either way, but the store has to
  // report it as a profile.
  reset();
  std::fill_n(bytes.begin(), PROFILE_SECTOR_SIZE, 0xFF);
  check(store.init());
  check(store.getStatus().profileCount == 0);
  check(store.replaceFactoryProfile("repaired", 8));
  check(store.getStatus().profileCount == 1);
  check(store.init());
  check(store.getStatus().profileCount == 1);
  check(store.getStatus().activeId == 0);
  expectBody(0, "repaired");

  std::printf("%u checks, 0 failed\n", checks);
}
