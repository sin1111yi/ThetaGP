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

#ifdef __cplusplus
extern "C" {
#endif

#if defined(STM32H7)

#define NVIC_PRIORITY_GROUPING NVIC_PRIORITYGROUP_2

#define NVIC_PRIO_MAX          NVIC_BUILD_PRIORITY(0, 1)

#define NVIC_BUILD_PRIORITY(base, sub)                                         \
  (((((base) << (4 - (7 - (NVIC_PRIORITY_GROUPING)))) |                        \
     ((sub) & (0x0f >> (7 - (NVIC_PRIORITY_GROUPING)))))                       \
    << 4) &                                                                    \
   0xf0)

#define NVIC_PRIORITY_BASE(prio)                                               \
  (((prio) >> (4 - (7 - (NVIC_PRIORITY_GROUPING)))) >> 4)

#define NVIC_PRIORITY_SUB(prio)                                                \
  (((prio) & (0x0f >> (7 - (NVIC_PRIORITY_GROUPING)))) >> 4)

#endif

#ifdef __cplusplus
}
#endif