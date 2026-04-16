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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cycleCounterInit(void);
int32_t clockCyclesToMicros(int32_t clockCycles);
float clockCyclesToMicrosf(int32_t clockCycles);
int32_t clockCyclesTo10thMicros(int32_t clockCycles);
int32_t clockCyclesTo100thMicros(int32_t clockCycles);
uint32_t clockMicrosToCycles(uint32_t micros);
uint32_t clockCycleCounter(void);

uint32_t microsISR(void);
uint32_t micros(void);
uint32_t millis(void);

void delay_ms(uint32_t ms);
void delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif