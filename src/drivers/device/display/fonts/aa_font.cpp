/**
 * This file is a part of ThetaGP.
 *
 * Anti-aliased font implementation.
 */

#include "aa_font.h"

namespace ThetaGP::Drivers::Device {

AaFont::AaFont(const uint8_t *bitmap, const GlyphDesc *glyphs,
               uint8_t lineHeight, uint8_t baseLine)
    : _bitmap(bitmap), _glyphs(glyphs),
      _lineHeight(lineHeight), _baseLine(baseLine) {
}

bool AaFont::getGlyph(char c, GlyphDesc &desc) const {
  uint8_t uc = static_cast<uint8_t>(c);
  if (uc < 0x20 || uc > 0x7E)
    return false;
  desc = _glyphs[uc - 0x20 + 1];  // +1 because index 0 is reserved
  return true;
}

} // namespace ThetaGP::Drivers::Device
