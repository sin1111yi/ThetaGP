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

#include "device/usbd_pvt.h"
#include "tusb.h"

#include <cstdint>

namespace ThetaGP::USB {

class USBCore {
public:
  static USBCore &getInstance() {
    static USBCore instance;
    return instance;
  }

  void init();

  const usbd_class_driver_t *getDrivers(uint8_t *count);
  const uint8_t *getConfigurationDescriptor(uint8_t index);

  void cdcRx(uint8_t itf);

private:
  USBCore() = default;

  usbd_class_driver_t _mode_driver;
  usbd_class_driver_t _cdc_driver;
};

} // namespace ThetaGP::USB
