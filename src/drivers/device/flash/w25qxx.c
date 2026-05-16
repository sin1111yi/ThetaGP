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

#include "driver/device/flash/w25qxx.h"

#include "driver/mcu/mcu.h"

/* clang-format off */

/**
 * @brief W25QXX Instruction Set Table(Standard SPI Instructions)
 *
 * @addgroup W25QXX_Instruction_Set_Table
 * @{
 */
#define W25QXX_I_WRITE_EN                 (uint8_t)(0x06U) /**< Write Enable */
#define W25QXX_I_VSR_WRITE_EN             (uint8_t)(0x50U) /**< Volatile SR Write Enable */
#define W25QXX_I_WRITE_DIS                (uint8_t)(0x04U) /**< Write Disable */

#define W25QXX_I_RELEASE_PD               (uint8_t)(0xABU) /**< Release Power Down */
#define W25QXX_I_MANUF_DEV_ID             (uint8_t)(0x90U) /**< Manufacturer/Device ID */
#define W25QXX_I_JEDEC_ID                 (uint8_t)(0x9FU) /**< JEDEC ID */
#define W25QXX_I_READ_UNIQ_ID             (uint8_t)(0x4BU) /**< Read Unique ID */

#define W25QXX_I_READ_DATA                (uint8_t)(0x03U) /**< Read Data */
#define W25QXX_I_READ_DATA_4B             (uint8_t)(0x13U) /**< Read Data 4Byte Address */
#define W25QXX_I_FAST_READ                (uint8_t)(0x0BU) /**< Fast Read */
#define W25QXX_I_FAST_READ_4B             (uint8_t)(0x0CU) /**< Fast Read 4Byte Address */

#define W25QXX_I_PAGE_PGM                 (uint8_t)(0x02U) /**< Page Program */
#define W25QXX_I_PAGE_PGM_4B              (uint8_t)(0x12U) /**< Page Program 4Byte Address */

#define W25QXX_SECTOR_ERASE_4K            (uint8_t)(0x20U) /**< Sector Erase 4KB */
#define W25QXX_SECTOR_ERASE_4K_4B         (uint8_t)(0x21U) /**< Sector Erase 4KB 4Byte Address */
#define W25QXX_BLOCK_ERASE_32K            (uint8_t)(0x52U) /**< Block Erase 32KB */
#define W25QXX_BLOCK_ERASE_64K            (uint8_t)(0xD8U) /**< Block Erase 64KB */
#define W25QXX_BLOCK_ERASE_64K_4B         (uint8_t)(0xDCU) /**< Block Erase 64KB 4Byte Address */
#define W25QXX_CHIP_ERASE                 (uint8_t)(0xC7U) /**< Chip Erase */

#define W25QXX_I_READ_SR1                 (uint8_t)(0x05U) /**< Read Status Register-1 */
#define W25QXX_I_READ_SR2                 (uint8_t)(0x35U) /**< Read Status Register-2 */
#define W25QXX_I_READ_SR3                 (uint8_t)(0x15U) /**< Read Status Register-3 */
#define W25QXX_I_WRITE_SR1                (uint8_t)(0x01U) /**< Write Status Register-1 */
#define W25QXX_I_WRITE_SR2                (uint8_t)(0x31U) /**< Write Status Register-2 */
#define W25QXX_I_WRITE_SR3                (uint8_t)(0x11U) /**< Write Status Register-3 */

#define W25QXX_I_READ_SFDP_REG            (uint8_t)(0x5AU) /**< Read SFDP Register */
#define W25QXX_I_ERASE_SECURITY_REG       (uint8_t)(0x44U) /**< Erase Security Register */
#define W25QXX_I_PROGRAM_SECURITY_REG     (uint8_t)(0x42U) /**< Program Security Register */
#define W25QXX_I_READ_SECURITY_REG        (uint8_t)(0x48U) /**< Read Security Register */

#define W25QXX_I_GLOBAL_BLOCK_LOCK        (uint8_t)(0x7EU) /**< Global Block Protect Lock */
#define W25QXX_I_GLOBAL_BLOCK_UNLOCK      (uint8_t)(0x98U) /**< Global Block Protect Unlock */
#define W25QXX_I_READ_BLOCK_LOCK          (uint8_t)(0x3DU) /**< Read Block Protect Lock */
#define W25QXX_I_INDIVIDUAL_BLOCK_LOCK    (uint8_t)(0x36U) /**< Individual Block Protect Lock */
#define W25QXX_I_INDIVIDUAL_BLOCK_UNLOCK  (uint8_t)(0x39U) /**< Individual Block Protect Unlock */

#define W25QXX_I_ERASE_PGM_SUSPEND        (uint8_t)(0x75U) /**< Program Suspend */
#define W25QXX_I_ERASE_PGM_RESUME         (uint8_t)(0x7AU) /**< Program Resume */
#define W25QXX_I_POWER_DOWN               (uint8_t)(0xB9U) /**< Power Down */

#define W25QXX_I_ENTER_4B_ADDR_MODE       (uint8_t)(0xB7U) /**< Enter 4-Byte Address Mode */
#define W25QXX_I_EXIT_4B_ADDR_MODE        (uint8_t)(0xE9U) /**< Exit 4-Byte Address Mode */

#define W25QXX_I_ENABLE_RESET             (uint8_t)(0x66U) /**< Enable Reset */
#define W25QXX_I_RESET_DEVICE             (uint8_t)(0x99U) /**< Reset Device */
/**
 * @}
 */

#define W25QXX_DUMMY_BYTE     (uint8_t)(0x00U) /**< Dummy Byte */

#define W25QXX_SR_1           (1U) /**< Status Register-1 Index */
#define W25QXX_SR_2           (2U) /**< Status Register-2 Index */
#define W25QXX_SR_3           (3U) /**< Status Register-3 Index */

#define W25QXX_SR1_BUSY       (uint8_t)(0x01U) /**< Busy */
#define W25QXX_SR1_WEL        (uint8_t)(0x02U) /**< Write Enable Latch */
#define W25QXX_SR1_BP0        (uint8_t)(0x04U) /**< Block Protect Bit 0 */
#define W25QXX_SR1_BP1        (uint8_t)(0x08U) /**< Block Protect Bit 1 */
#define W25QXX_SR1_BP2        (uint8_t)(0x10U) /**< Block Protect Bit 2 */
#define W25QXX_SR1_TB         (uint8_t)(0x20U) /**< Top/Bottom Protect */
#define W25QXX_SR1_SEC        (uint8_t)(0x40U) /**< Sector/Block Protect */
#define W25QXX_SR1_SRP0       (uint8_t)(0x80U) /**< Status Register Protect 0 */

#define W25QXX_SR2_SRP1       (uint8_t)(0x01U) /**< Status Register Protect 1 */
#define W25QXX_SR2_QE         (uint8_t)(0x02U) /**< Quad Enable */
#define W25QXX_SR2_LB1        (uint8_t)(0x04U) /**< LB1 */
#define W25QXX_SR2_LB2        (uint8_t)(0x08U) /**< LB2 */
#define W25QXX_SR2_LB3        (uint8_t)(0x10U) /**< LB3 */
#define W25QXX_SR2_CMP        (uint8_t)(0x20U) /**< Complement Protect */
#define W25QXX_SR2_SUS        (uint8_t)(0x40U) /**< Suspend Status */

#define W25QXX_SR3_WPS        (uint8_t)(0x04U) /**< Write Protect Selection */
#define W25QXX_SR3_DRV0       (uint8_t)(0x20U) /**< Output Driver Strength 0 */
#define W25QXX_SR3_DRV1       (uint8_t)(0x40U) /**< Output Driver Strength 1 */
#define W25QXX_SR3_HD_RST     (uint8_t)(0x80U) /**< /HOLD or /RESET function */

/* clang-format on */

static uint8_t
w25qxxReadStatusRegX (devFlash_t *flash, uint8_t idx)
{
  uint8_t cmd[1] = { 0 };
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  switch (idx)
    {
    case W25QXX_SR_1:
      cmd[0] = W25QXX_I_READ_SR1;
      break;
    case W25QXX_SR_2:
      cmd[0] = W25QXX_I_READ_SR2;
      break;
    case W25QXX_SR_3:
      cmd[0] = W25QXX_I_READ_SR3;
      break;
    default:
      return 0;
    }

  uint8_t status = 0x00U;

  mcu_drv->bus.enable (flash->busRes);
  mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                      sizeof (cmd));
  mcu_drv->bus.read (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, &status, 1);
  mcu_drv->bus.disable (flash->busRes);

  return status;
}

void
flashW25qxxReset (devFlash_t *flash)
{
  uint8_t cmd[] = {
    W25QXX_I_ENABLE_RESET,
    W25QXX_I_RESET_DEVICE,
  };
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  mcu_drv->bus.enable (flash->busRes);
  mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                      sizeof (cmd));
  mcu_drv->bus.disable (flash->busRes);
}

uint32_t
flashW25qxxReadChipId (devFlash_t *flash)
{
  uint8_t cmd[] = {
    W25QXX_I_MANUF_DEV_ID,
    W25QXX_DUMMY_BYTE,
    W25QXX_DUMMY_BYTE,
    W25QXX_DUMMY_BYTE,
  };
  uint32_t chipId = 0x0000;
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  mcu_drv->bus.enable (flash->busRes);
  mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd, 4);
  mcu_drv->bus.read (flash->busRes, IGNORE_PARAM, IGNORE_PARAM,
                     (uint8_t *)&chipId, 2);
  mcu_drv->bus.disable (flash->busRes);

  return chipId;
}

void
flashW25qxxInit (devFlash_t *flash)
{
  uint32_t chipId = 0x00;

  flash->type = DEV_FLASH_TYPE_W25QXX;
  flash->status = DEV_FLASH_STATUS_OK;
  flash->addr_mode = W25QXX_ADDR_MODE_3BYTE;

  flashW25qxxReset (flash);
  delay_ms (10);
  chipId = flashW25qxxReadChipId (flash);
  chipId = SWAPI32 (chipId) >> 16;

  switch (chipId)
    {
    case W25QXX_TYPE_W25Q10:
      flash->size_Mb = 10;
      break;
    case W25QXX_TYPE_W25Q20:
      flash->size_Mb = 20;
      break;
    case W25QXX_TYPE_W25Q40:
      flash->size_Mb = 40;
      break;
    case W25QXX_TYPE_W25Q80:
      flash->size_Mb = 80;
      break;
    case W25QXX_TYPE_W25Q16:
      flash->size_Mb = 16;
      break;
    case W25QXX_TYPE_W25Q32:
      flash->size_Mb = 32;
      break;
    case W25QXX_TYPE_W25Q64:
      flash->size_Mb = 64;
      break;
    case W25QXX_TYPE_W25Q128:
      flash->size_Mb = 128;
      break;
    case W25QXX_TYPE_W25Q256:
      flash->size_Mb = 256;
      flashW25qxxSet4ByteAddrMode (flash, true);
      break;
    case W25QXX_TYPE_W25Q512:
      flash->size_Mb = 512;
      flashW25qxxSet4ByteAddrMode (flash, true);
      break;
    default:
      flash->size_Mb = KIE;
      flash->status = DEV_FLASH_STATUS_ERROR;
      break;
    }

  if (flash->size_Mb != KIE)
    {
      flash->eaddr = EADDR (flash->size_Mb);
      flash->status = DEV_FLASH_STATUS_OK;
    }
  else
    {
      flash->eaddr = KIE;
      flash->status = DEV_FLASH_STATUS_ERROR;
    }
}

void
flashW25qxxSet4ByteAddrMode (devFlash_t *flash, bool enable)
{
  uint8_t cmd[1] = { 0 };
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  if (enable)
    {
      cmd[0] = W25QXX_I_ENTER_4B_ADDR_MODE;
      flash->addr_mode = W25QXX_ADDR_MODE_4BYTE;
    }
  else
    {
      cmd[0] = W25QXX_I_EXIT_4B_ADDR_MODE;
      flash->addr_mode = W25QXX_ADDR_MODE_3BYTE;
    }

  mcu_drv->bus.enable (flash->busRes);
  mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                      sizeof (cmd));
  mcu_drv->bus.disable (flash->busRes);
}

void
flashW25qxxWriteEnable (devFlash_t *flash)
{
  uint8_t cmd[] = { W25QXX_I_WRITE_EN };
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  mcu_drv->bus.enable (flash->busRes);
  mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                      sizeof (cmd));
  mcu_drv->bus.disable (flash->busRes);

  while (0 != (w25qxxReadStatusRegX (flash, W25QXX_SR_1) & W25QXX_SR1_BUSY))
    ;
}

void
flashW25qxxRead (devFlash_t *flash, uint32_t addr, uint8_t *data, uint32_t len)
{
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  if (len == 0 || data == NULL)
    return;

  if (addr > flash->eaddr)
    {
      flash->status = DEV_FLASH_STATUS_ERROR;
      return;
    }

  if (flash->addr_mode == W25QXX_ADDR_MODE_4BYTE)
    {
      uint8_t cmd[5]
          = { W25QXX_I_READ_DATA_4B, (uint8_t)((addr >> 24) & 0xFF),
              (uint8_t)((addr >> 16) & 0xFF), (uint8_t)((addr >> 8) & 0xFF),
              (uint8_t)(addr & 0xFF) };

      mcu_drv->bus.enable (flash->busRes);
      mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                          sizeof (cmd));
      mcu_drv->bus.read (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, data, len);
      mcu_drv->bus.disable (flash->busRes);
    }
  else
    {
      uint8_t cmd[4]
          = { W25QXX_I_READ_DATA, (uint8_t)((addr >> 16) & 0xFF),
              (uint8_t)((addr >> 8) & 0xFF), (uint8_t)(addr & 0xFF) };

      mcu_drv->bus.enable (flash->busRes);
      mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                          sizeof (cmd));
      mcu_drv->bus.read (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, data, len);
      mcu_drv->bus.disable (flash->busRes);
    }

  flash->status = DEV_FLASH_STATUS_OK;
}

void
flashW25qxxWrite (devFlash_t *flash, uint32_t addr, uint8_t *data, uint32_t len)
{
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  if (len == 0 || data == NULL)
    return;

  if (addr > flash->eaddr || (addr + len) > flash->eaddr)
    {
      flash->status = DEV_FLASH_STATUS_ERROR;
      return;
    }

  uint32_t remaining = len;
  uint32_t currentAddr = addr;
  uint8_t *currentData = data;

  while (remaining > 0)
    {
      uint32_t pageOffset = currentAddr % 256;
      uint32_t pageRemaining = 256 - pageOffset;
      uint32_t writeLen
          = (remaining < pageRemaining) ? remaining : pageRemaining;

      flashW25qxxWriteEnable (flash);

      if (flash->addr_mode == W25QXX_ADDR_MODE_4BYTE)
        {
          uint8_t cmd[5]
              = { W25QXX_I_PAGE_PGM_4B, (uint8_t)((currentAddr >> 24) & 0xFF),
                  (uint8_t)((currentAddr >> 16) & 0xFF),
                  (uint8_t)((currentAddr >> 8) & 0xFF),
                  (uint8_t)(currentAddr & 0xFF) };

          mcu_drv->bus.enable (flash->busRes);
          mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                              sizeof (cmd));
          mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM,
                              currentData, writeLen);
          mcu_drv->bus.disable (flash->busRes);
        }
      else
        {
          uint8_t cmd[4]
              = { W25QXX_I_PAGE_PGM, (uint8_t)((currentAddr >> 16) & 0xFF),
                  (uint8_t)((currentAddr >> 8) & 0xFF),
                  (uint8_t)(currentAddr & 0xFF) };

          mcu_drv->bus.enable (flash->busRes);
          mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                              sizeof (cmd));
          mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM,
                              currentData, writeLen);
          mcu_drv->bus.disable (flash->busRes);
        }

      while (0 != (w25qxxReadStatusRegX (flash, W25QXX_SR_1) & W25QXX_SR1_BUSY))
        ;

      currentAddr += writeLen;
      currentData += writeLen;
      remaining -= writeLen;
    }

  flash->status = DEV_FLASH_STATUS_OK;
}

void
flashW25qxxEraseSector4K (devFlash_t *flash, uint32_t addr)
{
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  if (addr > flash->eaddr)
    {
      flash->status = DEV_FLASH_STATUS_ERROR;
      return;
    }

  flashW25qxxWriteEnable (flash);

  if (flash->addr_mode == W25QXX_ADDR_MODE_4BYTE)
    {
      uint8_t cmd[5]
          = { W25QXX_SECTOR_ERASE_4K_4B, (uint8_t)((addr >> 24) & 0xFF),
              (uint8_t)((addr >> 16) & 0xFF), (uint8_t)((addr >> 8) & 0xFF),
              (uint8_t)(addr & 0xFF) };

      mcu_drv->bus.enable (flash->busRes);
      mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                          sizeof (cmd));
      mcu_drv->bus.disable (flash->busRes);
    }
  else
    {
      uint8_t cmd[4]
          = { W25QXX_SECTOR_ERASE_4K, (uint8_t)((addr >> 16) & 0xFF),
              (uint8_t)((addr >> 8) & 0xFF), (uint8_t)(addr & 0xFF) };

      mcu_drv->bus.enable (flash->busRes);
      mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                          sizeof (cmd));
      mcu_drv->bus.disable (flash->busRes);
    }

  while (0 != (w25qxxReadStatusRegX (flash, W25QXX_SR_1) & W25QXX_SR1_BUSY))
    ;

  flash->status = DEV_FLASH_STATUS_OK;
}

void
flashW25qxxEraseBlock32K (devFlash_t *flash, uint32_t addr)
{
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  if (addr > flash->eaddr)
    {
      flash->status = DEV_FLASH_STATUS_ERROR;
      return;
    }

  flashW25qxxWriteEnable (flash);

  uint8_t cmd[4] = { W25QXX_BLOCK_ERASE_32K, (uint8_t)((addr >> 16) & 0xFF),
                     (uint8_t)((addr >> 8) & 0xFF), (uint8_t)(addr & 0xFF) };

  mcu_drv->bus.enable (flash->busRes);
  mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                      sizeof (cmd));
  mcu_drv->bus.disable (flash->busRes);

  while (0 != (w25qxxReadStatusRegX (flash, W25QXX_SR_1) & W25QXX_SR1_BUSY))
    ;

  flash->status = DEV_FLASH_STATUS_OK;
}

void
flashW25qxxEraseBlock64K (devFlash_t *flash, uint32_t addr)
{
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  if (addr > flash->eaddr)
    {
      flash->status = DEV_FLASH_STATUS_ERROR;
      return;
    }

  flashW25qxxWriteEnable (flash);

  if (flash->addr_mode == W25QXX_ADDR_MODE_4BYTE)
    {
      uint8_t cmd[5]
          = { W25QXX_BLOCK_ERASE_64K_4B, (uint8_t)((addr >> 24) & 0xFF),
              (uint8_t)((addr >> 16) & 0xFF), (uint8_t)((addr >> 8) & 0xFF),
              (uint8_t)(addr & 0xFF) };

      mcu_drv->bus.enable (flash->busRes);
      mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                          sizeof (cmd));
      mcu_drv->bus.disable (flash->busRes);
    }
  else
    {
      uint8_t cmd[4]
          = { W25QXX_BLOCK_ERASE_64K, (uint8_t)((addr >> 16) & 0xFF),
              (uint8_t)((addr >> 8) & 0xFF), (uint8_t)(addr & 0xFF) };

      mcu_drv->bus.enable (flash->busRes);
      mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                          sizeof (cmd));
      mcu_drv->bus.disable (flash->busRes);
    }

  while (0 != (w25qxxReadStatusRegX (flash, W25QXX_SR_1) & W25QXX_SR1_BUSY))
    ;

  flash->status = DEV_FLASH_STATUS_OK;
}

void
flashW25qxxEraseChip (devFlash_t *flash)
{
  mcuDrivers_t *mcu_drv = mcuDrivers ();

  flashW25qxxWriteEnable (flash);

  uint8_t cmd[] = { W25QXX_CHIP_ERASE };

  mcu_drv->bus.enable (flash->busRes);
  mcu_drv->bus.write (flash->busRes, IGNORE_PARAM, IGNORE_PARAM, cmd,
                      sizeof (cmd));
  mcu_drv->bus.disable (flash->busRes);

  while (0 != (w25qxxReadStatusRegX (flash, W25QXX_SR_1) & W25QXX_SR1_BUSY))
    ;

  flash->status = DEV_FLASH_STATUS_OK;
}
