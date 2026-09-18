#pragma once

#include <cstdint>

/**
 * @brief Zero-allocation JSON helper wrapping frozen (json_scanf/json_printf).
 *
 * Parse mode — points to external input string, zero copy.
 * Write mode — writes into caller-provided buffer via json_printf.
 *
 * Path syntax for read accessors:
 *   "cmd"              → root key
 *   "map.socd"         → nested object key
 *
 * An array element is read by naming the array and the index separately
 * (getArrInt("map.btn_map", 3)), not by spelling the index into the path: the
 * walker names an element "<array>[<index>]", which a dot path cannot express.
 */
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

  /** Point parser at external JSON string. len=-1 → strlen. */
  void parse(const char *s, int len = -1);

  /**
   * Int value at dot path, or def if the key is missing or is not spelled as a
   * plain decimal integer.
   */
  int getInt(const char *path, int def = 0) const;

  /**
   * Int value of the element at index idx of the array at path (path names the
   * array, not the element), or def if the array or that element is missing, or
   * the element is not spelled as a plain decimal integer.
   */
  int getArrInt(const char *path, int idx, int def = 0) const;

  /**
   * String value at dot path, or nullptr if missing.
   * Returns pointer into the original JSON string (zero copy, NOT null-terminated).
   * The returned string is valid only while the original JSON is alive.
   * Length is written to *outLen (if non-null).
   */
  const char *getStr(const char *path, int *outLen = nullptr) const;

  /**
   * String value at dot path, copied into buf with null termination.
   * Returns buf on success, nullptr if missing/empty.
   * Safe to use with %Q and strcmp.
   */
  const char *getStrCopy(const char *path, char *buf, int bufSize) const;

  /** Bool value at dot path, or def if missing. */
  bool getBool(const char *path, bool def = false) const;

  /**
   * What this document holds at `path`: Present, Absent, or Unknown when the
   * path cannot be spelled into a lookup at all — a path longer than the format
   * the lookup walks with can hold, or an empty one. The distinction matters to
   * callers that count: "absent" is a fact about the document, "unknown" is the
   * absence of an answer.
   */
  enum class KeyLookup { Present, Absent, Unknown };
  KeyLookup lookup(const char *path) const;

  /**
   * True if the key exists at the given path.
   *
   * False for a path the lookup cannot be built for, which is not "yes" either
   * way; lookup() tells the two apart.
   */
  bool has(const char *path) const;

  /** Number of elements in array at path. */
  int getArrLen(const char *path) const;

  /** Direct frozen scanf (for batch operations). */
  int scanf(const char *fmt, ...) const;

  // ── Parse: key comparison ───────────────────────────────────

  /**
   * Number of object keys `source` carries that this document does not.
   *
   * A key is identified by the chain of names that reaches it, not by the
   * walker's spelling of its path: a key named `a.b` is not the `b` of `a`, and
   * a key whose name ends in `]` is not an array element. A member an array
   * holds is an element rather than a key — the key holding the array is
   * counted, its elements are not. A name `source` repeats is counted as often
   * as it appears, because what is counted is the occurrences that are not here
   * and not the distinct names.
   *
   * 0 means "nothing is known to be left behind", which is also the answer when
   * the comparison has none: when either body does not parse whole, when a
   * name cannot be spelled into a lookup (one carrying '.', '[' or ']'), when a
   * chain is deeper or its path longer than the lookup can walk (8 names, 122
   * bytes), or when this document has no lookup for a key of the source. A
   * count that is quietly short or quietly long is worse than none, so an
   * incomplete comparison reports nothing rather than part of the truth.
   */
  uint32_t missingKeyCount(const Json &source) const;

  // ── Write ───────────────────────────────────────────────────

  /** Begin write mode, bind to caller buffer. */
  void beginWrite(char *buf, int cap);

  /** Append formatted JSON via frozen json_printf. */
  void printf(const char *fmt, ...);

  /** Finalize, null-terminate, return written length. */
  int end();

  /**
   * True when a write dropped output because the buffer filled up: the text in
   * the buffer is then an incomplete prefix and must not be taken for a whole
   * JSON document. Cleared by beginWrite(); valid until the next beginWrite().
   */
  bool overflowed() const { return _overflowed; }

  const char *c_str() const { return _buf; }
  int len() const { return _writing ? _len : 0; }

  // ── Common ──────────────────────────────────────────────────

  /** Reset both parse and write state. */
  void reset();
};
