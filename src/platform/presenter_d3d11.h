#pragma once

#include "core/geometry.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hr::platform {

// Shows captured frames in a window through a flip-model D3D11 swap chain, scaled by a shader
// pass into a destination rect (A2 spec §5.1).
class D3DPresenter {
  public:
    enum class Driver { Hardware, Warp };
    enum class Filter { Point, Sharp };

    // Hardware device first (when allowed), then WARP; compiles the shaders. nullptr + `error`
    // if any of it fails.
    static std::unique_ptr<D3DPresenter> create(HWND window, core::Size size, bool allow_hardware,
                                                std::string& error);

    Driver driver() const { return driver_; }
    core::Size size() const { return size_; }

    // Resizes the back buffers. No-op for the current or an empty size; false = failed (the
    // presenter is then unusable).
    bool resize(core::Size size);

    // Clears to black and draws the BGRA frame scaled into `dest` (clipped to the back buffer).
    // Point = nearest neighbour; Sharp = sharp-bilinear with core::sharp_prescale.
    void render(const std::uint32_t* bgra, core::Size frame, core::Rect dest, Filter filter);
    void present(bool vsync);

    // Cursor sprites (A2 spec §5.3), kept per `key` for the presenter's lifetime. `bgra` is
    // straight-alpha 0xAARRGGBB. False if the texture cannot be made.
    bool add_sprite(std::uintptr_t key, const std::uint32_t* bgra, core::Size size);
    bool has_sprite(std::uintptr_t key) const { return sprites_.count(key) != 0; }
    // Blends the sprite over what render() drew, nearest-neighbour scaled into `where`, which may
    // extend past the back buffer. False for an unknown key or an empty rect.
    bool draw_sprite(std::uintptr_t key, core::Rect where);

    // The current back buffer as 0x??RRGGBB pixels (tests).
    std::vector<std::uint32_t> read_back();

  private:
    D3DPresenter() = default;
    bool create_pipeline(std::string& error);
    bool ensure_frame_texture(core::Size frame);
    bool create_target();
    // Binds the pipeline and draws the quad into `where` from `view` (constants already set).
    void draw_quad(ID3D11ShaderResourceView* view, core::Rect where, ID3D11BlendState* blend);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> frame_view_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> point_sampler_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> linear_sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> alpha_blend_;
    std::map<std::uintptr_t, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> sprites_;
    core::Size frame_size_{};
    core::Size size_{};
    Driver driver_ = Driver::Warp;
};

} // namespace hr::platform
