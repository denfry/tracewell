// src/app/shell/MainWindow.cpp
#include "pch.h"
#include "shell/MainWindow.h"

namespace tw::app {

namespace {
constexpr wchar_t kClassName[] = L"TracewellMainWindow";
}

bool MainWindow::Create(HINSTANCE instance, int cmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kClassName, L"Tracewell", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
                             nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }

    if (!renderDevice_.Initialize(hwnd_)) {
        return false;
    }

    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE:
            renderDevice_.Resize(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            renderDevice_.SetDpi(static_cast<float>(LOWORD(wParam)),
                                  static_cast<float>(HIWORD(wParam)));
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::Paint() {
    ID2D1DeviceContext* context = renderDevice_.BeginDraw();
    if (!context) {
        ValidateRect(hwnd_, nullptr);
        return;
    }
    context->Clear(D2D1::ColorF(0.95f, 0.95f, 0.95f));
    if (!renderDevice_.EndDraw()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    ValidateRect(hwnd_, nullptr);
}

}  // namespace tw::app
