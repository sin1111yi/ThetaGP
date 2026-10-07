/*
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
 * along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "stm32h7xx.h"

#define BDCFG_HAS_RGB_STRIP          0
#define BDCFG_LED_BARE_PIN           {Port::PortB, Pin::Pin1}
#define BDCFG_LED_GPIO_ONLY_PIN      {Port::PortB, Pin::Pin0}
#define BDCFG_LED_GPIO_ONLY_ACTIVE_LOW false
#define BDCFG_LED_STRIP2_PIN         {Port::PortE, Pin::Pin9}
#define BDCFG_LED_STRIP2_SOURCE      TimerChannel::Tim1Ch4
#define BDCFG_LED_STRIP2_NUMBER      12

#define BDCFG_KEYPAD_DRIVE_MODE      KeypadConfig::Mode::ScanMatrix
#define BDCFG_KEYPAD_ACTIVE_MODE     KeypadConfig::Active::High
#define BDCFG_KEYPAD_DRIVE_PIN_NUM   2
#define BDCFG_KEYPAD_DRIVE_IO_LIST \
    {Port::PortD, Pin::Pin8}, \
    {Port::PortD, Pin::Pin9}
#define BDCFG_KEYPAD_SENSE_PIN_NUM   3
#define BDCFG_KEYPAD_SENSE_IO_LIST \
    {Port::PortC, Pin::Pin4}, \
    {Port::PortC, Pin::Pin5}, \
    {Port::PortC, Pin::Pin6}

#define BDCFG_KEYPAD_KEY_MAP \
    {  0,   1,   2}, \
    {  3, 255,   5}


#define BDCFG_KEYPAD_MAX_KEY_INDEX   5
#define BDCFG_KEYPAD_MASK_ARRAY_SIZE 1

#define BDCFG_KEYPAD_BUTTON_MAP \
    {0, GAMEPAD_MASK_UP     }, \
    {1, GAMEPAD_MASK_B1     }, \
    {5, GAMEPAD_MASK_L1     }


#define BDCFG_IF_OTG1
#define BDCFG_SPEED_HS
#define BDCFG_REPORT_RATE_HZ         8000

#define BDCFG_USE_UART_1
#define BDCFG_USE_UART_2

#define BDCFG_USE_UART_COUNT 1

#define BDCFG_LOGGER_UART            BUS_UART_1

#define BDCFG_UART_DESC_DATA         \
        {UartInstance::Uart1, {Port::PortA, Pin::Pin9}, {Port::PortA, Pin::Pin10}, 115200}

#define BDCFG_USE_SPI_1
#define BDCFG_USE_SPI_2

#define BDCFG_USE_SPI_COUNT 1

#define BDCFG_FLASH_SPI              BUS_SPI_1

#define BDCFG_SPI_DESC_DATA          \
        {SpiInstance::Spi2, {{Port::PortB, Pin::Pin13}, {Port::PortB, Pin::Pin15}, {Port::PortB, Pin::Pin14}}, {Port::PortB, Pin::Pin12}}

#define BDCFG_HAS_FLASH 1
#define BDCFG_FLASH_CHIP_W25QXX
