// src/app/ui/Theme.cpp
#include "pch.h"
#include "ui/Theme.h"

namespace tw::app {

namespace {
constexpr ThemeColors kLight{
    {0.96f, 0.96f, 0.96f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {0.10f, 0.10f, 0.10f, 1.0f},
    {0.0f, 0.47f, 0.84f, 1.0f},
    {0.82f, 0.82f, 0.82f, 1.0f},
};
constexpr ThemeColors kDark{
    {0.11f, 0.11f, 0.12f, 1.0f},
    {0.16f, 0.16f, 0.18f, 1.0f},
    {0.93f, 0.93f, 0.94f, 1.0f},
    {0.20f, 0.60f, 0.95f, 1.0f},
    {0.27f, 0.27f, 0.29f, 1.0f},
};
}  // namespace

ThemeMode Theme::DetectSystemTheme() {
    DWORD value = 1;  // 1 = светлая тема, значение Windows по умолчанию
    DWORD size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                       L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                          reinterpret_cast<BYTE*>(&value), &size);
        RegCloseKey(key);
    }
    return value == 0 ? ThemeMode::Dark : ThemeMode::Light;
}

const ThemeColors& Theme::ColorsFor(ThemeMode mode) {
    return mode == ThemeMode::Dark ? kDark : kLight;
}

}  // namespace tw::app
