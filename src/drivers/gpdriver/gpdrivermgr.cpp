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
#include "drivers/gpdriver/usbcore.h"

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

  USB::USBCore::getInstance().init();

  // TinyUSB initialize
  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE,
#if defined(THETAGP_CFG_USB_HS)
                                 .speed = TUSB_SPEED_HIGH
#else
                                 .speed = TUSB_SPEED_FULL
#endif
  };
  tusb_init(1, &dev_init);
}

} // namespace ThetaGP::Drivers::GPDriver
