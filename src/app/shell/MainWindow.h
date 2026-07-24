// src/app/shell/MainWindow.h
#pragma once

#include "pch.h"
#include "render/RenderDevice.h"

namespace tw::app {

class MainWindow {
public:
    bool Create(HINSTANCE instance, int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint();

    HWND hwnd_ = nullptr;
    RenderDevice renderDevice_;
};

}  // namespace tw::app
