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

#include "class/cdc/cdc_device.h"
#include "utils/utils.h"

#include "device/usbd_pvt.h"
#include "tusb.h"

#include <cstdint>

namespace ThetaGP::Drivers::GPEmulator::USB {

class USBDriver {
private:
  static constexpr uint16_t CDC_BUFFER_SIZE = 256;

  // The CDC read buffer, in AXI SRAM: the callback reads into it and hands it on.
  static COMMON_ZERO_INIT uint8_t s_cdc_buffer[CDC_BUFFER_SIZE];

  USBDriver() = default;

  usbd_class_driver_t _drivers[2];

  // The CDC receive callback, C-style so no std::function is needed.
  using CDCRxCallbackFunc = void (*)(void *buffer, uint16_t len);
  CDCRxCallbackFunc volatile _cdcRxCallback;

public:
  static USBDriver &getInstance() {
    static USBDriver instance;
    return instance;
  }

  void init();

  const usbd_class_driver_t *getDrivers(uint8_t *count);
  const uint8_t *getConfigurationDescriptor(uint8_t index);

  void setCDCRxCallback(CDCRxCallbackFunc cb) { _cdcRxCallback = cb; }
  void cdcRxCallback(uint8_t itf) {
    UNUSED(itf);

    while (tud_cdc_available()) {
      const uint32_t len = tud_cdc_read(s_cdc_buffer, sizeof s_cdc_buffer);
      if (_cdcRxCallback) {
        _cdcRxCallback(s_cdc_buffer, len);
      }
    }
  }
};

} // namespace ThetaGP::Drivers::GPEmulator::USB

extern "C" {
bool get_usb_mounted(void);
bool get_usb_suspended(void);
}
