/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>

// ── Linker-exported region bounds ──
// Absolute symbols: the symbol's value IS the number. Read them as
// (uintptr_t)&sym — never dereference. The linker emits plain C symbols,
// so the C++ side needs extern "C" or the mangled name fails to link.
extern "C" uint8_t _mem_base_flash[];  extern "C" uint8_t _mem_end_flash[];  extern "C" uint8_t _mem_size_flash[];
extern "C" uint8_t _mem_base_dtcm[];   extern "C" uint8_t _mem_end_dtcm[];   extern "C" uint8_t _mem_size_dtcm[];
extern "C" uint8_t _mem_base_axi[];    extern "C" uint8_t _mem_end_axi[];    extern "C" uint8_t _mem_size_axi[];
extern "C" uint8_t _mem_base_d2[];     extern "C" uint8_t _mem_end_d2[];     extern "C" uint8_t _mem_size_d2[];
extern "C" uint8_t _mem_base_d3[];     extern "C" uint8_t _mem_end_d3[];     extern "C" uint8_t _mem_size_d3[];
extern "C" uint8_t _mem_base_itcm[];   extern "C" uint8_t _mem_end_itcm[];   extern "C" uint8_t _mem_size_itcm[];
extern "C" uint8_t _suser_stack[];     extern "C" uint8_t _euser_stack[];    // stack reserve (DTCMRAM)
extern "C" uint8_t _sram_heap[];       extern "C" uint8_t _eram_heap[];      // heap reserve (AXI SRAM)

namespace ThetaGP::Util::MemInfo {

// Resource accounting built purely on linker symbol arithmetic: no state,
// no allocation, callable from any context.

enum class RegionId : uint8_t { Flash = 0, Dtcm, Axi, D2, D3, Itcm, Count };

struct RegionUsage {
  uint32_t base = 0;     // region start (= ORIGIN)
  uint32_t end = 0;      // high-water mark (end of the last section, gaps included)
  uint32_t size = 0;     // region capacity (= LENGTH)
  uint32_t used = 0;     // end - base
  uint32_t reserved = 0; // fixed reserve already counted in used (DTCM stack / AXI heap; 0 elsewhere)
};

// Address of an absolute linker symbol.
inline uint32_t symbolAddress(const uint8_t *sym) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sym));
}

// Reserve spans measured from their own section bounds.
inline uint32_t stackBytes() { return symbolAddress(_euser_stack) - symbolAddress(_suser_stack); }
inline uint32_t heapBytes() { return symbolAddress(_eram_heap) - symbolAddress(_sram_heap); }

// Per-region usage. Pure computation, no caching.
inline RegionUsage region(RegionId id) {
  const uint8_t *base = nullptr;
  const uint8_t *end = nullptr;
  const uint8_t *size = nullptr;
  RegionUsage usage;

  switch (id) {
  case RegionId::Flash:
    base = _mem_base_flash; end = _mem_end_flash; size = _mem_size_flash;
    break;
  case RegionId::Dtcm:
    base = _mem_base_dtcm; end = _mem_end_dtcm; size = _mem_size_dtcm;
    usage.reserved = stackBytes();
    break;
  case RegionId::Axi:
    base = _mem_base_axi; end = _mem_end_axi; size = _mem_size_axi;
    usage.reserved = heapBytes();
    break;
  case RegionId::D2:
    base = _mem_base_d2; end = _mem_end_d2; size = _mem_size_d2;
    break;
  case RegionId::D3:
    base = _mem_base_d3; end = _mem_end_d3; size = _mem_size_d3;
    break;
  case RegionId::Itcm:
    base = _mem_base_itcm; end = _mem_end_itcm; size = _mem_size_itcm;
    break;
  default:
    return usage;
  }

  usage.base = symbolAddress(base);
  usage.end = symbolAddress(end);
  usage.size = symbolAddress(size);
  usage.used = usage.end - usage.base;
  return usage;
}

// Sum over the five RAM regions (DTCM, AXI, D2, D3, ITCM).
inline uint32_t ramUsedBytes() {
  return region(RegionId::Dtcm).used + region(RegionId::Axi).used +
         region(RegionId::D2).used + region(RegionId::D3).used +
         region(RegionId::Itcm).used;
}

inline uint32_t ramTotalBytes() {
  return region(RegionId::Dtcm).size + region(RegionId::Axi).size +
         region(RegionId::D2).size + region(RegionId::D3).size +
         region(RegionId::Itcm).size;
}

// Stack + heap reserves, both already included in ramUsedBytes().
inline uint32_t ramReservedBytes() { return stackBytes() + heapBytes(); }

// Bytes actually held by data (used minus the fixed reserves).
inline uint32_t ramLiveBytes() { return ramUsedBytes() - ramReservedBytes(); }

// MCU flash: everything that has to be programmed, load images included.
inline uint32_t mcuFlashUsedBytes() { return region(RegionId::Flash).used; }
inline uint32_t mcuFlashTotalBytes() { return region(RegionId::Flash).size; }

} // namespace ThetaGP::Util::MemInfo
