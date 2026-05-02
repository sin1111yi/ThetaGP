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
#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/gpdriver/usbcore.h"

#include "tusb.h"

using namespace ThetaGP::USB;
using namespace ThetaGP::Drivers::GPDriver;

extern "C" {

static bool usb_mounted;
static bool usb_suspended;

bool get_usb_mounted(void) { return usb_mounted; }

bool get_usb_suspended(void) { return usb_suspended; }

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *driver_count) {
  return USBCore::getInstance().getDrivers(driver_count);
}

void tud_cdc_rx_cb(uint8_t itf) {
  USBCore::getInstance().cdcRx(itf);
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
  return USBCore::getInstance().getConfigurationDescriptor(index);
}

uint8_t const *tud_descriptor_device_qualifier_cb() {
  return GPDriverManager::getInstance()
      .getgpdriverDevice()
      ->get_descriptor_device_qualifier_cb();
}
}
