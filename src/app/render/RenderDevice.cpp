// src/app/render/RenderDevice.cpp
#include "pch.h"
#include "render/RenderDevice.h"

using Microsoft::WRL::ComPtr;

namespace tw::app {

bool RenderDevice::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    return CreateDeviceResources(hwnd);
}

bool RenderDevice::CreateDeviceResources(HWND hwnd) {
    ReleaseDeviceResources();

    UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags,
        nullptr, 0, D3D11_SDK_VERSION,
        d3dDevice_.ReleaseAndGetAddressOf(), &featureLevel,
        d3dContext_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3dDevice_.As(&dxgiDevice))) {
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf()))) {
        return false;
    }
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(dxgiFactory.ReleaseAndGetAddressOf())))) {
        return false;
    }

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = static_cast<UINT>(clientRect.right - clientRect.left);
    swapChainDesc.Height = static_cast<UINT>(clientRect.bottom - clientRect.top);
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (FAILED(dxgiFactory->CreateSwapChainForHwnd(
            d3dDevice_.Get(), hwnd, &swapChainDesc, nullptr, nullptr,
            swapChain_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    D2D1_FACTORY_OPTIONS factoryOptions{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factoryOptions,
                                  d2dFactory_.ReleaseAndGetAddressOf()))) {
        return false;
    }
    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), d2dDevice_.ReleaseAndGetAddressOf()))) {
        return false;
    }
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                d2dContext_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    ComPtr<IDXGISurface> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf())))) {
        return false;
    }
    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpiX_, dpiY_);
    ComPtr<ID2D1Bitmap1> targetBitmap;
    if (FAILED(d2dContext_->CreateBitmapFromDxgiSurface(
            backBuffer.Get(), &bitmapProperties, targetBitmap.ReleaseAndGetAddressOf()))) {
        return false;
    }
    d2dContext_->SetTarget(targetBitmap.Get());
    d2dContext_->SetDpi(dpiX_, dpiY_);

    if (!dwriteFactory_) {
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf())))) {
            return false;
        }
    }

    return true;
}

void RenderDevice::ReleaseDeviceResources() {
    if (d2dContext_) {
        d2dContext_->SetTarget(nullptr);
    }
    d2dContext_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    swapChain_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();
}

void RenderDevice::Resize(UINT width, UINT height) {
    if (!swapChain_ || width == 0 || height == 0) {
        return;
    }
    d2dContext_->SetTarget(nullptr);

    if (FAILED(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
        CreateDeviceResources(hwnd_);
        return;
    }

    ComPtr<IDXGISurface> backBuffer;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf()));
    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpiX_, dpiY_);
    ComPtr<ID2D1Bitmap1> targetBitmap;
    d2dContext_->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bitmapProperties,
                                              targetBitmap.ReleaseAndGetAddressOf());
    d2dContext_->SetTarget(targetBitmap.Get());
}

void RenderDevice::SetDpi(float dpiX, float dpiY) {
    dpiX_ = dpiX;
    dpiY_ = dpiY;
    if (d2dContext_) {
        d2dContext_->SetDpi(dpiX_, dpiY_);
    }
}

ID2D1DeviceContext* RenderDevice::BeginDraw() {
    if (!d2dContext_) {
        return nullptr;
    }
    d2dContext_->BeginDraw();
    return d2dContext_.Get();
}

bool RenderDevice::EndDraw() {
    HRESULT hr = d2dContext_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        CreateDeviceResources(hwnd_);
        return false;
    }
    hr = swapChain_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        CreateDeviceResources(hwnd_);
        return false;
    }
    return true;
}

}  // namespace tw::app
