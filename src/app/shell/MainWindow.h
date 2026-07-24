// src/app/shell/MainWindow.h
#pragma once

#include <memory>

#include "pch.h"
#include "dispatch/Dispatcher.h"
#include "mvvm/MainViewModel.h"
#include "render/RenderDevice.h"
#include "ui/Button.h"
#include "ui/Panel.h"
#include "ui/TextBlock.h"
#include "ui/Theme.h"

namespace tw::app {

constexpr UINT WM_APP_DISPATCH = WM_APP + 1;

class MainWindow {
public:
    bool Create(HINSTANCE instance, int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void BuildLayout();
    void Paint();
    void HandleMouseMove(D2D1_POINT_2F point);
    void HandleLeftButtonUp(D2D1_POINT_2F point);

    HWND hwnd_ = nullptr;
    RenderDevice renderDevice_;
    ThemeMode themeMode_ = ThemeMode::Light;

    Dispatcher dispatcher_{[this] { PostMessageW(hwnd_, WM_APP_DISPATCH, 0, 0); }};
    MainViewModel viewModel_{dispatcher_};

    std::shared_ptr<Panel> sidebar_;
    std::shared_ptr<Panel> content_;
    std::shared_ptr<Button> refreshButton_;
    std::shared_ptr<TextBlock> statusText_;
    Button* hoveredButton_ = nullptr;
};

}  // namespace tw::app
