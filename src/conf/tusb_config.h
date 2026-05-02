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

/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#include "BoardConfig.h"
#include "utils/log/log.h"

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// RHport Configuration
//--------------------------------------------------------------------+

// RHPort number used for device (ULPI is port 1)
#ifndef BOARD_TUD_RHPORT

#if defined(USBHW_IF_ULPI) || defined(USBHW_IF_OTG1)
#define BOARD_TUD_RHPORT 1
#elif defined(USBHW_IF_OTG2)
#define BOARD_TUD_RHPORT 0
#else
#define BOARD_TUD_RHPORT 0
#endif

#endif

// RHPort max operational speed (from BoardConfig.h)
#ifndef BOARD_TUD_MAX_SPEED
#if defined(USBHW_SPEED_HS)
#define BOARD_TUD_MAX_SPEED OPT_MODE_HIGH_SPEED
#elif defined(USBHW_SPEED_FS)
#define BOARD_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#endif
#endif

// Device mode with rhport and speed
#if BOARD_TUD_RHPORT == 0
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | BOARD_TUD_MAX_SPEED)
#elif BOARD_TUD_RHPORT == 1
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_DEVICE | BOARD_TUD_MAX_SPEED)
#else
#error "Incorrect RHPort configuration"
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

/* USB DMA on some MCUs can only access a specific SRAM region with restriction
 * on alignment. Tinyusb use follows macros to declare transferring memory so
 * that they can be put into those specific section. e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_DEBUG_PRINTF LOG_IF

// Enable Device stack, Default is max speed that hardware controller could
// support with on-chip PHY
#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

// CDC FIFO size of TX and RX
#define CFG_TUD_CDC_RX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_CDC_TX_BUFSIZE (TUD_OPT_HIGH_SPEED ? 512 : 64)

// CDC Endpoint transfer buffer size, default to max bulk packet size (HS 512,
// FS 64). Larger is faster. Larger RX_EPSIZE requires CFG_TUD_CDC_RX_NEED_ZLP =
// 1 and host ZLP support
#define CFG_TUD_CDC_RX_EPSIZE  (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_CDC_TX_EPSIZE  (TUD_OPT_HIGH_SPEED ? 512 : 64)

//------------- CLASS -------------//
#define CFG_TUD_HID            1
#define CFG_TUD_CDC            1
#define CFG_TUD_MSC            0
#define CFG_TUD_MIDI           0
#define CFG_TUD_VENDOR         0

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
