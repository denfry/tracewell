#include "diff.h"

namespace tw {

DiffResult diff_payloads(const json& before, const json& after) {
    DiffResult result;
    if (!before.is_object() || !after.is_object()) {
        return result;
    }
    for (const auto& [key, old_value] : before.items()) {
        auto it = after.find(key);
        if (it == after.end()) {
            result.removed.push_back(key);
        } else if (*it != old_value) {
            result.changed.push_back({key, old_value, *it});
        }
    }
    for (const auto& [key, value] : after.items()) {
        if (!before.contains(key)) {
            result.added.push_back(key);
        }
    }
    return result;
}

}  // namespace tw
