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

#include "test/dispatcher.h"
#include "test/framelayer.h"

#include "build_info.h"
#include "utils/log/log.h"

#include <cstring>

namespace ThetaGP::Test {

Dispatcher &Dispatcher::getInstance() {
    static Dispatcher instance;
    return instance;
}

void Dispatcher::dispatch(const char *jsonLine) {
    getInstance().dispatchImpl(jsonLine);
}

void Dispatcher::dispatchImpl(const char *jsonLine) {
    LOG_DEBUG("Dispatcher: dispatch '%s'", jsonLine);
    // 1. Parse JSON — stack-local doc (safe in main loop context)
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, jsonLine);

    if (error) {
        LOG_WARN("Dispatcher: JSON parse error: %s", error.c_str());
        JsonDocument resp;
        resp["status"] = "error";
        resp["error_code"] = 2;
        resp["reason"] = "JSON parse error";
        FrameLayer::getInstance().sendResponse(resp);
        return;
    }

    // 2. Extract cmd field
    const char *cmd = doc["cmd"];
    if (!cmd || strlen(cmd) == 0) {
        LOG_WARN("Dispatcher: Missing or empty 'cmd' field");
        JsonDocument resp;
        resp["status"] = "error";
        resp["error_code"] = 2;
        resp["reason"] = "missing 'cmd' field";
        FrameLayer::getInstance().sendResponse(resp);
        return;
    }

    // 3. Extract and validate queued field
    if (!doc["queued"].is<int>()) {
        LOG_WARN("Dispatcher: Missing or invalid 'queued' field");
        JsonDocument resp;
        resp["status"] = "error";
        resp["error_code"] = 2;
        resp["reason"] = "missing or invalid 'queued' field";
        FrameLayer::getInstance().sendResponse(resp);
        return;
    }

    // 4. Find matching domain handler
    for (uint8_t i = 0; i < _handlerCount; ++i) {
        const char *prefix = _handlers[i].prefix;
        size_t prefixLen = strlen(prefix);

        if (strncmp(cmd, prefix, prefixLen) == 0 && cmd[prefixLen] == '.') {
            LOG_DEBUG("Dispatcher: route '%s' -> handler[%u] '%s'",
                      cmd, i, prefix);
            _handlers[i].handler(cmd, doc);
            return;
        }
    }

    // 5. No handler found -- return unknown command error
    LOG_WARN("Dispatcher: Unknown command: %s", cmd);
    JsonDocument resp;
    resp["status"] = "error";
    resp["error_code"] = 1;
    resp["reason"] = "unknown command";
    resp["cmd"] = cmd;
    resp["queued"] = doc["queued"].as<int>() + 1;
    FrameLayer::getInstance().sendResponse(resp);
}

void Dispatcher::registerHandler(const char *domain, DomainHandler handler) {
    if (_handlerCount >= MAX_HANDLERS) {
        LOG_WARN("Dispatcher: Max handlers reached (%d)", MAX_HANDLERS);
        return;
    }
    _handlers[_handlerCount].prefix = domain;
    _handlers[_handlerCount].handler = handler;
    _handlerCount++;
    LOG_DEBUG("Dispatcher: Registered handler for domain '%s'", domain);
}

} // namespace ThetaGP::Test
