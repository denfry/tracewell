// src/app/render/RenderDevice.h
#pragma once

#include "pch.h"

namespace tw::app {

// Владеет D3D11-устройством, DXGI swap chain'ом и D2D1-device context'ом,
// привязанными к одному HWND. Пересоздаётся целиком при потере устройства
// (DXGI_ERROR_DEVICE_REMOVED/RESET, D2DERR_RECREATE_TARGET).
class RenderDevice {
public:
    bool Initialize(HWND hwnd);
    void Resize(UINT width, UINT height);
    void SetDpi(float dpiX, float dpiY);

    // BeginDraw возвращает nullptr, если устройство ещё не готово.
    // EndDraw возвращает false при потере устройства — контекст уже
    // пересоздан внутри, вызывающий код должен запросить перерисовку.
    ID2D1DeviceContext* BeginDraw();
    bool EndDraw();

    IDWriteFactory* DWriteFactory() const { return dwriteFactory_.Get(); }

private:
    bool CreateDeviceResources(HWND hwnd);
    void ReleaseDeviceResources();

    HWND hwnd_ = nullptr;
    float dpiX_ = 96.0f;
    float dpiY_ = 96.0f;

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
};

}  // namespace tw::app
