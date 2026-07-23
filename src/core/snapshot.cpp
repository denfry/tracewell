#include "snapshot.h"

#include <windows.h>

#include <chrono>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>

namespace tw {

std::int64_t unix_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string generate_snapshot_id() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream out;
    out << std::hex << std::setw(12) << std::setfill('0') << unix_now_ms() << "-" << std::setw(8)
        << (rng() & 0xFFFFFFFFull);
    return out.str();
}

static std::string read_registry_string(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buf[256]{};
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string result(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) {
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, result.data(), len, nullptr, nullptr);
    }
    return result;
}

std::string machine_fingerprint() {
    // Хэш MachineGuid: сама GUID в БД не хранится (приватность), а отпечаток
    // стабилен для машины и меняется при переустановке Windows (задокументировано).
    std::string guid = read_registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
                                            L"MachineGuid");
    if (guid.empty()) {
        return "unknown";
    }
    std::ostringstream out;
    out << std::hex << std::hash<std::string>{}(guid);
    return out.str();
}

std::string os_build_string() {
    std::string build = read_registry_string(
        HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
    std::string display = read_registry_string(
        HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    if (build.empty()) {
        return "unknown";
    }
    return display.empty() ? build : build + " (" + display + ")";
}

}  // namespace tw
