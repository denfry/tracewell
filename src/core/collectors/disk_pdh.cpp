#include "disk_pdh.h"

#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>

#include <chrono>
#include <thread>
#include <vector>

#include "../win_util.h"

namespace tw {
namespace {

struct CounterSpec {
    const wchar_t* path;
    const char* field;
};

const CounterSpec kCounters[] = {
    {L"\\PhysicalDisk(*)\\Disk Reads/sec", "reads_per_sec"},
    {L"\\PhysicalDisk(*)\\Disk Writes/sec", "writes_per_sec"},
    {L"\\PhysicalDisk(*)\\Avg. Disk sec/Transfer", "avg_sec_per_transfer"},
    {L"\\PhysicalDisk(*)\\Current Disk Queue Length", "queue_length"},
    {L"\\PhysicalDisk(*)\\% Idle Time", "idle_time_pct"},
};

}  // namespace

CollectorResult DiskPdhCollector::collect(CancellationToken token) {
    auto started = std::chrono::steady_clock::now();
    CollectorResult result;
    result.collector_id = std::string(id());

    PDH_HQUERY query = nullptr;
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query);
    if (status != ERROR_SUCCESS) {
        result.errors.push_back({status, "PdhOpenQuery", Severity::Error});
        return result;
    }

    std::vector<PDH_HCOUNTER> counters;
    for (const auto& spec : kCounters) {
        PDH_HCOUNTER counter = nullptr;
        status = PdhAddEnglishCounterW(query, spec.path, 0, &counter);
        if (status != ERROR_SUCCESS) {
            result.errors.push_back({status, std::string("PdhAddEnglishCounter ") + spec.field,
                                     Severity::Warning});
            counter = nullptr;
        }
        counters.push_back(counter);
    }

    // Два сэмпла с интервалом 1 с — rate-счётчики без второго сэмпла не считаются.
    PdhCollectQueryData(query);
    for (int i = 0; i < 10 && !token.cancelled(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    status = PdhCollectQueryData(query);
    if (status != ERROR_SUCCESS) {
        result.errors.push_back({status, "PdhCollectQueryData", Severity::Error});
        PdhCloseQuery(query);
        return result;
    }

    for (size_t i = 0; i < std::size(kCounters); ++i) {
        if (!counters[i]) continue;
        DWORD buffer_size = 0, item_count = 0;
        PdhGetFormattedCounterArrayW(counters[i], PDH_FMT_DOUBLE, &buffer_size, &item_count,
                                     nullptr);
        if (buffer_size == 0) continue;
        std::vector<BYTE> buffer(buffer_size);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        status = PdhGetFormattedCounterArrayW(counters[i], PDH_FMT_DOUBLE, &buffer_size,
                                              &item_count, items);
        if (status != ERROR_SUCCESS) {
            result.errors.push_back({status, std::string("PdhGetFormattedCounterArray ") +
                                                 kCounters[i].field,
                                     Severity::Warning});
            continue;
        }
        for (DWORD j = 0; j < item_count; ++j) {
            std::string instance = wide_to_utf8(items[j].szName);
            if (instance == "_Total") continue;
            result.payload[instance][kCounters[i].field] = items[j].FmtValue.doubleValue;
        }
    }
    PdhCloseQuery(query);

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return result;
}

}  // namespace tw
