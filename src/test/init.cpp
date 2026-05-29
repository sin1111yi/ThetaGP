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

#include "test/init.h"
#include "test/framelayer.h"
#include "test/dispatcher.h"
#include "test/testsys.h"

#include "drivers/gpdriver/usbdriver.h"
#include "utils/log/log.h"

namespace ThetaGP::Test {

void initTestSystem() {
    FrameLayer &framelayer = FrameLayer::getInstance();
    Dispatcher &dispatcher = Dispatcher::getInstance();

    // Register domain handlers
    dispatcher.registerHandler("sys", SysHandler::handle);

    // Wire frame-complete callback -> dispatcher
    framelayer.setFrameCallback(Dispatcher::dispatch);

    // Wire USB CDC RX callback -> frame layer
    USB::USBDriver::getInstance().setCDCRxCallback(FrameLayer::cdcRxHandler);

    LOG_INFO("Test API system initialized [THETAGP_ENABLE_TEST_API]");
}

} // namespace ThetaGP::Test
