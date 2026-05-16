/**
 * This file is a part of hitbox-mcu.
 *
 * hitbox-mcu is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * hitbox-mcu is distributed in the hope that it will be
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

#include "driver/device/flash.h"
#include "driver/mcu/bus.h"

#include <stdbool.h>

#define W25QXX_SECTOR_4K_ADDR(sector)                                          \
  ((uint32_t)((sector_num) * W25QXX_SECTOR_SIZE))
#define W25QXX_BLOCK_32K_ADDR(block)                                           \
  ((uint32_t)((block_num) * W25QXX_BLOCK_32K_SIZE))
#define W25QXX_BLOCK_64K_ADDR(block)                                           \
  ((uint32_t)((block_num) * W25QXX_BLOCK_64K_SIZE))

enum w25qxxType_e
{
  W25QXX_TYPE_W25Q10 = 0XEF10U,  /**< w25q10, 1Mb */
  W25QXX_TYPE_W25Q20 = 0XEF11U,  /**< w25q20, 2Mb */
  W25QXX_TYPE_W25Q40 = 0XEF12U,  /**< w25q40, 4Mb */
  W25QXX_TYPE_W25Q80 = 0XEF13U,  /**< w25q80, 8Mb */
  W25QXX_TYPE_W25Q16 = 0XEF14U,  /**< w25q16, 16Mb */
  W25QXX_TYPE_W25Q32 = 0XEF15U,  /**< w25q32, 32Mb */
  W25QXX_TYPE_W25Q64 = 0XEF16U,  /**< w25q64, 64Mb */
  W25QXX_TYPE_W25Q128 = 0XEF17U, /**< w25q128, 128Mb */
  W25QXX_TYPE_W25Q256 = 0XEF18U, /**< w25q256, 256Mb */
  W25QXX_TYPE_W25Q512 = 0XEF19U, /**< w25q512, 512Mb */
};

enum w25qxxAddrMode_e
{
  W25QXX_ADDR_MODE_3BYTE = 0,
  W25QXX_ADDR_MODE_4BYTE = 1,
};

void flashW25qxxReset (devFlash_t *flash);
uint32_t flashW25qxxReadChipId (devFlash_t *flash);
void flashW25qxxInit (devFlash_t *flash);

void flashW25qxxSet4ByteAddrMode (devFlash_t *flash, bool enable);
void flashW25qxxWriteEnable (devFlash_t *flash);
void flashW25qxxRead (devFlash_t *flash, uint32_t addr, uint8_t *data,
                      uint32_t len);
void flashW25qxxWrite (devFlash_t *flash, uint32_t addr, uint8_t *data,
                       uint32_t len);
void flashW25qxxEraseSector4K (devFlash_t *flash, uint32_t addr);
void flashW25qxxEraseBlock32K (devFlash_t *flash, uint32_t addr);
void flashW25qxxEraseBlock64K (devFlash_t *flash, uint32_t addr);
void flashW25qxxEraseChip (devFlash_t *flash);
