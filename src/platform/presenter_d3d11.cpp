#include "platform/presenter_d3d11.h"

#include "core/scaling.h"

#include <d3dcompiler.h>

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

// One quad from SV_VertexID (no vertex buffer), drawn into a viewport equal to the destination
// rect. mode 0 = point; mode 1 = sharp-bilinear: inside a texel's centre region the sample snaps
// to the texel centre, only a band of width 1/prescale at the edges is blended.
constexpr char kShader[] = R"(
cbuffer Params : register(b0) {
    float2 tex_size;
    float2 prescale;
    uint mode;
};
Texture2D frame_tex : register(t0);
SamplerState point_sampler : register(s0);
SamplerState linear_sampler : register(s1);

struct VsOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VsOut vs_main(uint id : SV_VertexID) {
    VsOut o;
    const float2 uv = float2(id & 1, id >> 1);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

float4 ps_main(VsOut i) : SV_Target {
    if (mode == 0) {
        return frame_tex.Sample(point_sampler, i.uv);
    }
    const float2 texel = i.uv * tex_size;
    const float2 base = floor(texel);
    const float2 dist = frac(texel) - 0.5;
    const float2 range = 0.5 - 0.5 / prescale;
    const float2 f = (dist - clamp(dist, -range, range)) * prescale + 0.5;
    return frame_tex.Sample(linear_sampler, (base + f) / tex_size);
}
)";

struct Constants {
    float tex_size[2];
    float prescale[2];
    std::uint32_t mode;
    std::uint32_t padding[3];
};
static_assert(sizeof(Constants) == 32);
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
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER); // Alt+Enter is ours

    p->size_ = size;
    if (!p->create_target()) {
        error = "back buffer setup failed";
        return nullptr;
    }
    if (!p->create_pipeline(error)) {
        return nullptr;
    }
    return p;
}

bool D3DPresenter::create_target() {
    return SUCCEEDED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer_))) &&
           SUCCEEDED(device_->CreateRenderTargetView(back_buffer_.Get(), nullptr, &target_));
}

bool D3DPresenter::create_pipeline(std::string& error) {
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> messages;
    HRESULT hr = D3DCompile(kShader, sizeof kShader - 1, "presenter", nullptr, nullptr, "vs_main",
                            "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &messages);
    if (SUCCEEDED(hr)) {
        hr = D3DCompile(kShader, sizeof kShader - 1, "presenter", nullptr, nullptr, "ps_main",
                        "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &messages);
    }
    if (FAILED(hr)) {
        error = hresult_text("shader compile", hr);
        if (messages) {
            error += ": ";
            error.append(static_cast<const char*>(messages->GetBufferPointer()),
                         messages->GetBufferSize());
        }
        return false;
    }
    if (FAILED(hr = device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                                nullptr, &vertex_shader_)) ||
        FAILED(hr = device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                               &pixel_shader_))) {
        error = hresult_text("shader creation", hr);
        return false;
    }

    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Constants);
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_SAMPLER_DESC linear = sampler;
    linear.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    D3D11_BLEND_DESC blend{};
    D3D11_RENDER_TARGET_BLEND_DESC& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(hr = device_->CreateBuffer(&buffer, nullptr, &constants_)) ||
        FAILED(hr = device_->CreateSamplerState(&sampler, &point_sampler_)) ||
        FAILED(hr = device_->CreateSamplerState(&linear, &linear_sampler_)) ||
        FAILED(hr = device_->CreateRasterizerState(&raster, &rasterizer_)) ||
        FAILED(hr = device_->CreateBlendState(&blend, &alpha_blend_))) {
        error = hresult_text("pipeline state", hr);
        return false;
    }
    return true;
}

bool D3DPresenter::resize(core::Size size) {
    if (size == size_ || size.width <= 0 || size.height <= 0) {
        return true;
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset();
    back_buffer_.Reset();
    context_->Flush();
    if (FAILED(swap_chain_->ResizeBuffers(0, static_cast<UINT>(size.width),
                                          static_cast<UINT>(size.height), DXGI_FORMAT_UNKNOWN,
                                          0))) {
        return false;
    }
    size_ = size;
    return create_target();
}

bool D3DPresenter::ensure_texture(ComPtr<ID3D11Texture2D>& texture,
                                  ComPtr<ID3D11ShaderResourceView>& view, core::Size& current,
                                  core::Size size) {
    if (texture && size == current) {
        return true;
    }
    texture.Reset();
    view.Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(size.width);
    desc.Height = static_cast<UINT>(size.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
        texture.Reset();
        view.Reset();
        return false;
    }
    current = size;
    return true;
}

void D3DPresenter::render(const std::uint32_t* bgra, core::Size frame, core::Rect dest,
                          Filter filter) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context_->ClearRenderTargetView(target_.Get(), black);
    if (bgra == nullptr || frame.width <= 0 || frame.height <= 0 || dest.width <= 0 ||
        dest.height <= 0 || !ensure_texture(frame_, frame_view_, frame_size_, frame)) {
        return;
    }
    context_->UpdateSubresource(frame_.Get(), 0, nullptr, bgra, static_cast<UINT>(frame.width) * 4,
                                0);

    const core::Size prescale = core::sharp_prescale(frame, dest);
    const Constants constants{
        {static_cast<float>(frame.width), static_cast<float>(frame.height)},
        {static_cast<float>(prescale.width), static_cast<float>(prescale.height)},
        filter == Filter::Sharp ? 1u : 0u,
        {}};
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);

    draw_quad(frame_view_.Get(), dest, nullptr);
}

void D3DPresenter::draw_quad(ID3D11ShaderResourceView* view, core::Rect where,
                             ID3D11BlendState* blend) {
    // The viewport is the target rect; parts outside the back buffer are clipped.
    const D3D11_VIEWPORT viewport{static_cast<float>(where.x),
                                  static_cast<float>(where.y),
                                  static_cast<float>(where.width),
                                  static_cast<float>(where.height),
                                  0.0f,
                                  1.0f};
    ID3D11RenderTargetView* const targets[] = {target_.Get()};
    ID3D11Buffer* const buffers[] = {constants_.Get()};
    ID3D11ShaderResourceView* const views[] = {view};
    ID3D11SamplerState* const samplers[] = {point_sampler_.Get(), linear_sampler_.Get()};
    context_->OMSetRenderTargets(1, targets, nullptr);
    context_->OMSetBlendState(blend, nullptr, 0xFFFFFFFFu);
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(rasterizer_.Get());
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, buffers);
    context_->PSSetShaderResources(0, 1, views);
    context_->PSSetSamplers(0, 2, samplers);
    context_->Draw(4, 0);
}

void D3DPresenter::present(bool vsync) { swap_chain_->Present(vsync ? 1 : 0, 0); }

bool D3DPresenter::set_overlay(const std::uint32_t* bgra, core::Size size) {
    if (bgra == nullptr || size.width <= 0 || size.height <= 0 ||
        !ensure_texture(overlay_, overlay_view_, overlay_size_, size)) {
        return false;
    }
    context_->UpdateSubresource(overlay_.Get(), 0, nullptr, bgra, static_cast<UINT>(size.width) * 4,
                                0);
    return true;
}

void D3DPresenter::draw_overlay(core::Rect where, Filter filter) {
    if (!overlay_view_ || where.width <= 0 || where.height <= 0) {
        return;
    }
    const core::Size prescale = core::sharp_prescale(overlay_size_, where);
    const Constants constants{
        {static_cast<float>(overlay_size_.width), static_cast<float>(overlay_size_.height)},
        {static_cast<float>(prescale.width), static_cast<float>(prescale.height)},
        filter == Filter::Sharp ? 1u : 0u,
        {}};
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
    draw_quad(overlay_view_.Get(), where, alpha_blend_.Get());
}

bool D3DPresenter::add_sprite(std::uintptr_t key, const std::uint32_t* bgra, core::Size size) {
    if (bgra == nullptr || size.width <= 0 || size.height <= 0) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(size.width);
    desc.Height = static_cast<UINT>(size.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{bgra, static_cast<UINT>(size.width) * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device_->CreateTexture2D(&desc, &data, &texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
        return false;
    }
    sprites_[key] = view;
    return true;
}

bool D3DPresenter::draw_sprite(std::uintptr_t key, core::Rect where) {
    const auto it = sprites_.find(key);
    if (it == sprites_.end() || where.width <= 0 || where.height <= 0) {
        return false;
    }
    const Constants constants{{1.0f, 1.0f}, {1.0f, 1.0f}, 0u, {}}; // mode 0: point sampling
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
    draw_quad(it->second.Get(), where, alpha_blend_.Get());
    return true;
}

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
