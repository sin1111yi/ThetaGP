/**
 * This file is a part of ThetaGP.
 *
 * Anti-aliased bitmap font — data extracted from LVGL Montserrat fonts.
 * Original font data: Copyright (c) LVGL project, MIT license.
 */

#pragma once

#include <cstdint>

namespace ThetaGP::Drivers::Device::DisplayDrv {

// Glyph metrics
struct GlyphDesc {
  uint32_t bitmapOffset;  // byte offset into glyph bitmap
  uint32_t advanceWidth;  // pixels to advance after this glyph
  uint8_t  boxWidth;      // width of the glyph box
  uint8_t  boxHeight;     // height of the glyph box
  int8_t   offsetX;       // horizontal offset from pen position
  int8_t   offsetY;       // vertical offset from baseline
};

/// Anti-aliased (4bpp) bitmap font ported from LVGL.
class AaFont {
public:
  AaFont(const uint8_t *bitmap, const GlyphDesc *glyphs,
         uint8_t lineHeight, uint8_t baseLine);

  /// Get glyph descriptor for ASCII char. Returns false if not found.
  bool getGlyph(char c, GlyphDesc &desc) const;

  /// Get pointer to glyph bitmap data (4bpp, row-major).
  const uint8_t *getBitmap(const GlyphDesc &desc) const { return _bitmap + desc.bitmapOffset; }

  uint8_t lineHeight() const { return _lineHeight; }
  uint8_t baseLine() const { return _baseLine; }

private:
  const uint8_t *_bitmap;
  const GlyphDesc *_glyphs;
  uint8_t _lineHeight;
  uint8_t _baseLine;
};

// ── Font instances ──────────────────────────────────────────────────────────

extern const AaFont font8;
extern const AaFont font12;
extern const AaFont font16;

} // namespace ThetaGP::Drivers::Device::DisplayDrv
