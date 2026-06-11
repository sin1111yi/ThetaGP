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

#include <cstdint>
#include <ArduinoJson.h>

namespace ThetaGP::Test {

/**
 * Dispatcher -- routes incoming JSON commands to domain handlers.
 *
 * Parses the JSON line, extracts the `cmd` field, and dispatches
 * to the handler that matches the command's domain prefix
 * (e.g. "sys.ping" -> handler registered for "sys").
 */
class Dispatcher {
public:
    Dispatcher() = default;
    Dispatcher(const Dispatcher &) = delete;
    Dispatcher &operator=(const Dispatcher &) = delete;

    static Dispatcher &getInstance();

    /**
     * Main entry point. Called from FrameLayer frame-complete callback.
     * Static method that delegates to the singleton instance.
     * Parses jsonLine, validates fields, and dispatches to a handler.
     */
    static void dispatch(const char *jsonLine);

    /** Handler type: receives the full cmd string and the parsed document. */
    using DomainHandler = void (*)(const char *cmd, JsonDocument &doc);

    /**
     * Register a handler for a domain prefix.
     * e.g. registerHandler("test", handler) matches cmds starting with "test."
     */
    void registerHandler(const char *domain, DomainHandler handler);

private:
    static constexpr uint8_t MAX_HANDLERS = 8;

    struct HandlerEntry {
        const char *prefix;
        DomainHandler handler;
    };

    HandlerEntry _handlers[MAX_HANDLERS];
    uint8_t _handlerCount = 0;

    /** Internal dispatch implementation called by the static wrapper. */
    void dispatchImpl(const char *jsonLine);
};

} // namespace ThetaGP::Test
