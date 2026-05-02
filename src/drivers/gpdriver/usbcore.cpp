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

#include "drivers/gpdriver/usbcore.h"
#include "build_info.h"
#include "drivers/gpdriver/gpdrivermgr.h"
#include "drivers/gpdriver/hid/HIDDescriptors.h"

#include "class/cdc/cdc_device.h"
#include "device/usbd_pvt.h"
#include "tusb.h"

#include <cstring>

using namespace ThetaGP::USB;
using namespace ThetaGP::Drivers::GPDriver;

COMMON_CODE static uint8_t s_config_descriptor[1024];
static uint16_t s_config_size;

static constexpr uint8_t CDC_IFACE[] = {
    // CDC Communication Control interface
    9,                           // bLength
    4,                           // bDescriptorType
    CDC_COM_INTERFACE,           // bInterfaceNumber
    0,                           // bAlternateSetting
    1,                           // bNumEndpoints
    0x02,                        // bInterfaceClass (CDC)
    0x02,                        // bInterfaceSubClass (Abstract Control Model)
    0x01,                        // bInterfaceProtocol (AT commands V.250)
    4,                           // iInterface
    // CDC functional descriptor (Header Functional)
    5,                           // bLength
    0x24,                        // bDescriptorType (CS_INTERFACE)
    0x00,                        // bDescriptorSubtype (Header Functional)
    0x10, 0x01,                  // bcdCDC (1.10)
    // CDC Call Management Functional Descriptor
    5,                           // bLength
    0x24,                        // bDescriptorType (CS_INTERFACE)
    0x01,                        // bDescriptorSubtype (Call Management)
    0x03,                        // bmCapabilities
    CDC_DATA_INTERFACE,          // bDataInterface
    // CDC Abstract Control Management Functional Descriptor
    4,                           // bLength
    0x24,                        // bDescriptorType (CS_INTERFACE)
    0x02,                        // bDescriptorSubtype (Abstract Control Management)
    0x02,                        // bmCapabilities
    // CDC Union Functional Descriptor
    5,                           // bLength
    0x24,                        // bDescriptorType (CS_INTERFACE)
    0x06,                        // bDescriptorSubtype (Union)
    CDC_COM_INTERFACE,           // bMasterInterface
    CDC_DATA_INTERFACE,          // bSlaveInterface
    // CDC notification endpoint IN (interrupt)
    7,                                // bLength
    5,                                // bDescriptorType
    CDC_NOTIFICATION_ENDPOINT | 0x80, // bEndpointAddress
    0x03,                             // bmAttributes (intr)
    8, 0,                             // wMaxPacketSize
    16,                               // bInterval
    // CDC Data interface
    9,                           // bLength
    4,                           // bDescriptorType
    CDC_DATA_INTERFACE,          // bInterfaceNumber
    0,                           // bAlternateSetting
    2,                           // bNumEndpoints
    0x0A,                        // bInterfaceClass (CDC Data)
    0x00,                        // bInterfaceSubClass
    0x00,                        // bInterfaceProtocol
    0,                           // iInterface
    // CDC data endpoint OUT (bulk)
    7,                              // bLength
    5,                              // bDescriptorType
    CDC_DATA_OUT_ENDPOINT,          // bEndpointAddress (OUT)
    0x02,                           // bmAttributes (bulk)
    0x00, 0x02,                     // wMaxPacketSize (512)
    0,                              // bInterval
    // CDC data endpoint IN (bulk)
    7,                             // bLength
    5,                             // bDescriptorType
    CDC_DATA_IN_ENDPOINT | 0x80,   // bEndpointAddress (IN)
    0x02,                          // bmAttributes (bulk)
    0x00, 0x02,                    // wMaxPacketSize (512)
    0,                             // bInterval
};

constexpr uint16_t CDC_IFACE_SIZE = sizeof(CDC_IFACE);
constexpr uint16_t CONFIG_HEADER_SIZE = 9;

void USBCore::init() {
  // Build configuration descriptor
  uint8_t *p = s_config_descriptor;

  // Configuration header
  // size filled later
  p[0] = 9;    // bLength
  p[1] = 2;    // bDescriptorType
  p[2] = 0;    // wTotalLength (LSB)
  p[3] = 0;    // wTotalLength (MSB)
  p[4] = 3;    // bNumInterfaces
  p[5] = 1;    // bConfigurationValue
  p[6] = 0;    // iConfiguration
  p[7] = 0x80; // bmAttributes
  p[8] = 50;   // bMaxPower
  p += 9;

  // Mode interface descriptor
  auto *driver = GPDriverManager::getInstance().getgpdriverDevice();
  uint16_t mode_size = driver->get_interface_descriptor_size();
  std::memcpy(p, driver->get_interface_descriptor(), mode_size);
  p += mode_size;

  // CDC interfaces
  std::memcpy(p, CDC_IFACE, CDC_IFACE_SIZE);
  p += CDC_IFACE_SIZE;

  s_config_size = static_cast<uint16_t>(p - s_config_descriptor);
  s_config_descriptor[2] = LSB(s_config_size);
  s_config_descriptor[3] = MSB(s_config_size);

  // Store mode driver
  _mode_driver = *driver->get_class_driver();

  // Store CDC driver
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

const usbd_class_driver_t *USBCore::getDrivers(uint8_t *count) {
  *count = 2;
  return &_mode_driver;
}

const uint8_t *USBCore::getConfigurationDescriptor(uint8_t index) {
  (void)index;
  return s_config_descriptor;
}

void USBCore::cdcRx(uint8_t itf) {
  (void)itf;
  while (tud_cdc_available()) {
    uint8_t buf[64];
    uint32_t len = tud_cdc_read(buf, sizeof(buf));
    tud_cdc_write(buf, len);
    tud_cdc_write_flush();
  }
}
