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

#include <cstring>

#include "drivers/event/event.h"
#include "drivers/event/EventTable.h"

namespace ThetaGP::Drivers::Event {

class EventManager {
public:
  static EventManager &getInstance() {
    static EventManager instance;
    return instance;
  }

  void registerTable(const EventTable &table) {
    if (_tableCount >= MAX_TABLES)
      return;
    _tables[_tableCount++] = &table;
    for (uint8_t i = 0; i < table.count; i++)
      registerEvent(*table.entries[i].event);
  }

  void registerEvent(Event &evt) {
    if (_count >= MAX_EVENTS)
      return;
    _events[_count++] = &evt;
    _eventNames[_count - 1] = nullptr;
  }

  void trigger(Event &evt) {
    evt.trigger();
  }

  void trigger(const char *tableName, const char *eventName) {
    for (uint8_t t = 0; t < _tableCount; t++) {
      if (strcmp(_tables[t]->name, tableName) != 0)
        continue;
      for (uint8_t e = 0; e < _tables[t]->count; e++) {
        if (strcmp(_tables[t]->entries[e].name, eventName) == 0) {
          _tables[t]->entries[e].event->trigger();
          return;
        }
      }
    }
  }

  uint8_t eventCount() const { return _count; }
  Event *getEvent(uint8_t i) const { return _events[i]; }
  const char *getEventName(uint8_t i) const { return _eventNames[i]; }

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
