#include "drivers/device/gpinput/usbdriver.h"
#include "drivers/device/gpinput/gp_input_device.h"

#include "tusb.h"

using namespace ThetaGP::Drivers::Device;

extern "C" {

static bool usb_mounted;
static bool usb_suspended;

bool get_usb_mounted(void) { return usb_mounted; }

bool get_usb_suspended(void) { return usb_suspended; }

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1;
  return GPInputDevice::getInstance().getGPUsbDevice()->get_class_driver();
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  UNUSED(itf);
  return GPInputDevice::getInstance().getGPUsbDevice()->get_report(
      report_id, report_type, buffer, reqlen);
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
  UNUSED(itf);
  GPInputDevice::getInstance().getGPUsbDevice()->set_report(
      report_id, report_type, buffer, bufsize);
}

void tud_mount_cb(void) {
  usb_mounted = true;
  usb_suspended = false;
}

void tud_umount_cb(void) {
  usb_mounted = false;
  usb_suspended = false;
}

void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  usb_suspended = true;
}

void tud_resume_cb(void) { usb_suspended = false; }

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
  return GPInputDevice::getInstance()
      .getGPUsbDevice()
      ->vendor_control_xfer_cb(rhport, stage, request);
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  return GPInputDevice::getInstance()
      .getGPUsbDevice()
      ->get_descriptor_string_cb(index, langid);
}

uint8_t const *tud_descriptor_device_cb() {
  return GPInputDevice::getInstance().getGPUsbDevice()->get_descriptor_device_cb();
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
  return GPInputDevice::getInstance()
      .getGPUsbDevice()
      ->get_hid_descriptor_report_cb(itf);
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  return GPInputDevice::getInstance()
      .getGPUsbDevice()
      ->get_descriptor_configuration_cb(index);
}

uint8_t const *tud_descriptor_device_qualifier_cb() {
  return GPInputDevice::getInstance()
      .getGPUsbDevice()
      ->get_descriptor_device_qualifier_cb();
}

}
