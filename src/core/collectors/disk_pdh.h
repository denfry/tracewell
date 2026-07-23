#pragma once

#include "../collector.h"

namespace tw {

// Disk I/O через PDH. Rate-счётчики требуют двух сэмплов — collect() блокируется
// на ~1 секунду. Только PdhAddEnglishCounter: локализованные имена — известная ловушка.
class DiskPdhCollector final : public ICollector {
public:
    std::string_view id() const override { return "disk.pdh"; }
    CollectorCaps caps() const override { return {.needs_admin = false, .cost = CostClass::Slow}; }
    CollectorResult collect(CancellationToken token) override;
};

}  // namespace tw
