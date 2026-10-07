#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace hr::core {

// Where Stage B finds things in GAMEF.DLL (spec 2026-10-07 §4, §7). Function and data addresses
// are RVAs; `w_*` are offsets in the battle window's data block (GetWindowLongA(battle, 4)),
// `vp_*` offsets in a Reality Lab viewport object. Only the GOG build is known.
struct GamefLayout {
    // Reality Lab functions (__cdecl).
    std::uint32_t create_device;
    std::uint32_t set_device_mode;
    std::uint32_t set_device_dither;
    std::uint32_t set_device_shades;
    std::uint32_t create_viewport;
    std::uint32_t configure_viewport;
    std::uint32_t set_viewport_plane;
    std::uint32_t set_viewport_back;
    std::uint32_t set_viewport_flag_d8;
    std::uint32_t set_viewport_flag_44;
    std::uint32_t clear_viewport;
    std::uint32_t render_viewport;
    std::uint32_t destroy_object;
    std::uint32_t lookup_object;
    std::uint32_t viewport_type; // type tag address passed to lookup_object
    // Battle window data block.
    std::uint32_t w_scene;
    std::uint32_t w_camera;
    std::uint32_t w_device;
    std::uint32_t w_viewport;
    std::uint32_t w_device_desc; // 15 dwords
    std::uint32_t w_bits;
    std::uint32_t w_active_width;
    std::uint32_t w_active_height;
    std::uint32_t w_divisor;
    // Viewport object.
    std::uint32_t vp_back;
    std::uint32_t vp_front;
    std::uint32_t vp_flag_d8;
    std::uint32_t vp_flag_44;
    std::uint32_t vp_plane_a; // set_viewport_plane argument order: a, b, c, d
    std::uint32_t vp_plane_b;
    std::uint32_t vp_plane_c;
    std::uint32_t vp_plane_d;
};

// The layout for the GAMEF.DLL whose SHA-256 is `sha256_hex` (any case), or nullopt.
std::optional<GamefLayout> gamef_layout(std::string_view sha256_hex);

} // namespace hr::core
