#pragma once

#include <cstdint>

// Zero-allocation JSON, over frozen. Parse mode reads the caller's string in
// place; write mode fills the caller's buffer.
//
// A path is dot-separated: "cmd" is a root key, "map.socd" a nested one. An
// array element is read by naming the array and the index apart, because the
// walker names an element "<array>[<index>]" and a dot path cannot express it.
class Json {
  const char *_input = nullptr;  // parse: points to external data
  int _inputLen = 0;
  char *_buf = nullptr;          // write: caller buffer
  int _cap = 0;
  int _len = 0;
  bool _writing = false;
  bool _overflowed = false;      // write: a piece did not fit the buffer

  mutable char _fmtBuf[128];     // temp format string builder

  // Build a frozen format string from a dot path.
  // "map.socd" → "{map:{socd:%T}}"
  const char *buildFmt(const char *path, const char *spec) const;

public:
  // ── Parse ───────────────────────────────────────────────────

  // Points the parser at a JSON string; a negative len means strlen.
  void parse(const char *s, int len = -1);

  // The int at a dot path, or def when the key is missing or not a plain
  // decimal integer.
  int getInt(const char *path, int def = 0) const;

  // The int at one index of the array a path names, or def.
  int getArrInt(const char *path, int idx, int def = 0) const;

  // The string at a dot path, or nullptr: a pointer into the original JSON,
  // not terminated, valid while that JSON is, its length written to outLen.
  const char *getStr(const char *path, int *outLen = nullptr) const;

  // The string at a dot path, copied into buf and terminated; nullptr when it
  // is missing or empty.
  const char *getStrCopy(const char *path, char *buf, int bufSize) const;

  // The bool at a dot path, or def when it is missing.
  bool getBool(const char *path, bool def = false) const;

  // What this document holds at a path. Unknown is for a path no lookup can be
  // built from: empty, or longer than the walker holds. It is not "absent".
  enum class KeyLookup { Present, Absent, Unknown };
  KeyLookup lookup(const char *path) const;

  // Whether the key exists at a path. False for a path no lookup can be built
  // from; lookup() tells the two apart.
  bool has(const char *path) const;

  // How many elements the array at a path holds.
  int getArrLen(const char *path) const;

  // frozen's scanf, for batch operations.
  int scanf(const char *fmt, ...) const;

  // ── Parse: key comparison ───────────────────────────────────

  // How many object keys `source` carries that this document does not. A key is
  // the chain of names that reaches it, not the walker's spelling of a path: a
  // key named `a.b` is not the `b` of `a`. Members an array holds are elements,
  // not keys, and a repeated name is counted once for each occurrence.
  //
  // 0 also answers a comparison that has no answer: a body that does not parse
  // whole, a name no lookup can be built from, or a chain deeper than the walker
  // reaches. An incomplete count reports nothing rather than part of it.
  uint32_t missingKeyCount(const Json &source) const;

  // ── Write ───────────────────────────────────────────────────

  // Enters write mode on the caller's buffer.
  void beginWrite(char *buf, int cap);

  // Appends formatted JSON through frozen's json_printf.
  void printf(const char *fmt, ...);

  // Ends the write, terminates the text, and reports its length.
  int end();

  // Whether a write dropped output because the buffer filled: the text is then
  // a prefix, not a document. Cleared by beginWrite().
  bool overflowed() const { return _overflowed; }

  const char *c_str() const { return _buf; }
  int len() const { return _writing ? _len : 0; }

  // ── Common ──────────────────────────────────────────────────

  // Resets both parse and write state.
  void reset();
};
