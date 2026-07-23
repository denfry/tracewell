#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../core/collectors/disk_pdh.h"
#include "../core/collectors/services.h"
#include "../core/collectors/startup_registry.h"
#include "../core/diff.h"
#include "../core/snapshot.h"
#include "../core/storage.h"

namespace {

using namespace tw;

std::vector<std::unique_ptr<ICollector>> make_collectors() {
    std::vector<std::unique_ptr<ICollector>> collectors;
    collectors.push_back(std::make_unique<StartupRegistryCollector>());
    collectors.push_back(std::make_unique<ServicesCollector>());
    collectors.push_back(std::make_unique<DiskPdhCollector>());
    return collectors;
}

int cmd_snapshot(Storage& storage) {
    Snapshot snapshot;
    snapshot.id = generate_snapshot_id();
    snapshot.created_at_unix_ms = unix_now_ms();
    snapshot.machine = machine_fingerprint();
    snapshot.os_build = os_build_string();

    CancellationToken token;
    for (auto& collector : make_collectors()) {
        std::cout << "collecting " << collector->id() << "..." << std::flush;
        CollectorResult result = collector->collect(token);
        std::cout << " " << result.payload.size() << " entries, " << result.errors.size()
                  << " errors, " << result.duration.count() << " ms\n";
        snapshot.results.push_back(std::move(result));
    }
    storage.save(snapshot);
    std::cout << "snapshot saved: " << snapshot.id << "\n";
    return 0;
}

int cmd_list(Storage& storage) {
    for (const auto& meta : storage.list()) {
        std::cout << meta.id << "  " << meta.created_at_unix_ms << "  " << meta.os_build << "\n";
    }
    return 0;
}

int cmd_show(Storage& storage, const std::string& id) {
    auto snapshot = storage.load(id);
    if (!snapshot) {
        std::cerr << "snapshot not found: " << id << "\n";
        return 1;
    }
    json out = {{"id", snapshot->id},
                {"created_at", snapshot->created_at_unix_ms},
                {"machine", snapshot->machine},
                {"os_build", snapshot->os_build}};
    for (const auto& result : snapshot->results) {
        out["collectors"][result.collector_id] = result.payload;
    }
    std::cout << out.dump(2) << "\n";
    return 0;
}

int cmd_diff(Storage& storage, const std::string& id1, const std::string& id2) {
    auto before = storage.load(id1);
    auto after = storage.load(id2);
    if (!before || !after) {
        std::cerr << "snapshot not found\n";
        return 1;
    }
    for (const auto& new_result : after->results) {
        const json* old_payload = nullptr;
        for (const auto& old_result : before->results) {
            if (old_result.collector_id == new_result.collector_id) {
                old_payload = &old_result.payload;
                break;
            }
        }
        if (!old_payload) continue;
        DiffResult diff = diff_payloads(*old_payload, new_result.payload);
        if (diff.empty()) continue;
        std::cout << "== " << new_result.collector_id << " ==\n";
        for (const auto& key : diff.added) std::cout << "  + " << key << "\n";
        for (const auto& key : diff.removed) std::cout << "  - " << key << "\n";
        for (const auto& change : diff.changed) std::cout << "  ~ " << change.key << "\n";
    }
    return 0;
}

void print_usage() {
    std::cout << "tracewell-cli — Phase 0 core validation\n"
                 "  snapshot          collect all collectors and store a snapshot\n"
                 "  list              list stored snapshots\n"
                 "  show <id>         print snapshot as JSON\n"
                 "  diff <id1> <id2>  diff two snapshots\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 1;
        }
        std::string command = argv[1];
        Storage storage = Storage::open(Storage::default_db_path());
        if (command == "snapshot") return cmd_snapshot(storage);
        if (command == "list") return cmd_list(storage);
        if (command == "show" && argc >= 3) return cmd_show(storage, argv[2]);
        if (command == "diff" && argc >= 4) return cmd_diff(storage, argv[2], argv[3]);
        print_usage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 2;
    }
}
