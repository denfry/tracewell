#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace tw {

using json = nlohmann::json;

// Сквозной токен отмены: UI (Phase 1) должен уметь прервать сбор в любой момент.
class CancellationToken {
public:
    CancellationToken() : cancelled_(std::make_shared<std::atomic_bool>(false)) {}
    void cancel() { cancelled_->store(true); }
    bool cancelled() const { return cancelled_->load(); }

private:
    std::shared_ptr<std::atomic_bool> cancelled_;
};

enum class Severity { Info = 0, Warning = 1, Error = 2 };

// Partial failure — first-class: отказ одного ключа/счётчика не валит collector.
struct CollectorError {
    long long hresult = 0;
    std::string context;
    Severity severity = Severity::Warning;
};

enum class CostClass { Fast, Slow, Streaming };

struct CollectorCaps {
    bool needs_admin = false;
    CostClass cost = CostClass::Fast;
};

struct CollectorResult {
    std::string collector_id;
    int schema_version = 1;
    // payload — JSON-объект entity_id -> данные сущности; ключ стабилен между
    // снапшотами (нормализованный путь/имя) — на нём строится diff.
    json payload = json::object();
    std::vector<CollectorError> errors;
    std::chrono::milliseconds duration{0};
};

struct ICollector {
    virtual ~ICollector() = default;
    virtual std::string_view id() const = 0;
    virtual CollectorCaps caps() const = 0;
    virtual CollectorResult collect(CancellationToken token) = 0;
};

}  // namespace tw
