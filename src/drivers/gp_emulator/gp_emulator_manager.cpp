/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "drivers/gp_emulator/gp_emulator_manager.h"
#include "drivers/gp_emulator/hid/hid_driver.h"
#include "drivers/gp_emulator/ps4/ps4_driver.h"
#include "drivers/gp_emulator/xinput/xinput_driver.h"
#include "drivers/gp_emulator/xbone/xbone_driver.h"

#include "gamepad/config/config_manager.h"
#include "pb/enums.pb.h"
#include "drivers/gp_emulator/usb_driver.h"

#include "tusb.h"

namespace ThetaGP::Drivers::GPEmulator {

Enums::InputMode GPEmulatorManager::inputModeFromConfig() {
  const uint8_t asked =
      Gamepad::Config::ConfigManager::getInstance().config().usb.input_mode;
  return static_cast<Enums::InputMode>(asked);
}

void GPEmulatorManager::setup(Enums::InputMode mode) {
  inputMode = mode;

  // The configuration mode carries no device: the board is answered by the
  // store alone, and no gamepad interface is offered.
  if (mode == Enums::InputMode::INPUT_MODE_CONFIG) {
    return;
  }

  switch (mode) {
  case Enums::InputMode::INPUT_MODE_PS4:
    emulator = new PS4Driver();
    break;
  case Enums::InputMode::INPUT_MODE_XINPUT:
    emulator = new XInputDriver();
    break;
  case Enums::InputMode::INPUT_MODE_XBONE:
    emulator = new XboxOneDriver();
    break;
  default:
    // Every other declared mode is answered by the generic HID device.
    emulator = new HIDDriver();
    break;
  }

  emulator->initialize();

  USB::USBDriver::getInstance().init();

  // TinyUSB initialize
  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE,
#if THETAGP_USB_HIGH_SPEED
                                 .speed = TUSB_SPEED_HIGH
#else
                                 .speed = TUSB_SPEED_FULL
#endif
  };

  // The DWC2 settings belong to whichever port the board declares: the full
  // speed core and the high-speed one are configured the same way.
  tud_configure_dwc2_t cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
  tud_configure(THETAGP_USB_RHPORT, TUD_CFGID_DWC2, &cfg);

  tusb_init(THETAGP_USB_RHPORT, &dev_init);
}

} // namespace ThetaGP::Drivers::GPEmulator
