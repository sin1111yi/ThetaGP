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

#ifndef _GPDRIVER_H_
#define _GPDRIVER_H_

#include "drivers/gpdriver/usblistener.h"

#include "class/hid/hid.h"
#include "device/usbd_pvt.h"
#include "tusb.h"

#include <cstdint>

namespace ThetaGP::Drivers::GPDriver {

class GPDriver {
public:
  virtual void initialize() = 0;
  virtual void initializeAux() = 0;
  virtual bool process(void *gamepad) = 0;
  virtual uint16_t get_report(uint8_t report_id, hid_report_type_t report_type,
                              uint8_t *buffer, uint16_t reqlen) = 0;
  virtual void set_report(uint8_t report_id, hid_report_type_t report_type,
                          uint8_t const *buffer, uint16_t bufsize) = 0;
  virtual bool
  vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                         tusb_control_request_t const *request) = 0;
  virtual const uint16_t *get_descriptor_string_cb(uint8_t index,
                                                   uint16_t langid) = 0;
  virtual const uint8_t *get_descriptor_device_cb() = 0;
  virtual const uint8_t *get_hid_descriptor_report_cb(uint8_t itf) = 0;
  virtual const uint8_t *get_interface_descriptor() = 0;
  virtual uint16_t get_interface_descriptor_size() = 0;
  virtual const uint8_t *get_descriptor_device_qualifier_cb() = 0;
  virtual uint16_t GetJoystickMidValue() = 0;
  virtual USBListener *get_usb_auth_listener() = 0;

  const usbd_class_driver_t *get_class_driver() { return &class_driver; }

  static uint32_t get_string_hash_u32(const char *str) {
    uint32_t h = 2166136261u;
    while (*str) {
      h ^= static_cast<uint8_t>(*str++);
      h *= 16777619u;
    }
    return h;
  }

protected:
  usbd_class_driver_t class_driver;
};

} // namespace ThetaGP::Drivers::GPDriver

#endif
