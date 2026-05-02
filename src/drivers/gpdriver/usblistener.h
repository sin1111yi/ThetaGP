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

#ifndef _USBLISTENER_H_
#define _USBLISTENER_H_

#include <cstdint>

namespace ThetaGP::Drivers::GPDriver {

class USBListener {
public:
  virtual void setup() = 0;
  virtual void mount(uint8_t dev_addr, uint8_t instance,
                     uint8_t const *desc_report, uint16_t desc_len) = 0;
  virtual void xmount(uint8_t dev_addr, uint8_t instance,
                      uint8_t controllerType, uint8_t subtype) = 0;
  virtual void unmount(uint8_t dev_addr) = 0;
  virtual void report_received(uint8_t dev_addr, uint8_t instance,
                               uint8_t const *report, uint16_t len) = 0;
  virtual void report_sent(uint8_t dev_addr, uint8_t instance,
                           uint8_t const *report, uint16_t len) = 0;
  virtual void set_report_complete(uint8_t dev_addr, uint8_t instance,
                                   uint8_t report_id, uint8_t report_type,
                                   uint16_t len) = 0;
  virtual void get_report_complete(uint8_t dev_addr, uint8_t instance,
                                   uint8_t report_id, uint8_t report_type,
                                   uint16_t len) = 0;
};

} // namespace ThetaGP::Drivers::GPDriver

#endif
