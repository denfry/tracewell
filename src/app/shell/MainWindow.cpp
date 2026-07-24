// src/app/shell/MainWindow.cpp
#include "pch.h"
#include "shell/MainWindow.h"

#include <windowsx.h>

using Microsoft::WRL::ComPtr;

namespace tw::app {

namespace {
constexpr wchar_t kClassName[] = L"TracewellMainWindow";
constexpr float kSidebarWidth = 200.0f;
}  // namespace

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
    BuildLayout();

    ShowWindow(hwnd_, cmdShow);
    UpdateWindow(hwnd_);
    return true;
}

void MainWindow::BuildLayout() {
    sidebar_ = std::make_shared<Panel>(PanelOrientation::Vertical, 4.0f);

    auto startupEntry = std::make_shared<Button>();
    startupEntry->SetText(L"Startup");
    sidebar_->AddChild(startupEntry, 40.0f);

    // Заглушка будущего раздела: SetOnClick не задан, поэтому клик не даёт эффекта.
    auto diskEntry = std::make_shared<Button>();
    diskEntry->SetText(L"Disk I/O (скоро)");
    sidebar_->AddChild(diskEntry, 40.0f);

    content_ = std::make_shared<Panel>(PanelOrientation::Vertical, 12.0f);

    refreshButton_ = std::make_shared<Button>();
    refreshButton_->SetText(L"Refresh");
    refreshButton_->SetOnClick([this] { viewModel_.Refresh(); });
    content_->AddChild(refreshButton_, 40.0f);

    statusText_ = std::make_shared<TextBlock>();
    statusText_->SetText(viewModel_.StatusText().Get());
    content_->AddChild(statusText_, 24.0f);

    viewModel_.StatusText().Subscribe([this](const std::wstring& text) {
        statusText_->SetText(text);
        InvalidateRect(hwnd_, nullptr, FALSE);
    });
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
        case WM_APP_DISPATCH:
            dispatcher_.DrainUiQueue();
            return 0;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE: {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            renderDevice_.Resize(width, height);
            sidebar_->SetBounds(D2D1::RectF(0, 0, kSidebarWidth, static_cast<float>(height)));
            sidebar_->Layout();
            content_->SetBounds(D2D1::RectF(kSidebarWidth + 16.0f, 16.0f,
                                             static_cast<float>(width) - 16.0f,
                                             static_cast<float>(height) - 16.0f));
            content_->Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
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
    Button* hit = refreshButton_->HitTest(point) ? refreshButton_.get() : nullptr;
    if (hit != hoveredButton_) {
        if (hoveredButton_) hoveredButton_->SetHovered(false);
        if (hit) hit->SetHovered(true);
        hoveredButton_ = hit;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::HandleLeftButtonUp(D2D1_POINT_2F point) {
    if (refreshButton_->HitTest(point)) {
        refreshButton_->Click();
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

    ComPtr<ID2D1SolidColorBrush> sidebarBrush;
    context->CreateSolidColorBrush(theme.surface, sidebarBrush.ReleaseAndGetAddressOf());
    context->FillRectangle(sidebar_->Bounds(), sidebarBrush.Get());

    sidebar_->Draw(context, renderDevice_.DWriteFactory(), theme);
    content_->Draw(context, renderDevice_.DWriteFactory(), theme);

    if (!renderDevice_.EndDraw()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    ValidateRect(hwnd_, nullptr);
}

}  // namespace tw::app
