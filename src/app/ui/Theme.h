// src/app/ui/Theme.h
#pragma once

#include "pch.h"

namespace tw::app {

struct ThemeColors {
    D2D1_COLOR_F background;
    D2D1_COLOR_F surface;
    D2D1_COLOR_F text;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F border;
};

enum class ThemeMode { Light, Dark };

// Таблицы цветов тёмной/светлой темы + чтение текущей системной темы из реестра.
class Theme {
public:
    static ThemeMode DetectSystemTheme();
    static const ThemeColors& ColorsFor(ThemeMode mode);
};

}  // namespace tw::app
