# Static buffers

Every static buffer of 256 bytes or more that this tree allocates is listed here,
with the macro that places it and the region it lands in, so the total is
reviewable in one place. A buffer added without a row here is a review finding.

Two rules about what the table is:

- **It is a checklist, not an addend.** These bytes are already inside the region
  totals the linker prints, so the table is never summed with them.
- **Name and size are the contract.** An address moves whenever a symbol ahead of
  it is added or removed, so a row is compared by size and never by address.

## How the table was read

```bash
cmake --preset BoringTechH743 && cmake --build --preset BoringTechH743
elf=build/ThetaGP_*_Debug.elf
arm-none-eabi-nm -S --size-sort --radix=d "$elf" | awk '$3 ~ /^[bBdD]$/ && $2 >= 256'
arm-none-eabi-nm -S "$elf" | arm-none-eabi-c++filt      # names
```

A symbol's region comes from its address: DTCMRAM `0x2000_0000`, AXI SRAM
`0x2400_0000`, RAM_D2 `0x3000_0000`, RAM_D3 `0x3800_0000`, ITCM `0x0000_0000`.
A symbol without the `COMMON_*` macro is in `.bss`/`.data` and therefore lands in
DTCM. The snapshot below is the BoringTechH743 debug build of this tree.

## What the tree owns

| buffer | size | macro | region | definition |
|---|---:|---|---|---|
| `Frame::instance` | 5,156 | plain static, function-local | DTCM | `src/wire/frame.cpp:35` |
| `s_replaced` | 4,097 | `COMMON_ZERO_INIT` | AXI | `src/gamepad/config/config_manager.cpp:216` |
| `s_staging` | 4,097 | `COMMON_ZERO_INIT` | AXI | `src/gamepad/profile/profile_store.cpp:34` |
| `s_compaction` | 4,096 | `COMMON_ZERO_INIT` | AXI | `src/gamepad/profile/profile_store.cpp:35` |
| `s_stage` | 4,096 | `COMMON_ZERO_INIT` | AXI | `src/wire/flash.cpp:23` |
| `s_frames` | 1,200 | `COMMON_ZERO_INIT` | AXI | `src/drivers/device/rgb_strip/rgb_strip.cpp:109` |
| `s_answer` | 1,064 | `COMMON_ZERO_INIT` | AXI | `src/drivers/gp_emulator/ps4/ps4_auth.cpp:161` |
| `s_slots` | 1,056 | `COMMON_ZERO_INIT` | AXI | `src/drivers/device/rgb_strip/rgb_strip.cpp:106` |
| `s_frame` | 1,028 | `COMMON_ZERO_INIT` | AXI | `src/thetagp_tasks.cpp:52` |
| `s_reply` | 1,024 | `COMMON_ZERO_INIT` | AXI | `src/thetagp_tasks.cpp:51` |
| `s_config_descriptor` | 1,024 | `COMMON_ZERO_INIT` | AXI | `src/drivers/gp_emulator/usb_driver.cpp:35` |
| `TaskManager::taskSlots` | 960 | `FAST_DATA_ZERO_INIT` | DTCM | `src/task_manager.cpp:39` |
| `s_flashSpiTxBuf` | 512 | `COMMON_ZERO_INIT` | AXI | `src/drivers/device/flash/flash_w25qxx.cpp:42` |
| `s_flashSpiRxBuf` | 512 | `COMMON_ZERO_INIT` | AXI | `src/drivers/device/flash/flash_w25qxx.cpp:43` |
| `TaskManager::attrSlots` | 384 | plain static | DTCM | `src/task_manager.cpp:40` |
| `s_logTxBuf` | 256 | `COMMON_ZERO_INIT` | AXI | `src/drivers/device/logger.cpp:34` |
| `s_logRxBuf` | 256 | `COMMON_ZERO_INIT` | AXI | `src/drivers/device/logger.cpp:35` |
| `USBDriver::s_cdc_buffer` | 256 | `COMMON_ZERO_INIT` | AXI | `src/drivers/gp_emulator/usb_driver.cpp:37` |
| `s_challenge` | 256 | plain static | DTCM | `src/drivers/gp_emulator/ps4/ps4_auth.cpp:162` |
| `descriptorStringBuffer` (×4) | 1,024 | plain static, function-local | DTCM | `src/drivers/gp_emulator/shared/driver_helper.h:33` |

Totals: AXI SRAM 24,574 B, DTCMRAM 7,780 B.

`getStringDescriptor()` is a `static inline` in a shared header with a
function-local buffer, so each emulator translation unit that calls it carries its
own copy: four rows of 256 B for one logical buffer.

## Third-party buffers the scan also finds

Listed so the scan's output is accounted for, not owned by this tree: neither row
is a buffer this project may resize, and both are outside the totals above.

| buffer | size | region | owner |
|---|---:|---|---|
| `_sram_heap` | 16,384 | AXI | newlib heap |
| `_suser_stack` | 8,192 | DTCM | newlib stack |
| `g_ring` | 1,664 | AXI | TinyUSB CDC ring |
| `_cdcd_itf` | 1,148 | DTCM | TinyUSB CDC interface |
| `__sf` | 312 | DTCM | newlib `FILE` |
| `xfer_status` | 288 | DTCM | TinyUSB |
| `_on_exit_args_instance` | 264 | DTCM | newlib |
| `buffer.0` | 256 | AXI | newlib printf buffer |
