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

#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/gpdriver/hid/HIDDriver.h"
#include "drivers/gpdriver/usbdriver.h"

#include "tusb.h"

namespace ThetaGP::Drivers::GPDriver {

void GPDriverManager::setup(InputMode mode) {
  switch (mode) {
  case InputMode::HID:
    usbdevice = new HIDDriver();
    break;
  default:
    return;
  }

  if (usbdevice != nullptr) {
    usbdevice->initialize();
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

} // namespace ThetaGP::Drivers::GPDriver
