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

#include "drivers/gpdriver/usbdriver.h"
#include "build_info.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/gpdriver/hid/HIDDescriptors.h"

#include "class/cdc/cdc_device.h"
#include "tusb.h"

#include <cstring>

using namespace ThetaGP::USB;
using namespace ThetaGP::Drivers::GPDriver;

COMMON_CODE static uint8_t s_config_descriptor[1024];
static uint16_t s_config_size;

/* clang-format off */
static constexpr uint8_t CDC_IFACE[] = {
    0x09, 0x04,
    CDC_COM_INTERFACE,
    0x00, 0x01,
    0x02, 0x02,
    0x01, 0x04,
    0x05, 0x24,
   0x00, 0x10,
   0x01, 0x05,
   0x24, 0x01,
   0x03,
   CDC_DATA_INTERFACE,
   0x04, 0x24,
   0x02, 0x02,
   0x05, 0x24,
   0x06,
   CDC_COM_INTERFACE,
   CDC_DATA_INTERFACE,
   0x07, 0x05,
   CDC_NOTIFICATION_ENDPOINT | 0x80,
   0x03, 0x08,
   0x00, 0x10,
   0x09, 0x04,
   CDC_DATA_INTERFACE,
   0x00, 0x02,
   0x0A, 0x00,
   0x00, 0x00,
   0x07, 0x05,
   CDC_DATA_OUT_ENDPOINT,
   0x02, 0x00,
   0x02, 0x00,
   0x07, 0x05,
   CDC_DATA_IN_ENDPOINT | 0x80,
   0x02, 0x00,
   0x02, 0x00,
};
/* clang-format on */

constexpr uint16_t CDC_IFACE_SIZE = sizeof(CDC_IFACE);

void USBDriver::init() {
  uint8_t *p = s_config_descriptor;

  p[0] = 9;  p[1] = 2;  p[2] = 0;  p[3] = 0;
  p[4] = 3;  p[5] = 1;  p[6] = 0;  p[7] = 0x80;  p[8] = 50;
  p += 9;

  auto *driver = GPDriverManager::getInstance().getgpdriverDevice();
  uint16_t mode_size = driver->get_interface_descriptor_size();
  std::memcpy(p, driver->get_interface_descriptor(), mode_size);
  p += mode_size;

  std::memcpy(p, CDC_IFACE, CDC_IFACE_SIZE);
  p += CDC_IFACE_SIZE;

  s_config_size = static_cast<uint16_t>(p - s_config_descriptor);
  s_config_descriptor[2] = LSB(s_config_size);
  s_config_descriptor[3] = MSB(s_config_size);

  _mode_driver = *driver->get_class_driver();

  _cdc_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "CDC",
#endif
    .init = cdcd_init,
    .deinit = NULL,
    .reset = cdcd_reset,
    .open = cdcd_open,
    .control_xfer_cb = cdcd_control_xfer_cb,
    .xfer_cb = cdcd_xfer_cb,
    .xfer_isr = NULL,
    .sof = NULL,
  };
}

const usbd_class_driver_t *USBDriver::getDrivers(uint8_t *count) {
  *count = 2;
  return &_mode_driver;
}

const uint8_t *USBDriver::getConfigurationDescriptor(uint8_t index) {
  (void)index;
  return s_config_descriptor;
}

// ------------------------------------------------------------------- //

extern "C" {

static bool usb_mounted;
static bool usb_suspended;

bool get_usb_mounted(void) { return usb_mounted; }

bool get_usb_suspended(void) { return usb_suspended; }

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
  return USBDriver::getInstance().getDrivers(driver_count);
}

void tud_cdc_rx_cb(uint8_t itf) {
  USBDriver::getInstance().cdcRxCallback(itf);
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  UNUSED(itf);
  return GPDriverManager::getInstance().getgpdriverDevice()->get_report(
      report_id, report_type, buffer, reqlen);
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  UNUSED(itf);
  GPDriverManager::getInstance().getgpdriverDevice()->set_report(
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
  return GPDriverManager::getInstance()
      .getgpdriverDevice()
      ->vendor_control_xfer_cb(rhport, stage, request);
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  return GPDriverManager::getInstance()
      .getgpdriverDevice()
      ->get_descriptor_string_cb(index, langid);
}

uint8_t const *tud_descriptor_device_cb() {
  return GPDriverManager::getInstance()
      .getgpdriverDevice()
      ->get_descriptor_device_cb();
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
  return GPDriverManager::getInstance()
      .getgpdriverDevice()
      ->get_hid_descriptor_report_cb(itf);
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  return USBDriver::getInstance().getConfigurationDescriptor(index);
}

uint8_t const *tud_descriptor_device_qualifier_cb() {
  return GPDriverManager::getInstance()
      .getgpdriverDevice()
      ->get_descriptor_device_qualifier_cb();
}

}
