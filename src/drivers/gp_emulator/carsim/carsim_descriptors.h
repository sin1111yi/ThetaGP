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
 * You should have received a copy of the GNU General
 * Public License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "BoardConfig.h"
#include "utils/utils.h"

#include <stdint.h>

#define CARSIM_ENDPOINT_SIZE 64

// The vendor id this project's own devices carry.
#define CARSIM_VENDOR_ID 0xcafe

// ── endpoint buffer configuration ──

#define CARSIM_INTERFACE   0
#define CARSIM_ENDPOINT    1
#define CARSIM_SIZE        64

#define CDC_COM_INTERFACE  1
#define CDC_DATA_INTERFACE 2
#define CDC_NOTIFICATION_ENDPOINT 2
#define CDC_DATA_IN_ENDPOINT      3
#define CDC_DATA_OUT_ENDPOINT     4

// ── the axes and the gear ──

// The hat the D-pad reports, in the encoding a HID hat uses.
#define CARSIM_HAT_UP        0x00
#define CARSIM_HAT_UPRIGHT   0x01
#define CARSIM_HAT_RIGHT     0x02
#define CARSIM_HAT_DOWNRIGHT 0x03
#define CARSIM_HAT_DOWN      0x04
#define CARSIM_HAT_DOWNLEFT  0x05
#define CARSIM_HAT_LEFT      0x06
#define CARSIM_HAT_UPLEFT    0x07
#define CARSIM_HAT_NOTHING   0x08

// The gear the shifter stands in, in the four bits the report carries: neutral,
// the forward gears counting up from it, reverse, and no shifter wired.
#define CARSIM_GEAR_NEUTRAL  0
#define CARSIM_GEAR_REVERSE  9
#define CARSIM_GEAR_NONE     15

// One report of the wheel: the buttons, the hat and the gear thrown together in
// one byte, then the five axes as 16-bit counts.
typedef struct __attribute((packed, aligned(1))) {
  uint32_t buttons;

  uint8_t direction : 4;
  uint8_t gear : 4;

  uint16_t steer;
  uint16_t throttle;
  uint16_t brake;
  uint16_t clutch;
  uint16_t handbrake;
} CarSimReport;

static_assert(sizeof(CarSimReport) == 15, "a car simulator report is 15 bytes");

// ── force feedback ──

// The report ids the host reaches the wheel by. The axes travel on one input
// report; every force feedback report is its own id, because the reports differ
// in length and the host tells them apart by nothing else.
#define CARSIM_REPORT_AXES             0x01
#define CARSIM_REPORT_SET_EFFECT       0x10
#define CARSIM_REPORT_SET_CONSTANT     0x11
#define CARSIM_REPORT_SET_PERIODIC     0x12
#define CARSIM_REPORT_SET_CONDITION    0x13
#define CARSIM_REPORT_SET_ENVELOPE     0x14
#define CARSIM_REPORT_EFFECT_OPERATION 0x15
#define CARSIM_REPORT_BLOCK_FREE       0x16
#define CARSIM_REPORT_DEVICE_CONTROL   0x17
#define CARSIM_REPORT_DEVICE_GAIN      0x18
#define CARSIM_REPORT_CREATE_EFFECT    0x19
#define CARSIM_REPORT_POOL             0x20
#define CARSIM_REPORT_BLOCK_LOAD       0x21

// The effect types, in the order the descriptor declares them: the value the host
// writes is the type's place in that list, counting from one.
#define CARSIM_EFFECT_NONE      0
#define CARSIM_EFFECT_CONSTANT  1
#define CARSIM_EFFECT_RAMP      2
#define CARSIM_EFFECT_SQUARE    3
#define CARSIM_EFFECT_SINE      4
#define CARSIM_EFFECT_TRIANGLE  5
#define CARSIM_EFFECT_SAW_UP    6
#define CARSIM_EFFECT_SAW_DOWN  7
#define CARSIM_EFFECT_SPRING    8
#define CARSIM_EFFECT_DAMPER    9
#define CARSIM_EFFECT_INERTIA   10
#define CARSIM_EFFECT_FRICTION  11

// The blocks the pool hands out, and the pool the host is told it has.
#define CARSIM_EFFECT_BLOCKS 8
#define CARSIM_POOL_SIZE     640

// What the device control report asks for.
#define CARSIM_CONTROL_ENABLE    1
#define CARSIM_CONTROL_DISABLE   2
#define CARSIM_CONTROL_STOP_ALL  3
#define CARSIM_CONTROL_RESET     4
#define CARSIM_CONTROL_PAUSE     5
#define CARSIM_CONTROL_CONTINUE  6

// What an effect operation report asks for.
#define CARSIM_OPERATION_START 1
#define CARSIM_OPERATION_STOP  2

// What a block load report answers: the pool took the effect, the pool has no
// room, or the effect was refused.
#define CARSIM_BLOCK_LOAD_SUCCESS 1
#define CARSIM_BLOCK_LOAD_FULL    2
#define CARSIM_BLOCK_LOAD_ERROR   3

// ── descriptors ──

static const uint8_t carsim_string_language[] = {0x09, 0x04};
static const uint8_t carsim_string_manufacturer[] = "ThetaGamepad";
static const uint8_t carsim_string_product[] = BOARD_NAME " Wheel";
static const uint8_t carsim_string_version[] = "1.0";
static const uint8_t carsim_string_cdc[] = "ThetaGamepad Virtual Com Port";

static const uint8_t *carsim_string_descriptors[]
    __attribute__((unused)) = {carsim_string_language, carsim_string_manufacturer,
                               carsim_string_product, carsim_string_version,
                               carsim_string_cdc};

static uint8_t carsim_device_descriptor[] = {
    18, // bLength
    1,  // bDescriptorType
    0x00,
    0x02,                 // bcdUSB
    0,                    // bDeviceClass
    0,                    // bDeviceSubClass
    0,                    // bDeviceProtocol
    CARSIM_ENDPOINT_SIZE, // bMaxPacketSize0
    LSB16(CARSIM_VENDOR_ID),
    MSB16(CARSIM_VENDOR_ID), // idVendor
    0x00,
    0x01, // idProduct placeholder — patched at runtime
    0x00,
    0x01, // bcdDevice
    1,    // iManufacturer
    2,    // iProduct
    0,    // iSerialNumber
    1     // bNumConfigurations
};

// The wheel's report: 32 buttons, the hat and the gear in one byte, then the
// axes; and a PID collection carrying the force feedback reports the host sends
// down and reads back. Steering rests at the centre of its range and the pedals
// and the lever at zero.
static const uint8_t carsim_report_descriptor[] = {
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x04, // USAGE (Joystick)
    0xa1, 0x01, // COLLECTION (Application)

    0x85, CARSIM_REPORT_AXES,

    // 32 buttons
    0x05, 0x09, //   USAGE_PAGE (Button)
    0x19, 0x01, //   USAGE_MINIMUM (Button 1)
    0x29, 0x20, //   USAGE_MAXIMUM (Button 32)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0x01, //   LOGICAL_MAXIMUM (1)
    0x95, 0x20, //   REPORT_COUNT (32)
    0x75, 0x01, //   REPORT_SIZE (1)
    0x81, 0x02, //   INPUT (Data,Var,Abs)

    // hat
    0x05, 0x01, //   USAGE_PAGE (Generic Desktop)
    0x09, 0x39, //   USAGE (Hat switch)
    0x25, 0x07, //   LOGICAL_MAXIMUM (7)
    0x95, 0x01, //   REPORT_COUNT (1)
    0x75, 0x04, //   REPORT_SIZE (4)
    0x81, 0x42, //   INPUT (Data,Var,Abs,Null)

    // gear
    0x09, 0x37, //   USAGE (Dial)
    0x25, 0x0f, //   LOGICAL_MAXIMUM (15)
    0x95, 0x01, //   REPORT_COUNT (1)
    0x75, 0x04, //   REPORT_SIZE (4)
    0x81, 0x02, //   INPUT (Data,Var,Abs)

    // the axes
    0x26, 0xff, 0xff, //   LOGICAL_MAXIMUM (65535)
    0x46, 0xff, 0xff, //   PHYSICAL_MAXIMUM (65535)
    0x09, 0x30,       //   USAGE (X) — steering
    0x09, 0x31,       //   USAGE (Y) — throttle
    0x09, 0x32,       //   USAGE (Z) — brake
    0x09, 0x33,       //   USAGE (Rx) — clutch
    0x09, 0x34,       //   USAGE (Ry) — handbrake
    0x75, 0x10,       //   REPORT_SIZE (16)
    0x95, 0x05,       //   REPORT_COUNT (5)
    0x81, 0x02,       //   INPUT (Data,Var,Abs)

    0xc0, // END_COLLECTION

    // ── PID collection ──
    0x05, 0x0f, // USAGE_PAGE (Physical Interface Device)
    0x09, 0x01, // USAGE (Physical Interface Device)
    0xa1, 0x01, // COLLECTION (Application)

    // Create New Effect
    0x09, 0xab, //   USAGE (Create New Effect Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_CREATE_EFFECT,
    0x09, 0x22, //     USAGE (Effect Block Index)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x25, 0x07, //     LOGICAL_MAXIMUM (7)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x91, 0x02, //     OUTPUT (Data,Var,Abs)
    0x09, 0x25, //     USAGE (Effect Type)
    0xa1, 0x02, //     COLLECTION (Logical)
    0x09, 0x26, //     USAGE (ET Constant Force)
    0x09, 0x27, //     USAGE (ET Ramp)
    0x09, 0x30, //     USAGE (ET Square)
    0x09, 0x31, //     USAGE (ET Sine)
    0x09, 0x32, //     USAGE (ET Triangle)
    0x09, 0x33, //     USAGE (ET Sawtooth Up)
    0x09, 0x34, //     USAGE (ET Sawtooth Down)
    0x09, 0x40, //     USAGE (ET Spring)
    0x09, 0x41, //     USAGE (ET Damper)
    0x09, 0x42, //     USAGE (ET Inertia)
    0x09, 0x43, //     USAGE (ET Friction)
    0x15, 0x01, //     LOGICAL_MINIMUM (1)
    0x25, 0x0b, //     LOGICAL_MAXIMUM (11)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x91, 0x00, //     OUTPUT (Data,Ary,Abs)
    0xc0,       //     END_COLLECTION
    0xc0,       //   END_COLLECTION

    // Set Effect
    0x09, 0x21, //   USAGE (Set Effect Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_SET_EFFECT,
    0x09, 0x22,       //     USAGE (Effect Block Index)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x07,       //     LOGICAL_MAXIMUM (7)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x25,       //     USAGE (Effect Type)
    0xa1, 0x02,       //     COLLECTION (Logical)
    0x09, 0x26, //       USAGE (ET Constant Force)
    0x09, 0x27, //       USAGE (ET Ramp)
    0x09, 0x30, //       USAGE (ET Square)
    0x09, 0x31, //       USAGE (ET Sine)
    0x09, 0x32, //       USAGE (ET Triangle)
    0x09, 0x33, //       USAGE (ET Sawtooth Up)
    0x09, 0x34, //       USAGE (ET Sawtooth Down)
    0x09, 0x40, //       USAGE (ET Spring)
    0x09, 0x41, //       USAGE (ET Damper)
    0x09, 0x42, //       USAGE (ET Inertia)
    0x09, 0x43, //       USAGE (ET Friction)
    0x15, 0x01, //       LOGICAL_MINIMUM (1)
    0x25, 0x0b, //       LOGICAL_MAXIMUM (11)
    0x75, 0x08, //       REPORT_SIZE (8)
    0x95, 0x01, //       REPORT_COUNT (1)
    0x91, 0x00, //       OUTPUT (Data,Ary,Abs)
    0xc0,             //     END_COLLECTION
    0x09, 0x50,       //     USAGE (Duration)
    0x66, 0x03, 0x10, //     UNIT (milliseconds)
    0x55, 0x0d,       //     UNIT_EXPONENT (-3)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x53,       //     USAGE (Trigger Button)
    0x15, 0x01,       //     LOGICAL_MINIMUM (1)
    0x25, 0x20,       //     LOGICAL_MAXIMUM (32)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x54,       //     USAGE (Trigger Repeat Interval)
    0x66, 0x03, 0x10, //     UNIT (milliseconds)
    0x55, 0x0d,       //     UNIT_EXPONENT (-3)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x52,       //     USAGE (Gain)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00, //     LOGICAL_MAXIMUM (255)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x56,       //     USAGE (Direction Enable)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x01,       //     LOGICAL_MAXIMUM (1)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x57,       //     USAGE (Direction)
    0xa1, 0x02,       //     COLLECTION (Logical)
    0x09, 0x57,       //       USAGE (Direction)
    0x15, 0x00,       //       LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //       LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //       REPORT_SIZE (16)
    0x95, 0x01,       //       REPORT_COUNT (1)
    0x91, 0x02,       //       OUTPUT (Data,Var,Abs)
    0xc0,             //     END_COLLECTION
    0x09, 0xa7,       //     USAGE (Start Delay)
    0x66, 0x03, 0x10, //     UNIT (milliseconds)
    0x55, 0x0d,       //     UNIT_EXPONENT (-3)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0xc0,             //   END_COLLECTION

    // Set Constant Force
    0x09, 0x73, //   USAGE (Set Constant Force Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_SET_CONSTANT,
    0x09, 0x22,       //     USAGE (Effect Block Index)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x07,       //     LOGICAL_MAXIMUM (7)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x70,       //     USAGE (Magnitude)
    0x16, 0xff, 0x7f, //     LOGICAL_MINIMUM (-32767)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0xc0,             //   END_COLLECTION

    // Set Periodic
    0x09, 0x6e, //   USAGE (Set Periodic Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_SET_PERIODIC,
    0x09, 0x22,       //     USAGE (Effect Block Index)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x07,       //     LOGICAL_MAXIMUM (7)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x70,       //     USAGE (Magnitude)
    0x16, 0xff, 0x7f, //     LOGICAL_MINIMUM (-32767)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x6f,       //     USAGE (Offset)
    0x16, 0xff, 0x7f, //     LOGICAL_MINIMUM (-32767)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x71,       //     USAGE (Phase)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x72,       //     USAGE (Period)
    0x66, 0x03, 0x10, //     UNIT (milliseconds)
    0x55, 0x0d,       //     UNIT_EXPONENT (-3)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0xc0,             //   END_COLLECTION

    // Set Condition
    0x09, 0x5f, //   USAGE (Set Condition Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_SET_CONDITION,
    0x09, 0x22,       //     USAGE (Effect Block Index)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x07,       //     LOGICAL_MAXIMUM (7)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x23,       //     USAGE (Parameter Block Offset)
    0x15, 0x01,       //     LOGICAL_MINIMUM (1)
    0x25, 0x02,       //     LOGICAL_MAXIMUM (2)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x60,       //     USAGE (CP Offset)
    0x16, 0xff, 0x7f, //     LOGICAL_MINIMUM (-32767)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x61,       //     USAGE (Positive Coefficient)
    0x16, 0xff, 0x7f, //     LOGICAL_MINIMUM (-32767)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x62,       //     USAGE (Negative Coefficient)
    0x16, 0xff, 0x7f, //     LOGICAL_MINIMUM (-32767)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x63,       //     USAGE (Positive Saturation)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x64,       //     USAGE (Negative Saturation)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x65,       //     USAGE (Dead Band)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0xc0,             //   END_COLLECTION

    // Set Envelope
    0x09, 0x5a, //   USAGE (Set Envelope Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_SET_ENVELOPE,
    0x09, 0x22,       //     USAGE (Effect Block Index)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x07,       //     LOGICAL_MAXIMUM (7)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x5b,       //     USAGE (Attack Level)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x5c,       //     USAGE (Attack Time)
    0x66, 0x03, 0x10, //     UNIT (milliseconds)
    0x55, 0x0d,       //     UNIT_EXPONENT (-3)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x5d,       //     USAGE (Fade Level)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x5e,       //     USAGE (Fade Time)
    0x66, 0x03, 0x10, //     UNIT (milliseconds)
    0x55, 0x0d,       //     UNIT_EXPONENT (-3)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0xc0,             //   END_COLLECTION

    // Effect Operation
    0x09, 0x77, //   USAGE (Effect Operation Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_EFFECT_OPERATION,
    0x09, 0x22,       //     USAGE (Effect Block Index)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x07,       //     LOGICAL_MAXIMUM (7)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0x09, 0x78,       //     USAGE (Effect Operation)
    0xa1, 0x02,       //     COLLECTION (Logical)
    0x09, 0x79,       //       USAGE (Effect Start)
    0x09, 0x7b,       //       USAGE (Effect Stop)
    0x15, 0x01,       //       LOGICAL_MINIMUM (1)
    0x25, 0x02,       //       LOGICAL_MAXIMUM (2)
    0x75, 0x08,       //       REPORT_SIZE (8)
    0x95, 0x01,       //       REPORT_COUNT (1)
    0x91, 0x00,       //       OUTPUT (Data,Ary,Abs)
    0xc0,             //     END_COLLECTION
    0x09, 0x7c,       //     USAGE (Loop Count)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00, //     LOGICAL_MAXIMUM (255)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0xc0,             //   END_COLLECTION

    // Block Free
    0x09, 0x90, //   USAGE (Block Free Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_BLOCK_FREE,
    0x09, 0x22, //     USAGE (Effect Block Index)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x25, 0x07, //     LOGICAL_MAXIMUM (7)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x91, 0x02, //     OUTPUT (Data,Var,Abs)
    0xc0,       //   END_COLLECTION

    // Device Control
    0x09, 0x96, //   USAGE (PID Device Control)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_DEVICE_CONTROL,
    0x09, 0x97, //     USAGE (Enable Actuators)
    0x09, 0x98, //     USAGE (Disable Actuators)
    0x09, 0x99, //     USAGE (Stop All Effects)
    0x09, 0x9a, //     USAGE (Device Reset)
    0x09, 0x9b, //     USAGE (Device Pause)
    0x09, 0x9c, //     USAGE (Device Continue)
    0x15, 0x01, //     LOGICAL_MINIMUM (1)
    0x25, 0x06, //     LOGICAL_MAXIMUM (6)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x91, 0x00, //     OUTPUT (Data,Ary,Abs)
    0xc0,       //   END_COLLECTION

    // Device Gain
    0x09, 0x7d, //   USAGE (Device Gain Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_DEVICE_GAIN,
    0x09, 0x7e,       //     USAGE (Device Gain)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00, //     LOGICAL_MAXIMUM (255)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0x91, 0x02,       //     OUTPUT (Data,Var,Abs)
    0xc0,             //   END_COLLECTION

    // PID Pool
    0x09, 0x7f, //   USAGE (PID Pool Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_POOL,
    0x09, 0x80,       //     USAGE (RAM Pool Size)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0xb1, 0x02,       //     FEATURE (Data,Var,Abs)
    0x09, 0x83,       //     USAGE (Simultaneous Effects Max)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00, //     LOGICAL_MAXIMUM (255)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0xb1, 0x02,       //     FEATURE (Data,Var,Abs)
    0x09, 0xa9,       //     USAGE (Device Managed Pool)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x01,       //     LOGICAL_MAXIMUM (1)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0xb1, 0x02,       //     FEATURE (Data,Var,Abs)
    0xc0,             //   END_COLLECTION

    // PID Block Load
    0x09, 0x89, //   USAGE (PID Block Load Report)
    0xa1, 0x02, //   COLLECTION (Logical)
    0x85, CARSIM_REPORT_BLOCK_LOAD,
    0x09, 0x22,       //     USAGE (Effect Block Index)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x25, 0x07,       //     LOGICAL_MAXIMUM (7)
    0x75, 0x08,       //     REPORT_SIZE (8)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0xb1, 0x02,       //     FEATURE (Data,Var,Abs)
    0x09, 0xac,       //     USAGE (RAM Pool Available)
    0x15, 0x00,       //     LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x7f, //     LOGICAL_MAXIMUM (32767)
    0x75, 0x10,       //     REPORT_SIZE (16)
    0x95, 0x01,       //     REPORT_COUNT (1)
    0xb1, 0x02,       //     FEATURE (Data,Var,Abs)
    0x09, 0x8b,       //     USAGE (Block Load Status)
    0xa1, 0x02,       //     COLLECTION (Logical)
    0x09, 0x8c,       //       USAGE (Block Load Success)
    0x09, 0x8d,       //       USAGE (Block Load Full)
    0x09, 0x8e,       //       USAGE (Block Load Error)
    0x15, 0x01,       //       LOGICAL_MINIMUM (1)
    0x25, 0x03,       //       LOGICAL_MAXIMUM (3)
    0x75, 0x08,       //       REPORT_SIZE (8)
    0x95, 0x01,       //       REPORT_COUNT (1)
    0xb1, 0x00,       //       FEATURE (Data,Ary,Abs)
    0xc0,             //     END_COLLECTION
    0xc0,             //   END_COLLECTION

    0xc0 // END_COLLECTION (PID)
};

// HID interface descriptor block (interface + HID desc + endpoint)
#define CARSIM_IFACE_SIZE (9 + 9 + 7)
static const uint8_t carsim_interface_descriptor[] = {
    // HID interface
    9,                 // bLength
    4,                 // bDescriptorType
    CARSIM_INTERFACE,  // bInterfaceNumber
    0,                 // bAlternateSetting
    1,                 // bNumEndpoints
    0x03,              // bInterfaceClass (HID)
    0x00,              // bInterfaceSubClass
    0x00,              // bInterfaceProtocol
    0,                 // iInterface
    // HID descriptor
    9,                                // bLength
    0x21,                             // bDescriptorType
    0x11, 0x01,                       // bcdHID
    0,                                // bCountryCode
    1,                                // bNumDescriptors
    0x22,                             // bDescriptorType
    LSB16(sizeof(carsim_report_descriptor)), // wDescriptorLength
    MSB16(sizeof(carsim_report_descriptor)),
    // HID endpoint IN (interrupt)
    7,                      // bLength
    5,                      // bDescriptorType
    CARSIM_ENDPOINT | 0x80, // bEndpointAddress
    0x03,                   // bmAttributes (intr)
    CARSIM_SIZE, 0,         // wMaxPacketSize
    // bInterval: how often the host polls this endpoint, in the encoding of the
    // speed in use: one frame at full speed, one microframe at high speed.
    1,
};

// The wire layouts of the reports above, as the descriptor declares them: one
// byte per field, and the 16-bit fields little endian. The driver reads the
// reports into these and writes the answers back out of them.
typedef struct __attribute((packed, aligned(1))) {
  uint8_t block;
  uint8_t type;
  uint16_t duration;
  uint8_t triggerButton;
  uint16_t triggerRepeat;
  uint8_t gain;
  uint8_t directionEnable;
  uint16_t direction;
  uint16_t startDelay;
} CarSimSetEffectReport;

typedef struct __attribute((packed, aligned(1))) {
  uint8_t block;
  int16_t magnitude;
} CarSimSetConstantReport;

typedef struct __attribute((packed, aligned(1))) {
  uint8_t block;
  int16_t magnitude;
  int16_t offset;
  uint16_t phase;
  uint16_t period;
} CarSimSetPeriodicReport;

typedef struct __attribute((packed, aligned(1))) {
  uint8_t block;
  uint8_t parameterBlock;
  int16_t cpOffset;
  int16_t positiveCoefficient;
  int16_t negativeCoefficient;
  uint16_t positiveSaturation;
  uint16_t negativeSaturation;
  uint16_t deadBand;
} CarSimSetConditionReport;

typedef struct __attribute((packed, aligned(1))) {
  uint8_t block;
  uint16_t attackLevel;
  uint16_t attackTime;
  uint16_t fadeLevel;
  uint16_t fadeTime;
} CarSimSetEnvelopeReport;

typedef struct __attribute((packed, aligned(1))) {
  uint8_t block;
  uint8_t operation;
  uint8_t loopCount;
} CarSimEffectOperationReport;

typedef struct __attribute((packed, aligned(1))) {
  uint16_t poolSize;
  uint8_t simultaneousMax;
  uint8_t deviceManaged;
} CarSimPoolReport;

typedef struct __attribute((packed, aligned(1))) {
  uint8_t block;
  uint16_t poolAvailable;
  uint8_t status;
} CarSimBlockLoadReport;

static_assert(sizeof(CarSimSetEffectReport) == 13, "set effect is 13 bytes");
static_assert(sizeof(CarSimSetConstantReport) == 3, "set constant is 3 bytes");
static_assert(sizeof(CarSimSetPeriodicReport) == 9, "set periodic is 9 bytes");
static_assert(sizeof(CarSimSetConditionReport) == 14, "set condition is 14 bytes");
static_assert(sizeof(CarSimSetEnvelopeReport) == 9, "set envelope is 9 bytes");
static_assert(sizeof(CarSimEffectOperationReport) == 3,
              "effect operation is 3 bytes");
static_assert(sizeof(CarSimPoolReport) == 4, "pool is 4 bytes");
static_assert(sizeof(CarSimBlockLoadReport) == 4, "block load is 4 bytes");
