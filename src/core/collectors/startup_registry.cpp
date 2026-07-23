#include "startup_registry.h"

#include <windows.h>

#include <shlobj.h>

#include <chrono>
#include <filesystem>

#include "../win_util.h"

namespace tw {
namespace {

struct RegistrySource {
    HKEY root;
    const wchar_t* subkey;
    REGSAM wow64_view;  // KEY_WOW64_64KEY / KEY_WOW64_32KEY — иначе теряется половина записей
    const char* label;
};

const RegistrySource kSources[] = {
    {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", KEY_WOW64_64KEY,
     "HKLM\\Run"},
    {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", KEY_WOW64_32KEY,
     "HKLM\\Run (32-bit)"},
    {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", KEY_WOW64_64KEY,
     "HKLM\\RunOnce"},
    {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", KEY_WOW64_64KEY,
     "HKCU\\Run"},
    {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", KEY_WOW64_64KEY,
     "HKCU\\RunOnce"},
};

void collect_registry_source(const RegistrySource& source, CollectorResult& result) {
    HKEY key = nullptr;
    LSTATUS status =
        RegOpenKeyExW(source.root, source.subkey, 0, KEY_READ | source.wow64_view, &key);
    if (status != ERROR_SUCCESS) {
        if (status != ERROR_FILE_NOT_FOUND) {
            result.errors.push_back({status, source.label, Severity::Warning});
        }
        return;
    }
    for (DWORD index = 0;; ++index) {
        wchar_t name[16384];
        DWORD name_size = static_cast<DWORD>(std::size(name));
        BYTE data[65536];
        DWORD data_size = sizeof(data);
        DWORD type = 0;
        status = RegEnumValueW(key, index, name, &name_size, nullptr, &type, data, &data_size);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) {
            result.errors.push_back({status, std::string(source.label) + " enum", Severity::Warning});
            break;
        }
        std::wstring command(reinterpret_cast<wchar_t*>(data));
        if (type == REG_EXPAND_SZ) {
            wchar_t expanded[32768];
            if (ExpandEnvironmentStringsW(command.c_str(), expanded,
                                          static_cast<DWORD>(std::size(expanded))) > 0) {
                command = expanded;
            }
        }
        std::string entry_key = std::string(source.label) + "\\" + wide_to_utf8(name, -1);
        result.payload[entry_key] = {
            {"source", source.label},
            {"name", wide_to_utf8(name, -1)},
            {"command", wide_to_utf8(command)},
            {"type", "registry"},
        };
    }
    RegCloseKey(key);
}

void collect_startup_folder(REFKNOWNFOLDERID folder_id, const char* label,
                            CollectorResult& result) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder_id, 0, nullptr, &path))) {
        result.errors.push_back({E_FAIL, label, Severity::Warning});
        return;
    }
    std::filesystem::path dir(path);
    CoTaskMemFree(path);
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string filename = wide_to_utf8(entry.path().filename().wstring());
        if (filename == "desktop.ini") continue;
        result.payload[std::string(label) + "\\" + filename] = {
            {"source", label},
            {"name", filename},
            {"command", wide_to_utf8(entry.path().wstring())},
            {"type", "startup_folder"},
        };
    }
    if (ec) {
        result.errors.push_back({ec.value(), label, Severity::Warning});
    }
}

}  // namespace

CollectorResult StartupRegistryCollector::collect(CancellationToken token) {
    auto started = std::chrono::steady_clock::now();
    CollectorResult result;
    result.collector_id = std::string(id());
    for (const auto& source : kSources) {
        if (token.cancelled()) break;
        collect_registry_source(source, result);
    }
    if (!token.cancelled()) {
        collect_startup_folder(FOLDERID_Startup, "StartupFolder\\User", result);
        collect_startup_folder(FOLDERID_CommonStartup, "StartupFolder\\Common", result);
    }
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return result;
}

}  // namespace tw
