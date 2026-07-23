#pragma once

#include "../collector.h"

namespace tw {

// Автозагрузка: Run/RunOnce (HKLM+HKCU, оба WOW64-view) + Startup-папки.
// Phase 1 добавит Task Scheduler и Services как отдельные источники impact-модели.
class StartupRegistryCollector final : public ICollector {
public:
    std::string_view id() const override { return "startup.registry"; }
    CollectorCaps caps() const override { return {.needs_admin = false, .cost = CostClass::Fast}; }
    CollectorResult collect(CancellationToken token) override;
};

}  // namespace tw
