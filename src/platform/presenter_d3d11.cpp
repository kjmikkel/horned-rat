#include "platform/presenter_d3d11.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hr::platform {

using Microsoft::WRL::ComPtr;

namespace {
std::string hresult_text(const char* what, HRESULT hr) {
    char text[96];
    std::snprintf(text, sizeof text, "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    return text;
}
} // namespace

std::unique_ptr<D3DPresenter> D3DPresenter::create(HWND window, core::Size size,
                                                   bool allow_hardware, std::string& error) {
    std::unique_ptr<D3DPresenter> p(new D3DPresenter());
    HRESULT hr = E_FAIL;
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (type == D3D_DRIVER_TYPE_HARDWARE && !allow_hardware) {
            continue;
        }
        hr = D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &p->device_, nullptr, &p->context_);
        if (SUCCEEDED(hr)) {
            p->driver_ = type == D3D_DRIVER_TYPE_HARDWARE ? Driver::Hardware : Driver::Warp;
            break;
        }
    }
    if (FAILED(hr)) {
        error = hresult_text("D3D11CreateDevice", hr);
        return nullptr;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(hr = p->device_.As(&dxgi_device)) ||
        FAILED(hr = dxgi_device->GetAdapter(&adapter)) ||
        FAILED(hr = adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        error = hresult_text("DXGI factory lookup", hr);
        return nullptr;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(size.width);
    desc.Height = static_cast<UINT>(size.height);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(hr = factory->CreateSwapChainForHwnd(p->device_.Get(), window, &desc, nullptr,
                                                    nullptr, &p->swap_chain_))) {
        error = hresult_text("CreateSwapChainForHwnd", hr);
        return nullptr;
    }
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER); // Alt+Enter is ours (A2.2)

    if (FAILED(hr = p->swap_chain_->GetBuffer(0, IID_PPV_ARGS(&p->back_buffer_))) ||
        FAILED(
            hr = p->device_->CreateRenderTargetView(p->back_buffer_.Get(), nullptr, &p->target_))) {
        error = hresult_text("back buffer setup", hr);
        return nullptr;
    }
    p->size_ = size;
    return p;
}

bool D3DPresenter::ensure_frame_texture(core::Size frame) {
    if (frame_ && frame == frame_size_) {
        return true;
    }
    frame_.Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(frame.width);
    desc.Height = static_cast<UINT>(frame.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &frame_))) {
        return false;
    }
    frame_size_ = frame;
    return true;
}

void D3DPresenter::render(const std::uint32_t* bgra, core::Size frame, core::Rect dest) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context_->ClearRenderTargetView(target_.Get(), black);
    if (bgra == nullptr || frame.width <= 0 || frame.height <= 0 || !ensure_frame_texture(frame)) {
        return;
    }
    context_->UpdateSubresource(frame_.Get(), 0, nullptr, bgra, static_cast<UINT>(frame.width) * 4,
                                0);

    const int left = std::max(dest.x, 0);
    const int top = std::max(dest.y, 0);
    const int right = std::min(dest.x + std::min(dest.width, frame.width), size_.width);
    const int bottom = std::min(dest.y + std::min(dest.height, frame.height), size_.height);
    if (right <= left || bottom <= top) {
        return;
    }
    const D3D11_BOX box{static_cast<UINT>(left - dest.x),  static_cast<UINT>(top - dest.y),    0,
                        static_cast<UINT>(right - dest.x), static_cast<UINT>(bottom - dest.y), 1};
    context_->CopySubresourceRegion(back_buffer_.Get(), 0, static_cast<UINT>(left),
                                    static_cast<UINT>(top), 0, frame_.Get(), 0, &box);
}

void D3DPresenter::present(bool vsync) { swap_chain_->Present(vsync ? 1 : 0, 0); }

std::vector<std::uint32_t> D3DPresenter::read_back() {
    D3D11_TEXTURE2D_DESC desc{};
    back_buffer_->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) {
        return {};
    }
    context_->CopyResource(staging.Get(), back_buffer_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return {};
    }
    std::vector<std::uint32_t> pixels(static_cast<size_t>(desc.Width) * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y) {
        std::memcpy(&pixels[static_cast<size_t>(y) * desc.Width],
                    static_cast<const BYTE*>(mapped.pData) +
                        static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(desc.Width) * 4);
    }
    context_->Unmap(staging.Get(), 0);
    return pixels;
}

} // namespace hr::platform
