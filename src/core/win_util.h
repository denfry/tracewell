#pragma once

#include <windows.h>

#include <string>

namespace tw {

inline std::string wide_to_utf8(const wchar_t* text, int length = -1) {
    if (!text) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    if (length == -1 && !result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

inline std::string wide_to_utf8(const std::wstring& text) {
    return wide_to_utf8(text.c_str(), static_cast<int>(text.size()));
}

}  // namespace tw
