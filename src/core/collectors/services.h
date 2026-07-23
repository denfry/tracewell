#pragma once

#include "../collector.h"

namespace tw {

// Службы Windows: имя, тип запуска (включая delayed auto-start), состояние, PID.
class ServicesCollector final : public ICollector {
public:
    std::string_view id() const override { return "services"; }
    CollectorCaps caps() const override { return {.needs_admin = false, .cost = CostClass::Fast}; }
    CollectorResult collect(CancellationToken token) override;
};

}  // namespace tw
