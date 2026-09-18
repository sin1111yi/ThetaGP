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

// A token that spells a whole decimal integer and nothing else: an optional
// minus, then digits that start with a non-zero one unless the number is the
// single zero, then a value terminator. frozen ends a number token where the
// digits end, so a document spelling 1x hands over the token "1"; the digits of
// 1.9, 1e1, 0x1 and 01 stop inside the token. Digits taken out of either
// spelling are a number the document never carried, and a bit index or an
// enumerator is where such a number lands, so the whole spelling and the
// character behind the token are both checked here.
static bool isPlainIntToken(const struct json_token &tok, const char *input,
                            int inputLen) {
  if (!tok.ptr || tok.len <= 0) {
    return false;
  }

  // A token that carries no digit covers nothing.
  int i = (tok.ptr[0] == '-') ? 1 : 0;
  if (i == tok.len) {
    return false;
  }
  // JSON gives a leading zero to no number but zero itself.
  if (tok.ptr[i] == '0' && i + 1 < tok.len) {
    return false;
  }
  for (; i < tok.len; ++i) {
    if (tok.ptr[i] < '0' || tok.ptr[i] > '9') {
      return false;
    }
  }

  // The token covers its digits only, so the character behind it is the one
  // sign of a spelling the token did not cover. A document that ends at the
  // token carries no character there.
  const char *after = tok.ptr + tok.len;
  if (after >= input + inputLen) {
    return true;
  }
  switch (*after) {
  case ' ':
  case '\t':
  case '\r':
  case '\n':
  case ',':
  case '}':
  case ']':
  case '\0':
    return true;
  default:
    return false;
  }
}

const char *Json::buildFmt(const char *path, const char *spec) const {
  if (!path || !spec) return nullptr;

  // What the format holds, counted before a byte of it is written: one "{key:"
  // per segment (the separators of the path are not written), the format
  // specifier, one '}' per segment to close them, and the terminator. A path
  // whose format does not fit the buffer as a whole is refused as a whole — a
  // truncated format is one that walks somewhere other than the path it was
  // built from.
  size_t pathLen = 0;
  int segments = 1;  // the last segment
  for (const char *p = path; *p; ++p) {
    if (*p == '.') segments++;
    pathLen++;
  }
  const size_t specLen = strlen(spec);
  if (pathLen + 2 * (size_t)segments + specLen + 2 > sizeof(_fmtBuf)) {
    return nullptr;
  }

  char *d = _fmtBuf;
  const char *p = path;
  for (int i = 0; i < segments; i++) {
    *d++ = '{';
    while (*p && *p != '.') *d++ = *p++;
    if (*p == '.') p++;  // skip dot
    *d++ = ':';
  }
  memcpy(d, spec, specLen);
  d += specLen;
  for (int i = 0; i < segments; i++) *d++ = '}';
  *d = '\0';
  return _fmtBuf;
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
  // No format means no lookup: the answer to this path is the default, exactly
  // as if the document did not carry it.
  if (!fmt) return def;
  if (json_scanf(_input, _inputLen, fmt, &tok) == 1 && isNumberToken(tok) &&
      isPlainIntToken(tok, _input, _inputLen)) {
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
      isNumberToken(tok) && isPlainIntToken(tok, _input, _inputLen)) {
    return tokenToInt(tok);
  }
  return def;
}

const char *Json::getStr(const char *path, int *outLen) const {
  if (!_input) { if (outLen) *outLen = 0; return nullptr; }
  struct json_token tok;
  const char *fmt = buildFmt(path, "%T");
  if (fmt && json_scanf(_input, _inputLen, fmt, &tok) == 1 &&
      tok.type == JSON_TYPE_STRING) {
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
  if (fmt && json_scanf(_input, _inputLen, fmt, &tok) == 1) {
    if (tok.type == JSON_TYPE_TRUE) return true;
    if (tok.type == JSON_TYPE_FALSE) return false;
    if (tok.type == JSON_TYPE_NUMBER) {
      // Accept 0/1 as boolean
      return (tok.ptr[0] == '1');
    }
  }
  return def;
}

Json::KeyLookup Json::lookup(const char *path) const {
  if (!_input || !path || !*path) return KeyLookup::Unknown;
  struct json_token tok;
  const char *fmt = buildFmt(path, "%T");
  // A path with no format has no lookup, and the answer is not "not here": the
  // document was never asked. Callers that treat the missing lookup as absence
  // report a key that may well be present (see missingKeyCount).
  if (!fmt) return KeyLookup::Unknown;
  return json_scanf(_input, _inputLen, fmt, &tok) == 1 ? KeyLookup::Present
                                                       : KeyLookup::Absent;
}

bool Json::has(const char *path) const {
  return lookup(path) == KeyLookup::Present;
}

int Json::getArrLen(const char *path) const {
  if (!_input) return 0;
  struct json_token tok;
  const char *fmt = buildFmt(path, "%T");
  if (!fmt) return 0;
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

// ── Parse: key comparison ───────────────────────────────────

namespace {

// A key is identified by the chain of names that reaches it, and the lookup
// that asks about it walks that chain as a dotted path. The walker spells a
// member's path the same way, but its spelling is not the chain: a name
// carrying a '.' reads as two names, and a name carrying '[' or ']' reads as an
// array element. The walk below therefore carries the chain itself rather than
// trusting the path, and anything it cannot name exactly makes the whole
// comparison unknown — a count that is quietly short or quietly long is worse
// than no count at all.

// The longest dotted path a lookup can be built for. The format is the path,
// two bytes and a '}' per name, the specifier and the terminator, and
// buildFmt's buffer holds 128 bytes: past 128 - 3 (specifier and terminator)
// - 3 (the shortest name's braces) = 122 bytes of path there is no format for
// any segmentation.
constexpr size_t kMaxLookupPathLen = 122;

// The deepest chain of names this counts. A body nested deeper than this is far
// past the shapes this layer writes, and one is answered "unknown" rather than
// guessed at.
constexpr int kMaxKeyNames = 8;

// What the walk below carries between callbacks: the document a key of the
// source is looked up in, the names of the objects open at this point, and what
// the walk has learned so far.
struct KeyWalk {
  const Json *carrier;              // the document keys are looked up in
  const char *name[kMaxKeyNames];   // names of the objects open here
  uint16_t nameLen[kMaxKeyNames];
  size_t namePathLen[kMaxKeyNames]; // path length each of them was opened with
  int names;                        // entries of the arrays above in use
  int arrays;                       // arrays open at this point
  bool complete;                    // every key could be asked about
  uint32_t missing;                 // keys of the source the carrier lacks
  char path[kMaxLookupPathLen + 1]; // scratch: the dotted path of one key
};

// Whether a name can be spelled as one name of a dotted path. An empty name
// reaches nothing, and one carrying '.' would be read as two names while one
// carrying the brackets of an array element would be read as an element.
static bool isPlainName(const char *name, size_t len) {
  if (!name || len == 0) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = name[i];
    if (c == '.' || c == '[' || c == ']') return false;
  }
  return true;
}

// Ask the carrier about the key whose last name is `leaf`, reached through the
// objects the walk has open, and count it when the carrier does not carry it.
static void countKey(KeyWalk *walk, const char *leaf, size_t leafLen) {
  if (!isPlainName(leaf, leafLen) || walk->names >= kMaxKeyNames) {
    walk->complete = false;
    return;
  }

  size_t n = 0;
  for (int i = 0; i < walk->names; i++) {
    if (n + walk->nameLen[i] + 1 > kMaxLookupPathLen) {
      walk->complete = false;
      return;
    }
    if (i > 0) walk->path[n++] = '.';
    memcpy(walk->path + n, walk->name[i], walk->nameLen[i]);
    n += walk->nameLen[i];
  }
  if (n > 0) walk->path[n++] = '.';
  if (n + leafLen > kMaxLookupPathLen) {
    walk->complete = false;
    return;
  }
  memcpy(walk->path + n, leaf, leafLen);
  n += leafLen;
  walk->path[n] = '\0';

  switch (walk->carrier->lookup(walk->path)) {
  case Json::KeyLookup::Absent:
    walk->missing++;
    break;
  case Json::KeyLookup::Present:
    break;
  default:
    // The carrier has no lookup for this key: nothing here says whether it
    // carries it, so the comparison has no answer.
    walk->complete = false;
    break;
  }
}

// One member of the document being walked, as json_walk reports it.
//
// The walker reports an object's members, an array's elements, and the start
// and the end of every container. A member of an object is a key; an element of
// an array is not, and neither is an event that ends a container (those arrive
// unnamed). What an array holds is told apart by the walk's own count of open
// arrays rather than by the look of the path, so a key whose name ends in ']'
// is a key like any other.
static void missingKeyCb(void *data, const char *name, size_t nameLen,
                         const char *path, const struct json_token *token) {
  KeyWalk *walk = static_cast<KeyWalk *>(data);
  if (!token || !path) return;

  switch (token->type) {
  case JSON_TYPE_OBJECT_END:
    // An object's end arrives with the path the object was opened with, and a
    // name's entry was recorded with that same length; an unnamed object (the
    // body itself, or an element of an array) closes no entry, because its path
    // is strictly longer than the entry's.
    if (walk->names > 0 &&
        walk->namePathLen[walk->names - 1] == strlen(path)) {
      walk->names--;
    }
    return;
  case JSON_TYPE_ARRAY_END:
    if (walk->arrays > 0) walk->arrays--;
    // An array's end arrives with the path the array was opened with, exactly
    // as an object's does, so the name ARRAY_START recorded for the member
    // holding it closes here by the same test. An array that opened no entry
    // leaves the path strictly longer than the entry's — a member's array is
    // named by a path whose length the end repeats, while an element's array
    // carries the brackets of the array around it — or the chain is empty, as
    // for the body itself.
    if (walk->names > 0 &&
        walk->namePathLen[walk->names - 1] == strlen(path)) {
      walk->names--;
    }
    return;
  case JSON_TYPE_OBJECT_START:
  case JSON_TYPE_ARRAY_START:
    if (walk->arrays == 0 && name) {
      // A member of an object is a key — the one holding this container — and
      // it is also a name on the chain of everything this object holds.
      countKey(walk, name, nameLen);
      if (walk->names < kMaxKeyNames && isPlainName(name, nameLen)) {
        walk->name[walk->names] = name;
        walk->nameLen[walk->names] = static_cast<uint16_t>(nameLen);
        walk->namePathLen[walk->names] = strlen(path);
        walk->names++;
      }
    }
    if (token->type == JSON_TYPE_ARRAY_START) walk->arrays++;
    return;
  default:
    // A scalar: the key is the name it is reported under.
    if (walk->arrays == 0 && name) countKey(walk, name, nameLen);
    return;
  }
}

// Whether a walk that reports `consumed` bytes got through a body of `len`
// bytes. What is left behind the walked part may carry keys of its own, so a
// walk that stopped early leaves the comparison unknown; whitespace is the one
// thing that can stand there without hiding anything.
static bool walkedWhole(const char *body, int len, int consumed) {
  if (consumed <= 0 || consumed > len) return false;
  for (int i = consumed; i < len; i++) {
    const char c = body[i];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
  }
  return true;
}

} // namespace

uint32_t Json::missingKeyCount(const Json &source) const {
  if (!_input || !source._input) return 0;

  // The document being asked is walked whole first: one that does not parse
  // answers "absent" to every key, which would report the whole of the source
  // as left behind — a count this has no answer for rather than one to hand
  // over. The walk carries no callback, so running it costs a parse and nothing
  // else.
  if (!walkedWhole(_input, _inputLen,
                   json_walk(_input, _inputLen, nullptr, nullptr))) {
    return 0;
  }

  KeyWalk walk = {};
  walk.carrier = this;
  walk.complete = true;
  const int consumed =
      json_walk(source._input, source._inputLen, missingKeyCb, &walk);
  // A source the walk could not get through holds keys this never saw, and what
  // it does not know it does not report: 0 is "nothing is known to be left
  // behind", never "nothing was left behind".
  if (!walkedWhole(source._input, source._inputLen, consumed)) {
    walk.complete = false;
  }
  return walk.complete ? walk.missing : 0;
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
