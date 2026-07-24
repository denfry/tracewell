// src/app/shell/MainWindow.h
#pragma once

#include "pch.h"

namespace tw::app {

class MainWindow {
public:
    bool Create(HINSTANCE instance, int cmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
};

}  // namespace tw::app
