#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "snapshot.h"

struct sqlite3;

namespace tw {

struct SnapshotMeta {
    std::string id;
    std::int64_t created_at_unix_ms = 0;
    std::string machine;
    std::string os_build;
};

class Storage {
public:
    // db_path = ":memory:" — in-memory БД для тестов.
    static Storage open(const std::string& db_path);
    ~Storage();
    Storage(Storage&&) noexcept;
    Storage& operator=(Storage&&) noexcept;
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    void save(const Snapshot& snapshot);
    std::vector<SnapshotMeta> list();
    std::optional<Snapshot> load(const std::string& id);

    // Путь по умолчанию: %LOCALAPPDATA%\Tracewell\tracewell.db (каталог создаётся).
    static std::string default_db_path();

private:
    explicit Storage(sqlite3* db);
    void migrate();

    sqlite3* db_ = nullptr;
};

}  // namespace tw
