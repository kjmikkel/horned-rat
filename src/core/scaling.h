#pragma once

#include "core/config.h"
#include "core/geometry.h"

namespace hr::core {

// Where the picture goes in the presenter's client area (Stage A spec §5.3):
// - integer: the largest whole multiple that fits (with aspect = stretch, per axis), at least 1x,
//   centred; it overflows a client smaller than the picture.
// - sharp / nearest-fit: the largest fit with the picture's own aspect, centred; with
//   aspect = stretch, the whole client.
// An empty client or picture gives an empty rect.
Rect dest_rect(Size client, Size content, ScaleMode scale, AspectMode aspect);

// Whole-number prescale for sharp-bilinear: the whole part of dest / content per axis, at least 1.
Size sharp_prescale(Size content, Rect dest);

// `wanted` shrunk, keeping its aspect, until it fits `available`; unchanged when it fits.
Size fit_inside(Size wanted, Size available);

} // namespace hr::core
