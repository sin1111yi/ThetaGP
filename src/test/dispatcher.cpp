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
    // 1. Parse JSON using our own Json class
    Json cmdJson;
    cmdJson.parse(jsonLine);

    // 2. Extract cmd field and null-terminate it (getStr returns pointer INTO
    //    the original JSON string which is NOT null-terminated)
    int cmdLen = 0;
    const char *cmdRaw = cmdJson.getStr("cmd", &cmdLen);
    if (!cmdRaw || cmdLen <= 0) {
        LOG_WARN("Dispatcher: Missing or empty 'cmd' field");
        Json resp;
        resp.beginWrite(_respBuf, sizeof(_respBuf));
        resp.printf("{status:%Q,error_code:%d,reason:%Q}",
                    "error", 2, "missing 'cmd' field");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
        return;
    }
    // Copy cmd into local buffer and null-terminate for safe %Q / strcmp usage
    constexpr uint8_t CMD_BUF_MAX = 64;
    char cmdBuf[CMD_BUF_MAX];
    if (static_cast<uint16_t>(cmdLen) >= CMD_BUF_MAX) cmdLen = CMD_BUF_MAX - 1;
    memcpy(cmdBuf, cmdRaw, cmdLen);
    cmdBuf[cmdLen] = '\0';
    const char *cmd = cmdBuf;

    // 3. Extract and validate queued field
    int queued = cmdJson.getInt("queued", -1);
    if (queued < 0) {
        LOG_WARN("Dispatcher: Missing or invalid 'queued' field");
        Json resp;
        resp.beginWrite(_respBuf, sizeof(_respBuf));
        resp.printf("{status:%Q,error_code:%d,reason:%Q}",
                    "error", 2, "missing or invalid 'queued' field");
        uint16_t len = resp.end();
        FrameLayer::getInstance().sendResponse(resp.c_str(), len);
        return;
    }

    // 4. Find matching domain handler
    for (uint8_t i = 0; i < _handlerCount; ++i) {
        const char *prefix = _handlers[i].prefix;
        size_t prefixLen = strlen(prefix);

        if (strncmp(cmd, prefix, prefixLen) == 0 && cmd[prefixLen] == '.') {
            LOG_DEBUG("Dispatcher: route '%s' -> handler[%u] '%s'",
                      cmd, i, prefix);
            _handlers[i].handler(cmd, cmdJson);
            return;
        }
    }

    // 5. No handler found -- return unknown command error
    LOG_WARN("Dispatcher: Unknown command: %s", cmd);
    Json resp;
    resp.beginWrite(_respBuf, sizeof(_respBuf));
    resp.printf("{cmd:%Q,queued:%d,status:%Q,error_code:%d,reason:%Q}",
                cmd, queued + 1, "error", 1, "unknown command");
    uint16_t len = resp.end();
    FrameLayer::getInstance().sendResponse(resp.c_str(), len);
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
