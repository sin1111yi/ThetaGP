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

#include "drivers/gp_emulator/ps4/ps4_auth.h"

#include "build_info.h"
#include "conf/ThetaGP_Config.h"
#include "utils/crc.h"

#include "mbedtls/bignum.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"

#if THETAGP_CFG_PS4_TEST_KEY
#include "drivers/gp_emulator/ps4/ps4_test_key.h"
#endif

#include <cstring>

namespace ThetaGP::Drivers::GPEmulator {

namespace {

constexpr uint8_t kChallengeReport = 0xF0; // the host writes the challenge
constexpr uint8_t kAnswerReport = 0xF1;    // the answer is read back in chunks
constexpr uint8_t kStateReport = 0xF2;     // whether the answer is ready
constexpr uint8_t kResetReport = 0xF3;

constexpr uint16_t kChallengeSize = 256;
constexpr uint16_t kPageSize = 56;
constexpr uint16_t kLastPageSize = 32;
constexpr uint16_t kFrameSize = 63;    // the payload of one feature report
constexpr uint16_t kAnswerSize = 1064; // 19 chunks of 56 bytes
constexpr uint16_t kAnswerChunks = 19;
constexpr uint16_t kSignatureSize = 256;
constexpr uint16_t kSerialSize = 16;
constexpr uint16_t kKeySize = 256;

// ── RSA signing ──
// The key's halves are imported into the RSA context, which completes the
// private exponent and the CRT parameters from them: nothing else of the key is
// carried.

// The salt a PSS signature carries keeps two signatures of one digest apart; it
// is not a secret, so the sequence only has to differ from the previous one. It
// starts from the chip's own id.
uint32_t s_saltState = 0;

int signerRandom(void *context, unsigned char *output, size_t length) {
  (void)context;
  if (s_saltState == 0) {
    s_saltState = DEVICE_UID_WORD(0) | 1u;
  }
  for (size_t i = 0; i < length; ++i) {
    s_saltState ^= s_saltState << 13;
    s_saltState ^= s_saltState >> 17;
    s_saltState ^= s_saltState << 5;
    output[i] = static_cast<unsigned char>(s_saltState >> 24);
  }
  return 0;
}

class RsaSigner {
public:
  RsaSigner() = default;
  ~RsaSigner() {
    if (_loaded) {
      mbedtls_rsa_free(&_context);
    }
  }
  RsaSigner(RsaSigner const &) = delete;
  RsaSigner &operator=(RsaSigner const &) = delete;

  bool load(const Ps4AuthKey &key) {
    if (key.modulus == nullptr || key.exponent == nullptr ||
        key.prime_p == nullptr || key.prime_q == nullptr) {
      return false;
    }
    mbedtls_rsa_init(&_context);

    mbedtls_mpi modulus;
    mbedtls_mpi exponent;
    mbedtls_mpi primeP;
    mbedtls_mpi primeQ;
    mbedtls_mpi_init(&modulus);
    mbedtls_mpi_init(&exponent);
    mbedtls_mpi_init(&primeP);
    mbedtls_mpi_init(&primeQ);

    const bool loaded =
        mbedtls_mpi_read_binary(&modulus, key.modulus, kKeySize) == 0 &&
        mbedtls_mpi_read_binary(&exponent, key.exponent, kKeySize) == 0 &&
        mbedtls_mpi_read_binary(&primeP, key.prime_p, kKeySize / 2) == 0 &&
        mbedtls_mpi_read_binary(&primeQ, key.prime_q, kKeySize / 2) == 0 &&
        mbedtls_rsa_import(&_context, &modulus, &primeP, &primeQ, nullptr,
                           &exponent) == 0 &&
        mbedtls_rsa_complete(&_context) == 0 &&
        mbedtls_rsa_set_padding(&_context, MBEDTLS_RSA_PKCS_V21,
                                MBEDTLS_MD_SHA256) == 0;

    mbedtls_mpi_free(&modulus);
    mbedtls_mpi_free(&exponent);
    mbedtls_mpi_free(&primeP);
    mbedtls_mpi_free(&primeQ);

    if (!loaded) {
      mbedtls_rsa_free(&_context);
      return false;
    }
    _loaded = true;
    return true;
  }

  [[nodiscard]] bool isLoaded() const { return _loaded; }

  bool signPssSha256(const uint8_t digest[32], uint8_t signature[256]) {
    if (!_loaded) {
      return false;
    }
    return mbedtls_rsa_rsassa_pss_sign(&_context, signerRandom, nullptr,
                                       MBEDTLS_MD_SHA256, 32, digest,
                                       signature) == 0;
  }

  bool exportKey(uint8_t modulus[256], uint8_t exponent[256]) {
    if (!_loaded) {
      return false;
    }
    return mbedtls_rsa_export_raw(&_context, modulus, kKeySize, nullptr, 0,
                                  nullptr, 0, nullptr, 0, exponent,
                                  kKeySize) == 0;
  }

private:
  mbedtls_rsa_context _context{};
  bool _loaded = false;
};

// ── The channel's state ──

// The answer leaves through a feature report read, which is not a DMA path: it
// is copied into the report the controller sends.
COMMON_ZERO_INIT static uint8_t s_answer[kAnswerSize];
static uint8_t s_challenge[kChallengeSize];
static RsaSigner s_signer;
static uint8_t s_nonceId;
static uint8_t s_chunk;
static bool s_signOwed;
static bool s_signed;

bool buildAnswer() {
  const Ps4AuthKey *key = ps4AuthKey();
  if (key == nullptr) {
    return false;
  }

  uint8_t digest[32];
  if (mbedtls_sha256(s_challenge, sizeof(s_challenge), digest, 0) != 0) {
    return false;
  }
  if (!s_signer.signPssSha256(digest, s_answer)) {
    return false;
  }

  constexpr uint16_t kModulusAt = kSignatureSize + kSerialSize;
  constexpr uint16_t kExponentAt = kModulusAt + kKeySize;
  constexpr uint16_t kCertificateAt = kExponentAt + kKeySize;
  constexpr uint16_t kPaddingAt = kCertificateAt + kKeySize;

  std::memcpy(&s_answer[kSignatureSize], key->serial, kSerialSize);
  if (!s_signer.exportKey(&s_answer[kModulusAt], &s_answer[kExponentAt])) {
    return false;
  }
  std::memcpy(&s_answer[kCertificateAt], key->certificate, kKeySize);
  std::memset(&s_answer[kPaddingAt], 0, kAnswerSize - kPaddingAt);
  return true;
}

uint16_t copyOut(const uint8_t *payload, uint16_t size, uint8_t *buffer,
                 uint16_t reqlen) {
  if (size > reqlen) {
    size = reqlen;
  }
  std::memcpy(buffer, payload, size);
  return size;
}

} // namespace

#if THETAGP_CFG_PS4_TEST_KEY
const Ps4AuthKey *ps4AuthKey() { return &kPs4TestKey; }
#else
const Ps4AuthKey *ps4AuthKey() { return nullptr; }
#endif

void Ps4Auth::initialize() {
  const Ps4AuthKey *key = ps4AuthKey();
  if (key != nullptr) {
    (void)s_signer.load(*key);
  }
  s_nonceId = 0;
  s_chunk = 0;
  s_signOwed = false;
  s_signed = false;
  std::memset(s_challenge, 0, sizeof(s_challenge));
  std::memset(s_answer, 0, sizeof(s_answer));
}

bool Ps4Auth::isAvailable() const { return s_signer.isLoaded(); }

void Ps4Auth::process() {
  if (!s_signOwed || s_signed) {
    return;
  }
  s_signOwed = false;
  s_signed = buildAnswer();
  s_chunk = 0;
}

uint16_t Ps4Auth::getReport(uint8_t report_id, uint8_t *buffer,
                            uint16_t reqlen) {
  if (!isAvailable()) {
    return 0;
  }

  switch (report_id) {
  case kAnswerReport: {
    if (!s_signed) {
      return 0;
    }
    uint8_t payload[kFrameSize] = {};
    payload[0] = s_nonceId;
    payload[1] = s_chunk;
    payload[2] = 0;
    std::memcpy(&payload[3], &s_answer[s_chunk * kPageSize], kPageSize);
    // The checksum covers the report id and the bytes in front of it.
    uint8_t frame[kFrameSize + 1] = {};
    frame[0] = report_id;
    std::memcpy(&frame[1], payload, kFrameSize - 4);
    const uint32_t crc = Util::Crc::crc32(frame, kFrameSize - 3);
    std::memcpy(&payload[kFrameSize - 4], &crc, sizeof(crc));
    s_chunk = static_cast<uint8_t>((s_chunk + 1) % kAnswerChunks);
    return copyOut(payload, kFrameSize, buffer, reqlen);
  }
  case kStateReport: {
    // Zero says the signature is ready; sixteen says it is still being made.
    uint8_t payload[15] = {};
    payload[0] = s_nonceId;
    payload[1] = static_cast<uint8_t>(s_signed ? 0 : 16);
    uint8_t frame[12] = {};
    frame[0] = report_id;
    std::memcpy(&frame[1], payload, 11);
    const uint32_t crc = Util::Crc::crc32(frame, sizeof(frame));
    std::memcpy(&payload[11], &crc, sizeof(crc));
    return copyOut(payload, sizeof(payload), buffer, reqlen);
  }
  case kResetReport: {
    uint8_t payload[7] = {};
    s_signOwed = false;
    s_signed = false;
    s_chunk = 0;
    return copyOut(payload, sizeof(payload), buffer, reqlen);
  }
  default:
    return 0;
  }
}

void Ps4Auth::setReport(uint8_t report_id, uint8_t const *buffer,
                        uint16_t size) {
  if (!isAvailable() || report_id != kChallengeReport ||
      size != kFrameSize || buffer == nullptr) {
    return;
  }

  const uint8_t page = buffer[1];
  if (page > 4) {
    return;
  }

  // The checksum covers the report id and the bytes in front of it.
  uint8_t frame[kFrameSize + 1] = {};
  frame[0] = report_id;
  std::memcpy(&frame[1], buffer, kFrameSize - 4);
  uint32_t carried = 0;
  std::memcpy(&carried, &buffer[kFrameSize - 4], sizeof(carried));
  if (Util::Crc::crc32(frame, kFrameSize - 3) != carried) {
    return;
  }

  const uint16_t offset = static_cast<uint16_t>(page * kPageSize);
  const uint16_t length = (page == 4) ? kLastPageSize : kPageSize;
  std::memcpy(&s_challenge[offset], &buffer[3], length);

  if (page == 0) {
    s_nonceId = buffer[0];
    s_signed = false;
    s_signOwed = false;
    s_chunk = 0;
  }
  if (page == 4) {
    s_signOwed = true;
  }
}

} // namespace ThetaGP::Drivers::GPEmulator
