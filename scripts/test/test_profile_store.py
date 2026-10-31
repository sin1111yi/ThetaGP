#!/usr/bin/env python3
"""Compile the profile store against an in-memory NOR flash."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix="thetagp_store_") as tmp:
    stub = Path(tmp)
    files = {
        "build_info.h": "#pragma once\n#define COMMON_ZERO_INIT\n",
        "conf/ThetaGP_Config.h": "#define THETAGP_CFG_HAS_FLASH 1\n",
        "utils/log/log.h": "#pragma once\n#define LOG_INFO(...)\n#define LOG_WARN(...)\n#define LOG_ERROR(...)\n#define LOG_DEBUG(...)\n",
        "drivers/device/flash/flash_base.h": """#pragma once
#include <cstdint>
namespace ThetaGP::Drivers::Device {
struct FlashInfo { uint32_t sizeBytes = 0; };
class FlashBase {
public:
  static FlashBase &getInstance();
  bool isInitialized() const { return true; }
  bool read(uint32_t, uint8_t *, uint32_t);
  bool write(uint32_t, const uint8_t *, uint32_t);
  bool eraseSector(uint32_t);
  const FlashInfo &getInfo() const;
};
}
""",
    }
    for name, content in files.items():
        path = stub / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    binary = stub / "profile_store_test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra", "-O1",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(stub), "-I", str(ROOT / "src"),
        str(ROOT / "scripts/test/test_profile_store_cases.cpp"),
        str(ROOT / "src/gamepad/profile/profile_store.cpp"),
        str(ROOT / "src/utils/crc.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
