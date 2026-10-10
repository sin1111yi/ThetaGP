# ThetaGP

<p align="center">
  <img src="asset/thetagp-logo.png" alt="ThetaGP Logo" width="400">
</p>

Universal gamepad firmware: USB HID plus a CDC command channel, configured per
board. No RTOS, no dynamic allocation.

## Features

- USB HID gamepad (GP2040-CE compatible button mapping); scan-matrix keypad
- CDC command channel carrying protobuf frames (schema: `ThetaGP.PB`)
- TOML board configuration, validated and generated at configure time
- Statically allocated, cooperative task scheduler

## Quick Start

```bash
git submodule update --init      # the protocol schema
git submodule update --remote    # to the newest schema on its main
cmake --preset BoringTechH743   # configures, and fetches dependencies
cmake --build --preset BoringTechH743
probe-rs run --chip <CHIP> build/BoringTechH743/ThetaGP_*.elf
```

`CMakePresets.json` declares one preset per board — `BoringTechH743` and
`ThetaGPH7` — each with a `-release` variant, and puts each build tree under
`build/<preset>/`. Configuring by hand with `-DTARGET=<board>` is equivalent.

Needs CMake 4.0+, `arm-none-eabi-gcc`, Python 3.11+ and probe-rs.

## Layout

```
configs/    per-board TOML and its generated BoardConfig.h / board_config.cmake
platform/   MCU ports (STM32H7)
scripts/    build tooling and host tools
src/        firmware: wire/ (protocol), drivers/, gamepad/, utils/
lib/        third-party, fetched at configure time; the protocol schema is the
            submodule lib/ThetaGP.PB, following its main
```

Board configuration is TOML under `configs/<TARGET>/BoardConfig.toml`; see
`configs/CONFIGURATION.md` for the fields. The configure step also writes
`.clangd` with the toolchain's include search list, which the compilation
database does not carry.

## Dependencies

Declared in `lib/CMakeLists.txt` and fetched at configure. Every dependency
follows its branch and is checked once a day — the protocol schema included: it
is the submodule at `lib/ThetaGP.PB`, on the schema repository's `main`, and it
is the contract the host toolkit speaks as well, so both sides carry one commit.

| Library | Purpose |
|---|---|
| TinyUSB | USB device stack |
| frozen | JSON parser behind `src/utils/json` (profile bodies) |
| nanopb | protobuf codec for the wire |
| mbedTLS | fetched, not linked |
| ThetaGP.PB | the protocol schema |

## License

GPL-3.0
