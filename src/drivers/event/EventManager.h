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

#include "drivers/event/event.h"
#include "drivers/event/EventTable.h"

namespace ThetaGP::Drivers::Event {

class EventManager {
public:
  static EventManager &getInstance();

  void registerTable(const EventTable &table);
  void registerEvent(Event &evt);
  void trigger(Event &evt);
  void trigger(const char *tableName, const char *eventName);

  uint8_t eventCount() const;
  Event *getEvent(uint8_t i) const;
  const char *getEventName(uint8_t i) const;

private:
  EventManager() = default;

  static constexpr uint8_t MAX_TABLES = 4;
  static constexpr uint8_t MAX_EVENTS = 16;

  const EventTable *_tables[MAX_TABLES] = {};
  uint8_t _tableCount = 0;

  Event *_events[MAX_EVENTS] = {};
  const char *_eventNames[MAX_EVENTS] = {};
  uint8_t _count = 0;
};

} // namespace ThetaGP::Drivers::Event
