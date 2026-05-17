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

#include "utils/mempool/mempoolmanager.h"
#include <cstdint>

namespace ThetaGP {
namespace Drivers {
namespace Device {

class DevMem {
public:
  static DevMem &getInstance() {
    static DevMem instance;
    return instance;
  }

  bool init();
  Mempool::PoolID poolId() const { return _poolId; }

private:
  DevMem() = default;
  bool _initialized = false;
  Mempool::PoolID _poolId = Mempool::INVALID_POOL_ID;
};

} // namespace Device
} // namespace Drivers
} // namespace ThetaGP
