#include "services.h"

#include <windows.h>

#include <chrono>
#include <vector>

#include "../win_util.h"

namespace tw {
namespace {

const char* start_type_name(DWORD start_type) {
    switch (start_type) {
        case SERVICE_AUTO_START: return "auto";
        case SERVICE_DEMAND_START: return "manual";
        case SERVICE_DISABLED: return "disabled";
        case SERVICE_BOOT_START: return "boot";
        case SERVICE_SYSTEM_START: return "system";
        default: return "unknown";
    }
}

const char* state_name(DWORD state) {
    switch (state) {
        case SERVICE_RUNNING: return "running";
        case SERVICE_STOPPED: return "stopped";
        case SERVICE_PAUSED: return "paused";
        case SERVICE_START_PENDING: return "start_pending";
        case SERVICE_STOP_PENDING: return "stop_pending";
        default: return "other";
    }
}

// Возвращает пусто при отказе в доступе — non-admin видит не все конфиги; это
// partial failure, а не ошибка снапшота.
void append_config(SC_HANDLE scm, const wchar_t* service_name, json& entry,
                   CollectorResult& result) {
    SC_HANDLE service = OpenServiceW(scm, service_name, SERVICE_QUERY_CONFIG);
    if (!service) {
        result.errors.push_back({static_cast<long long>(GetLastError()),
                                 "OpenService " + wide_to_utf8(service_name), Severity::Info});
        return;
    }
    DWORD needed = 0;
    QueryServiceConfigW(service, nullptr, 0, &needed);
    if (needed > 0) {
        std::vector<BYTE> buffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (QueryServiceConfigW(service, config, needed, &needed)) {
            entry["start_type"] = start_type_name(config->dwStartType);
            entry["binary_path"] = wide_to_utf8(config->lpBinaryPathName);
            entry["account"] = wide_to_utf8(config->lpServiceStartName);
        }
    }
    DWORD needed2 = 0;
    QueryServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, nullptr, 0, &needed2);
    if (needed2 > 0) {
        std::vector<BYTE> buffer(needed2);
        if (QueryServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, buffer.data(),
                                 needed2, &needed2)) {
            entry["delayed_auto_start"] =
                reinterpret_cast<SERVICE_DELAYED_AUTO_START_INFO*>(buffer.data())
                    ->fDelayedAutostart != FALSE;
        }
    }
    CloseServiceHandle(service);
}

}  // namespace

CollectorResult ServicesCollector::collect(CancellationToken token) {
    auto started = std::chrono::steady_clock::now();
    CollectorResult result;
    result.collector_id = std::string(id());

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) {
        result.errors.push_back({static_cast<long long>(GetLastError()), "OpenSCManager",
                                 Severity::Error});
        return result;
    }

    DWORD bytes_needed = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, nullptr, 0,
                          &bytes_needed, &count, &resume, nullptr);
    std::vector<BYTE> buffer(bytes_needed);
    if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                              buffer.data(), bytes_needed, &bytes_needed, &count, &resume,
                              nullptr)) {
        auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            if (token.cancelled()) break;
            const auto& svc = services[i];
            json entry = {
                {"name", wide_to_utf8(svc.lpServiceName)},
                {"display_name", wide_to_utf8(svc.lpDisplayName)},
                {"state", state_name(svc.ServiceStatusProcess.dwCurrentState)},
                {"pid", svc.ServiceStatusProcess.dwProcessId},
            };
            append_config(scm, svc.lpServiceName, entry, result);
            result.payload[wide_to_utf8(svc.lpServiceName)] = std::move(entry);
        }
    } else {
        result.errors.push_back({static_cast<long long>(GetLastError()), "EnumServicesStatusEx",
                                 Severity::Error});
    }
    CloseServiceHandle(scm);

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return result;
}

}  // namespace tw
