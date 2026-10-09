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

#include "drivers/gp_emulator/gp_emulator.h"
#include "drivers/gp_emulator/ps4/ps4_descriptors.h"

#include "class/hid/hid.h"
#include "device/usbd_pvt.h"

namespace ThetaGP::Drivers::GPEmulator {

// The DualShock 4 the device reports as. The report it answers with is the
// gamepad's own state; the output reports a host sends it reads and does not
// act on, because this board carries no motor and no lightbar of the device's
// kind. The authentication reports are answered as an unauthenticated device:
// a host that does not ask for a signature reads the same controller either
// way, and the listener a fitted authentication would arrive through is the
// base class's own hook.
class PS4Driver : public GPEmulator {
public:
  PS4Driver();
  void initialize() override;
  bool process(void *gamepad) override;
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
  PS4Report report = {};
  uint16_t timestamp = 0;
  uint8_t counter = 0;
};

} // namespace ThetaGP::Drivers::GPEmulator
