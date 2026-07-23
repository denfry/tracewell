#include "storage.h"

#include <windows.h>

#include <shlobj.h>
#include <sqlite3.h>

#include <stdexcept>

namespace tw {
namespace {

[[noreturn]] void throw_sqlite(sqlite3* db, const char* what) {
    throw std::runtime_error(std::string(what) + ": " + (db ? sqlite3_errmsg(db) : "no db"));
}

void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string message = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("sqlite exec failed: " + message);
    }
}

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            throw_sqlite(db, "prepare");
        }
    }
    ~Stmt() { sqlite3_finalize(stmt_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& bind(int index, const std::string& value) {
        sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int index, std::int64_t value) {
        sqlite3_bind_int64(stmt_, index, value);
        return *this;
    }
    bool step() {
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw_sqlite(db_, "step");
    }
    std::string column_text(int index) {
        const unsigned char* text = sqlite3_column_text(stmt_, index);
        return text ? reinterpret_cast<const char*>(text) : "";
    }
    std::int64_t column_int64(int index) { return sqlite3_column_int64(stmt_, index); }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

constexpr int kSchemaVersion = 1;

}  // namespace

Storage::Storage(sqlite3* db) : db_(db) {}

Storage::Storage(Storage&& other) noexcept : db_(other.db_) { other.db_ = nullptr; }

Storage& Storage::operator=(Storage&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close(db_);
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

Storage::~Storage() {
    if (db_) sqlite3_close(db_);
}

Storage Storage::open(const std::string& db_path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::string message = db ? sqlite3_errmsg(db) : "cannot open";
        sqlite3_close(db);
        throw std::runtime_error("sqlite open failed (" + db_path + "): " + message);
    }
    Storage storage(db);
    exec(db, "PRAGMA journal_mode=WAL;");
    exec(db, "PRAGMA busy_timeout=5000;");
    exec(db, "PRAGMA foreign_keys=ON;");
    storage.migrate();
    return storage;
}

void Storage::migrate() {
    // Линейные номерные миграции; версия — в meta. Механизм проверен с v1,
    // чтобы к моменту реальных пользовательских данных он уже был обкатан.
    exec(db_, "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    int version = 0;
    {
        Stmt stmt(db_, "SELECT value FROM meta WHERE key='schema_version';");
        if (stmt.step()) version = static_cast<int>(std::stoll(stmt.column_text(0)));
    }
    if (version < 1) {
        exec(db_, "BEGIN;");
        exec(db_,
             "CREATE TABLE snapshots("
             "  id TEXT PRIMARY KEY,"
             "  created_at INTEGER NOT NULL,"
             "  machine TEXT NOT NULL,"
             "  os_build TEXT NOT NULL);");
        exec(db_,
             "CREATE TABLE collector_results("
             "  snapshot_id TEXT NOT NULL REFERENCES snapshots(id) ON DELETE CASCADE,"
             "  collector_id TEXT NOT NULL,"
             "  schema_version INTEGER NOT NULL,"
             "  payload TEXT NOT NULL,"
             "  duration_ms INTEGER NOT NULL);");
        exec(db_,
             "CREATE TABLE errors("
             "  snapshot_id TEXT NOT NULL REFERENCES snapshots(id) ON DELETE CASCADE,"
             "  collector_id TEXT NOT NULL,"
             "  hresult INTEGER NOT NULL,"
             "  context TEXT NOT NULL,"
             "  severity INTEGER NOT NULL);");
        exec(db_, "INSERT OR REPLACE INTO meta(key,value) VALUES('schema_version','1');");
        exec(db_, "COMMIT;");
        version = kSchemaVersion;
    }
    if (version > kSchemaVersion) {
        throw std::runtime_error("database schema is newer than this build supports");
    }
}

void Storage::save(const Snapshot& snapshot) {
    exec(db_, "BEGIN;");
    try {
        Stmt insert_snapshot(db_,
                             "INSERT INTO snapshots(id,created_at,machine,os_build) VALUES(?,?,?,?);");
        insert_snapshot.bind(1, snapshot.id)
            .bind(2, snapshot.created_at_unix_ms)
            .bind(3, snapshot.machine)
            .bind(4, snapshot.os_build)
            .step();
        for (const auto& result : snapshot.results) {
            Stmt insert_result(db_,
                               "INSERT INTO collector_results(snapshot_id,collector_id,"
                               "schema_version,payload,duration_ms) VALUES(?,?,?,?,?);");
            insert_result.bind(1, snapshot.id)
                .bind(2, result.collector_id)
                .bind(3, static_cast<std::int64_t>(result.schema_version))
                .bind(4, result.payload.dump())
                .bind(5, static_cast<std::int64_t>(result.duration.count()))
                .step();
            for (const auto& error : result.errors) {
                Stmt insert_error(db_,
                                  "INSERT INTO errors(snapshot_id,collector_id,hresult,context,"
                                  "severity) VALUES(?,?,?,?,?);");
                insert_error.bind(1, snapshot.id)
                    .bind(2, result.collector_id)
                    .bind(3, error.hresult)
                    .bind(4, error.context)
                    .bind(5, static_cast<std::int64_t>(error.severity))
                    .step();
            }
        }
        exec(db_, "COMMIT;");
    } catch (...) {
        exec(db_, "ROLLBACK;");
        throw;
    }
}

std::vector<SnapshotMeta> Storage::list() {
    std::vector<SnapshotMeta> result;
    Stmt stmt(db_, "SELECT id,created_at,machine,os_build FROM snapshots ORDER BY id;");
    while (stmt.step()) {
        result.push_back({stmt.column_text(0), stmt.column_int64(1), stmt.column_text(2),
                          stmt.column_text(3)});
    }
    return result;
}

std::optional<Snapshot> Storage::load(const std::string& id) {
    Snapshot snapshot;
    {
        Stmt stmt(db_, "SELECT id,created_at,machine,os_build FROM snapshots WHERE id=?;");
        stmt.bind(1, id);
        if (!stmt.step()) return std::nullopt;
        snapshot.id = stmt.column_text(0);
        snapshot.created_at_unix_ms = stmt.column_int64(1);
        snapshot.machine = stmt.column_text(2);
        snapshot.os_build = stmt.column_text(3);
    }
    Stmt results(db_,
                 "SELECT collector_id,schema_version,payload,duration_ms FROM collector_results "
                 "WHERE snapshot_id=?;");
    results.bind(1, id);
    while (results.step()) {
        CollectorResult result;
        result.collector_id = results.column_text(0);
        result.schema_version = static_cast<int>(results.column_int64(1));
        result.payload = json::parse(results.column_text(2), nullptr, /*allow_exceptions=*/false);
        result.duration = std::chrono::milliseconds(results.column_int64(3));
        snapshot.results.push_back(std::move(result));
    }
    return snapshot;
}

std::string Storage::default_db_path() {
    PWSTR local_appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_appdata))) {
        throw std::runtime_error("cannot resolve %LOCALAPPDATA%");
    }
    std::filesystem::path dir(local_appdata);
    CoTaskMemFree(local_appdata);
    dir /= "Tracewell";
    std::filesystem::create_directories(dir);
    return (dir / "tracewell.db").string();
}

}  // namespace tw
