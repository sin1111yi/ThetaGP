#include "utils/json/json.h"
#include "frozen.h"

#include <climits>
#include <cstdio>
#include <cstring>

// ── Path helpers ──────────────────────────────────────────────

// Digits of a number token as an int. strtol is not used: newlib-nano's returns
// the right value but leaves endptr at the start, so the usual endptr check
// fails from the second call on.
static int tokenToInt(const struct json_token &tok) {
  int val = 0;
  bool negative = false;
  int i = 0;
  if (tok.ptr[0] == '-') {
    negative = true;
    i = 1;
  }
  for (; i < tok.len; ++i) {
    const char c = tok.ptr[i];
    if (c < '0' || c > '9') {
      break;
    }
    const int digit = c - '0';
    // The JSON text comes from outside, so a number wider than int is saturating
    // rather than wrapping: every caller compares the value against its own
    // bounds, and a value that saturates keeps its sign and its rejection.
    if (val > (INT_MAX - digit) / 10) {
      return negative ? INT_MIN : INT_MAX;
    }
    val = val * 10 + digit;
  }
  return negative ? -val : val;
}

static bool isNumberToken(const struct json_token &tok) {
  return tok.type == JSON_TYPE_NUMBER && tok.ptr && tok.len > 0;
}

const char *Json::buildFmt(const char *path, const char *spec) const {
  // Count segments and find last dot
  const char *p = path;
  int segments = 0;
  const char *lastDot = nullptr;
  (void)lastDot;
  while (*p) {
    if (*p == '.') { segments++; lastDot = p; }
    p++;
  }
  segments++;  // last segment

  char *d = _fmtBuf;
  int remain = sizeof(_fmtBuf) - 4;  // room for %T + null
  bool truncated = false;

  // Open braces with keys
  p = path;
  for (int i = 0; i < segments; i++) {
    if (d - _fmtBuf >= remain) { truncated = true; break; }
    *d++ = '{';
    while (*p && *p != '.') {
      if (d - _fmtBuf >= remain) { truncated = true; break; }
      *d++ = *p++;
    }
    if (truncated) break;
    *d++ = ':';
    if (*p == '.') p++;  // skip dot
  }

  // Format specifier
  if (!truncated) {
    size_t specLen = strlen(spec);
    if (d - _fmtBuf + (int)specLen < remain) {
      memcpy(d, spec, specLen);
      d += specLen;
    } else {
      truncated = true;
    }
  }

  // Close braces
  if (!truncated) {
    for (int i = 0; i < segments; i++) {
      if (d - _fmtBuf >= remain) { truncated = true; break; }
      *d++ = '}';
    }
  }

  *d = '\0';
  return truncated ? nullptr : _fmtBuf;
}

// ── Parse ─────────────────────────────────────────────────────

void Json::parse(const char *s, int len) {
  _input = s;
  _inputLen = (len >= 0) ? len : (int)strlen(s);
  _writing = false;
}

int Json::getInt(const char *path, int def) const {
  if (!_input) return def;
  struct json_token tok;
  const char *fmt = buildFmt(path, "%T");
  if (json_scanf(_input, _inputLen, fmt, &tok) == 1 && isNumberToken(tok)) {
    return tokenToInt(tok);
  }
  return def;
}

int Json::getArrInt(const char *path, int idx, int def) const {
  if (!_input || !path || idx < 0) return def;

  // frozen walks into an array element under the name "<array>[<index>]" and
  // matches that spelling against the path it is given plus the index. Object
  // keys on that path carry a leading dot, so the array path is spelled with
  // one; a path without it reaches no element.
  char arrPath[64];
  const size_t pathLen = strlen(path);
  if (pathLen + 2 > sizeof(arrPath)) return def;
  arrPath[0] = '.';
  memcpy(arrPath + 1, path, pathLen + 1);

  struct json_token tok;
  if (json_scanf_array_elem(_input, _inputLen, arrPath, idx, &tok) > 0 &&
      isNumberToken(tok)) {
    return tokenToInt(tok);
  }
  return def;
}

const char *Json::getStr(const char *path, int *outLen) const {
  if (!_input) { if (outLen) *outLen = 0; return nullptr; }
  struct json_token tok;
  const char *fmt = buildFmt(path, "%T");
  if (json_scanf(_input, _inputLen, fmt, &tok) == 1 && tok.type == JSON_TYPE_STRING) {
    if (outLen) *outLen = tok.len;
    return tok.ptr;
  }
  if (outLen) *outLen = 0;
  return nullptr;
}

const char *Json::getStrCopy(const char *path, char *buf, int bufSize) const {
  int len = 0;
  const char *s = getStr(path, &len);
  if (!s || len <= 0 || bufSize <= 0) return nullptr;
  if (len >= bufSize) len = bufSize - 1;
  memcpy(buf, s, len);
  buf[len] = '\0';
  return buf;
}

bool Json::getBool(const char *path, bool def) const {
  if (!_input) return def;
  struct json_token tok;
  const char *fmt = buildFmt(path, "%T");
  if (json_scanf(_input, _inputLen, fmt, &tok) == 1) {
    if (tok.type == JSON_TYPE_TRUE) return true;
    if (tok.type == JSON_TYPE_FALSE) return false;
    if (tok.type == JSON_TYPE_NUMBER) {
      // Accept 0/1 as boolean
      return (tok.ptr[0] == '1');
    }
  }
  return def;
}

bool Json::has(const char *path) const {
  if (!_input) return false;
  struct json_token tok;
  const char *fmt = buildFmt(path, "%T");
  return json_scanf(_input, _inputLen, fmt, &tok) == 1;
}

int Json::getArrLen(const char *path) const {
  if (!_input) return 0;
  struct json_token tok;
  const char *fmt = buildFmt(path, "%T");
  if (json_scanf(_input, _inputLen, fmt, &tok) != 1) return 0;
  if (tok.type != JSON_TYPE_ARRAY_END) return 0;
  // tok.ptr points to '[', tok.len is the entire array length (incl. brackets)
  // Empty array "[]" -> len=2 -> return 0
  if (tok.len <= 2) return 0;
  // Scan and count elements
  const char *p = tok.ptr + 1;  // skip '['
  int depth = 0, count = 0;
  bool inStr = false;
  char prev = 0;
  while (p < tok.ptr + tok.len - 1) {
    char c = *p;
    if (c == '\"' && prev != '\\') inStr = !inStr;
    if (!inStr) {
      if (c == '[' || c == '{') depth++;
      if ((c == ']' || c == '}') && depth > 0) depth--;
      if (depth == 0 && c == ',') count++;
    }
    prev = c;
    p++;
  }
  return count + 1;
}

int Json::scanf(const char *fmt, ...) const {
  if (!_input) return -1;
  va_list ap;
  va_start(ap, fmt);
  int ret = json_vscanf(_input, _inputLen, fmt, ap);
  va_end(ap);
  return ret;
}

// ── Write ─────────────────────────────────────────────────────

void Json::beginWrite(char *buf, int cap) {
  _buf = buf;
  _cap = cap;
  _len = 0;
  _writing = true;
  _overflowed = false;
  if (_buf && _cap > 0) _buf[0] = '\0';
}

void Json::printf(const char *fmt, ...) {
  if (!_buf || !_writing) return;
  int remain = _cap - _len;
  if (remain <= 0) return;
  struct json_out out = JSON_OUT_BUF(_buf + _len, (size_t)remain);
  va_list ap;
  va_start(ap, fmt);
  int n = json_vprintf(&out, fmt, ap);
  va_end(ap);
  // json_vprintf returns the length the piece needed, not the length that
  // fit, so _len overshooting _cap is how a lost tail shows up.
  if (n > 0) _len += n;
  if (_len >= _cap) {
    _len = _cap - 1;
    _overflowed = true;
  }
  _buf[_len] = '\0';
}

int Json::end() {
  _writing = false;
  return _len;
}

// ── Common ────────────────────────────────────────────────────

void Json::reset() {
  _input = nullptr;
  _inputLen = 0;
  _buf = nullptr;
  _cap = 0;
  _len = 0;
  _writing = false;
  _overflowed = false;
}
