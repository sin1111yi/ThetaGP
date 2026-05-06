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
#include <functional>

namespace ThetaGP::USB {

class USBDriver {
private:
  static constexpr uint16_t CDC_BUFFER_SIZE = 256;
  USBDriver() = default;

  usbd_class_driver_t _drivers[2];

  using CDCRxCallbackFunc = std::function<void(void *buffer, uint16_t len)>;
  CDCRxCallbackFunc _cdcRxCallback;

public:
  static USBDriver &getInstance() {
    static USBDriver instance;
    return instance;
  }

  void init();

  const usbd_class_driver_t *getDrivers(uint8_t *count);
  const uint8_t *getConfigurationDescriptor(uint8_t index);

  void setCDCRxCallback(CDCRxCallbackFunc cb);
  void cdcRxCallback(uint8_t itf) {
    UNUSED(itf);

    while (tud_cdc_available()) {
      static uint8_t buffer[CDC_BUFFER_SIZE];
      uint32_t len = tud_cdc_read(buffer, sizeof(buffer));
#if 0
      tud_cdc_write(buffer, len);
      tud_cdc_write_flush();
#endif
      if (_cdcRxCallback) {
        _cdcRxCallback(buffer, len);
      }
    }
  }
};

} // namespace ThetaGP::USB

extern "C" {
bool get_usb_mounted(void);
bool get_usb_suspended(void);
}
