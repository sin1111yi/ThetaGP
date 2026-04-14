#pragma once

#include "drivers/device/gpinput/hid/HIDDescriptors.h"
#include "drivers/device/gpinput/gpdriver.h"

#include "class/hid/hid.h"
#include "device/usbd_pvt.h"

namespace ThetaGP::Drivers::Device {

class HIDDriver : public GPInputUSBDevice {
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
  const uint8_t *get_descriptor_configuration_cb(uint8_t index) override;
  const uint8_t *get_descriptor_device_qualifier_cb() override;
  uint16_t GetJoystickMidValue() override;
  USBListener *get_usb_auth_listener() override { return nullptr; }

private:
  uint8_t last_report[CFG_TUD_ENDPOINT0_SIZE] = {};
  HIDReport hidReport;
};

}  // namespace ThetaGP::Drivers::Device
