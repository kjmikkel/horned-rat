#pragma once

#include "core/geometry.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hr::platform {

// Shows captured frames in a window through a flip-model D3D11 swap chain. A2.1 copies 1:1; the
// scaling shader arrives with A2.2.
class D3DPresenter {
  public:
    enum class Driver { Hardware, Warp };

    // Hardware device first (when allowed), then WARP. nullptr + `error` if neither works.
    static std::unique_ptr<D3DPresenter> create(HWND window, core::Size size, bool allow_hardware,
                                                std::string& error);

    Driver driver() const { return driver_; }

    // Clears the back buffer to black and copies the BGRA frame 1:1 to `dest`, clipped.
    void render(const std::uint32_t* bgra, core::Size frame, core::Rect dest);
    void present(bool vsync);

    // The current back buffer as 0x??RRGGBB pixels (tests).
    std::vector<std::uint32_t> read_back();

  private:
    D3DPresenter() = default;
    bool ensure_frame_texture(core::Size frame);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_;
    core::Size frame_size_{};
    core::Size size_{};
    Driver driver_ = Driver::Warp;
};

} // namespace hr::platform
