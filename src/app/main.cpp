// src/app/main.cpp
#include "pch.h"

#include "shell/MainWindow.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    tw::app::MainWindow window;
    if (!window.Create(hInstance, nCmdShow)) {
        return -1;
    }
    return window.RunMessageLoop();
}
