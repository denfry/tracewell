#pragma once

#include <string>
#include <vector>

#include "collector.h"

namespace tw {

struct EntityChange {
    std::string key;
    json before;
    json after;
};

struct DiffResult {
    std::vector<std::string> added;
    std::vector<std::string> removed;
    std::vector<EntityChange> changed;

    bool empty() const { return added.empty() && removed.empty() && changed.empty(); }
};

// Чистая функция над двумя payload'ами (JSON-объекты entity_id -> данные).
// Тестируется без Windows API.
DiffResult diff_payloads(const json& before, const json& after);

}  // namespace tw
