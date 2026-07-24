// src/app/shell/MainWindow.h
#pragma once

#include <memory>

#include "pch.h"
#include "render/RenderDevice.h"
#include "ui/Button.h"
#include "ui/TextBlock.h"
#include "ui/Theme.h"

namespace tw::app {

class MainWindow {
public:
    bool Create(HINSTANCE instance, int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint();
    void HandleMouseMove(D2D1_POINT_2F point);
    void HandleLeftButtonUp(D2D1_POINT_2F point);

    HWND hwnd_ = nullptr;
    RenderDevice renderDevice_;
    ThemeMode themeMode_ = ThemeMode::Light;

    std::shared_ptr<Button> helloButton_;
    std::shared_ptr<TextBlock> helloText_;
};

}  // namespace tw::app
