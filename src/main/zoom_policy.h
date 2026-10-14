#ifndef ISLAND_ZOOM_POLICY_H_
#define ISLAND_ZOOM_POLICY_H_

// CEF-free page zoom policy. CEF stores zoom as a level where 0.0 is 100% and
// the scale is 1.2^level, so one step of 0.5 is about 9.5%. The clamp keeps
// the level between roughly 25% and 470%.

namespace island {

inline constexpr double kZoomLevelStep = 0.5;
inline constexpr double kZoomLevelMin = -7.5;
inline constexpr double kZoomLevelMax = 8.5;

// `current` moved by `steps` zoom steps (negative zooms out), clamped to the
// usable range.
[[nodiscard]] inline double SteppedZoomLevel(double current, int steps) noexcept {
    const double next = current + kZoomLevelStep * steps;
    if (next < kZoomLevelMin) {
        return kZoomLevelMin;
    }
    if (next > kZoomLevelMax) {
        return kZoomLevelMax;
    }
    return next;
}

}  // namespace island

#endif  // ISLAND_ZOOM_POLICY_H_
