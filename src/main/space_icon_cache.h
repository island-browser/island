#ifndef ISLAND_SPACE_ICON_CACHE_H_
#define ISLAND_SPACE_ICON_CACHE_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace island {

// Memoizes recolored space icons so ordinary chrome updates never touch the disk. The key is the
// icon id, pixel size and color; the image already carries both the 1x and 2x representations, so
// the scale is not a separate key part. Failed loads are not cached, so a missing file is retried.
template <typename Image>
class SpaceIconCache {
  public:
    using Loader = std::function<std::optional<Image>()>;

    std::optional<Image> GetOrLoad(std::string_view icon_id, int size, std::uint32_t argb,
                                   const Loader& load) {
        Key key{std::string(icon_id), size, argb};
        if (const auto found = entries_.find(key); found != entries_.end()) {
            return found->second;
        }
        std::optional<Image> image = load();
        if (image.has_value()) {
            entries_.emplace(std::move(key), *image);
        }
        return image;
    }

    void Clear() { entries_.clear(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  private:
    using Key = std::tuple<std::string, int, std::uint32_t>;
    std::map<Key, Image> entries_;
};

}  // namespace island

#endif  // ISLAND_SPACE_ICON_CACHE_H_
