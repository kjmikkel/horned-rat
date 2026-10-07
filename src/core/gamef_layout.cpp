#include "core/gamef_layout.h"

#include "core/text.h"

namespace hr::core {
namespace {

constexpr std::string_view kGogSha256 =
    "e8afe836e9c99b54a76643b2adb4edb52f56a991d53ab8af60f746129b1132d6";

constexpr GamefLayout kGog{
    .create_device = 0x57040,
    .set_device_mode = 0x56F00,
    .set_device_dither = 0x56EB0,
    .set_device_shades = 0x56E40,
    .create_viewport = 0x57BF0,
    .configure_viewport = 0x56800,
    .set_viewport_plane = 0x56790,
    .set_viewport_back = 0x56640,
    .set_viewport_flag_d8 = 0x57BB0,
    .set_viewport_flag_44 = 0x57B70,
    .clear_viewport = 0x58650,
    .render_viewport = 0x583C0,
    .destroy_object = 0x56520,
    .lookup_object = 0x56A20,
    .viewport_type = 0xFB0E8,
    .w_scene = 0x32860,
    .w_camera = 0x32868,
    .w_device = 0x32870,
    .w_viewport = 0x32874,
    .w_device_desc = 0x32878,
    .w_bits = 0x328FC,
    .w_active_width = 0x32968,
    .w_active_height = 0x3296C,
    .w_divisor = 0x329A0,
    .vp_back = 0xC4,
    .vp_front = 0xF4,
    .vp_flag_d8 = 0xD8,
    .vp_flag_44 = 0x44,
    .vp_plane_a = 0xF8,
    .vp_plane_b = 0xFC,
    .vp_plane_c = 0x104,
    .vp_plane_d = 0x100,
};

} // namespace

std::optional<GamefLayout> gamef_layout(std::string_view sha256_hex) {
    if (to_lower(sha256_hex) == kGogSha256) {
        return kGog;
    }
    return std::nullopt;
}

} // namespace hr::core
