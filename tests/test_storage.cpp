#include <catch2/catch_test_macros.hpp>

#include "core/snapshot.h"
#include "core/storage.h"

using namespace tw;

namespace {

Snapshot make_snapshot(const std::string& id) {
    Snapshot snapshot;
    snapshot.id = id;
    snapshot.created_at_unix_ms = 1700000000000;
    snapshot.machine = "test-machine";
    snapshot.os_build = "10.0.26100";

    CollectorResult result;
    result.collector_id = "startup.registry";
    result.schema_version = 1;
    result.payload = {{"HKLM\\Run\\Foo", {{"command", "C:\\foo.exe"}}}};
    result.errors.push_back({5, "access denied on HKLM\\...", Severity::Warning});
    result.duration = std::chrono::milliseconds(42);
    snapshot.results.push_back(std::move(result));
    return snapshot;
}

}  // namespace

TEST_CASE("storage round-trips a snapshot (in-memory)") {
    Storage storage = Storage::open(":memory:");
    storage.save(make_snapshot("0001-aaaa"));

    auto metas = storage.list();
    REQUIRE(metas.size() == 1);
    CHECK(metas[0].id == "0001-aaaa");
    CHECK(metas[0].os_build == "10.0.26100");

    auto loaded = storage.load("0001-aaaa");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->results.size() == 1);
    CHECK(loaded->results[0].collector_id == "startup.registry");
    CHECK(loaded->results[0].payload["HKLM\\Run\\Foo"]["command"] == "C:\\foo.exe");
    CHECK(loaded->results[0].duration.count() == 42);
}

TEST_CASE("load of unknown id returns nullopt") {
    Storage storage = Storage::open(":memory:");
    CHECK_FALSE(storage.load("missing").has_value());
}

TEST_CASE("list is ordered by id") {
    Storage storage = Storage::open(":memory:");
    storage.save(make_snapshot("0002-bbbb"));
    storage.save(make_snapshot("0001-aaaa"));
    auto metas = storage.list();
    REQUIRE(metas.size() == 2);
    CHECK(metas[0].id == "0001-aaaa");
    CHECK(metas[1].id == "0002-bbbb");
}
