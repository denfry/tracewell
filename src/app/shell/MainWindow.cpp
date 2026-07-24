// src/app/shell/MainWindow.cpp
#include "pch.h"
#include "shell/MainWindow.h"

#include <windowsx.h>

using Microsoft::WRL::ComPtr;

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
    themeMode_ = Theme::DetectSystemTheme();

    helloButton_ = std::make_shared<Button>();
    helloButton_->SetText(L"Hello");
    helloButton_->SetBounds(D2D1::RectF(24, 24, 144, 64));

    helloText_ = std::make_shared<TextBlock>();
    helloText_->SetText(L"Tracewell");
    helloText_->SetBounds(D2D1::RectF(24, 80, 300, 104));

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
        case WM_SETTINGCHANGE:
            themeMode_ = Theme::DetectSystemTheme();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            HandleMouseMove(D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lParam)),
                                           static_cast<float>(GET_Y_LPARAM(lParam))));
            return 0;
        case WM_LBUTTONUP:
            HandleLeftButtonUp(D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lParam)),
                                              static_cast<float>(GET_Y_LPARAM(lParam))));
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void MainWindow::HandleMouseMove(D2D1_POINT_2F point) {
    bool hovered = helloButton_->HitTest(point);
    helloButton_->SetHovered(hovered);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::HandleLeftButtonUp(D2D1_POINT_2F point) {
    if (helloButton_->HitTest(point)) {
        helloButton_->Click();
    }
}

void MainWindow::Paint() {
    ID2D1DeviceContext* context = renderDevice_.BeginDraw();
    if (!context) {
        ValidateRect(hwnd_, nullptr);
        return;
    }
    const ThemeColors& theme = Theme::ColorsFor(themeMode_);
    context->Clear(theme.background);
    helloButton_->Draw(context, renderDevice_.DWriteFactory(), theme);
    helloText_->Draw(context, renderDevice_.DWriteFactory(), theme);
    if (!renderDevice_.EndDraw()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    ValidateRect(hwnd_, nullptr);
}

}  // namespace tw::app
