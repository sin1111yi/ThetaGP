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

#include "utils/json/json.h"

namespace ThetaGP::Test {

#ifdef THETAGP_CFG_TEST

/**
 * TestCmdHandler — handles commands in the `test.` domain.
 *
 * Supported commands:
 *   test.chip_erase   - Erase entire SPI flash chip
 *   test.spi_mode     - Set SPI bus mode (0=Sync, 1=Async with DMA)
 *   test.flash_read   - Raw SPI flash read at given address
 *   test.flash_info   - Read SPI flash size/sector info
 *   test.mempool_info - Read DevMem pool statistics
 */
class TestCmdHandler {
public:
    TestCmdHandler() = default;
    TestCmdHandler(const TestCmdHandler &) = delete;
    TestCmdHandler &operator=(const TestCmdHandler &) = delete;

    static TestCmdHandler &getInstance();
    static void handle(const char *cmd, const Json &json);
    static void registerHandlers();
};

#else

// Production mode no-op stub
class TestCmdHandler {
public:
    static TestCmdHandler &getInstance() { static TestCmdHandler i; return i; }
    static void handle(const char *, const Json &) {}
    static void registerHandlers() {}
};

#endif

} // namespace ThetaGP::Test
