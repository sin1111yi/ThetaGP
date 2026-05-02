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

#pragma once

#include "drivers/gpdriver/gpdriver.h"
#include "drivers/gpdriver/hid/HIDDescriptors.h"

#include "class/hid/hid.h"
#include "device/usbd_pvt.h"

namespace ThetaGP::Drivers::GPDriver {

class HIDDriver : public GPDriver {
public:
  HIDDriver();

  void initialize() override;
  bool process(Gamepad *gamepad) override;
  void initializeAux() override {}

  uint16_t get_report(uint8_t report_id, hid_report_type_t report_type,
                      uint8_t *buffer, uint16_t reqlen) override;
  void set_report(uint8_t report_id, hid_report_type_t report_type,
                  uint8_t const *buffer, uint16_t bufsize) override;
  bool vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                              tusb_control_request_t const *request) override;
  const uint16_t *get_descriptor_string_cb(uint8_t index,
                                           uint16_t langid) override;
  const uint8_t *get_descriptor_device_cb() override;
  const uint8_t *get_hid_descriptor_report_cb(uint8_t itf) override;
  const uint8_t *get_interface_descriptor() override;
  uint16_t get_interface_descriptor_size() override;
  const uint8_t *get_descriptor_device_qualifier_cb() override;
  uint16_t GetJoystickMidValue() override;
  USBListener *get_usb_auth_listener() override { return nullptr; }

private:
  uint8_t last_report[CFG_TUD_ENDPOINT0_SIZE] = {};
  HIDReport hidReport;
};

} // namespace ThetaGP::Drivers::GPDriver
