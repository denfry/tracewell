#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "collector.h"

namespace tw {

struct Snapshot {
    std::string id;
    std::int64_t created_at_unix_ms = 0;
    std::string machine;   // отпечаток машины, чтобы не смешивать тренды разных ПК
    std::string os_build;  // например "10.0.26100"
    std::vector<CollectorResult> results;
};

// Лексикографически сортируемый id (timestamp-префикс + случайный суффикс).
std::string generate_snapshot_id();

std::string machine_fingerprint();
std::string os_build_string();
std::int64_t unix_now_ms();

}  // namespace tw
