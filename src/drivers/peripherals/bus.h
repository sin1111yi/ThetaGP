/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "utils/types.h"

#include "build_info.h"

namespace ThetaGP {
namespace Drivers {
namespace Periph {
namespace Bus {

enum class Type { Uart, Spi, I2c };
enum class Mode { Polling, Interrupt, DirectMemAccess };

class Bus {
protected:
  Type _type;
  Mode _mode = Mode::Polling;

public:
  Bus();

  void setType(Type type) { _type = type; }
  void setMode(Mode mode) { _mode = mode; }
  virtual void init() = 0;
  virtual void enable() = 0;

  virtual retval_t write(uint8_t byte) = 0;             // byte write
  virtual retval_t write(uint8_t *bytes, uint16_t num); // bytes write

  virtual retval_t read(uint8_t *byte) = 0;            // byte read
  virtual retval_t read(uint8_t *bytes, uint16_t num); // bytes read
};

} // namespace Bus
} // namespace Periph
} // namespace Drivers
} // namespace ThetaGP
