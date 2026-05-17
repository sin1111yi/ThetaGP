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

#include "build_info.h"
#include "drivers/device/devmem.h"

using namespace ThetaGP::Drivers::Device;

#define DEV_MEMPOOL_SIZE (2048 + 2048 + 16)

COMMON_CODE static uint8_t s_DevMempool[DEV_MEMPOOL_SIZE];

bool DevMem::init() {
  _poolId = ThetaGP::Mempool::MempoolManager::createPool(
      s_DevMempool, DEV_MEMPOOL_SIZE, "DevPool");
  if (_poolId == ThetaGP::Mempool::INVALID_POOL_ID) {
    return false;
  }
  _initialized = true;
  return true;
}
