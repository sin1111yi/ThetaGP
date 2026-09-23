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
#include "drivers/gp_emulator/usb_driver.h"

#include "tusb.h"

namespace ThetaGP::Drivers::GPEmulator {

void GPEmulatorManager::setup(InputMode mode) {
  switch (mode) {
  case InputMode::HID:
    emulator = new HIDDriver();
    break;
  default:
    return;
  }

  if (emulator != nullptr) {
    emulator->initialize();
  }
  inputMode = mode;

  USB::USBDriver::getInstance().init();

  // TinyUSB initialize
  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE,
#if THETAGP_USB_HIGH_SPEED
                                 .speed = TUSB_SPEED_HIGH
#else
                                 .speed = TUSB_SPEED_FULL
#endif
  };

#if THETAGP_USB_RHPORT == 0
  tud_configure_dwc2_t cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
  tud_configure(0, TUD_CFGID_DWC2, &cfg);
#endif

  tusb_init(THETAGP_USB_RHPORT, &dev_init);
}

} // namespace ThetaGP::Drivers::GPEmulator
