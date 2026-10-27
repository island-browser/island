#ifndef ISLAND_FAVORITE_TABS_H_
#define ISLAND_FAVORITE_TABS_H_

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "bookmark_store.h"
#include "session_store.h"

namespace island {

// Identity of one favorite's virtual ("phantom") tab: the favorite list it sits in (std::nullopt is
// the global strip, otherwise a space's SpaceId value) plus its bookmark ID. The same bookmark in
// two lists is two virtual tabs.
struct FavoriteTabKey {
    FavoriteScope scope;
    std::uint64_t bookmark_id = 0;

    bool operator==(const FavoriteTabKey&) const = default;
    auto operator<=>(const FavoriteTabKey&) const = default;
};

// The restorable state of one virtual tab, kept in session.json. |reset_url| is the favorite URL
// the tab returns to when it is closed; |last_url| is where it was when the session was saved.
struct FavoriteTabRecord {
    std::string reset_url;
    std::string last_url;

    bool operator==(const FavoriteTabRecord&) const = default;
};

// CEF-free registry of virtual-tab records. BrowserWindow pairs a record with its live Tab, if any,
// and owns the rules for when a record is created or disposed; this type only stores them.
class FavoriteTabRegistry {
  public:
    // Returns the record for |key|, creating it at |reset_url| when missing. An existing record
    // keeps its last URL and takes |reset_url| as its new reset URL (the favorite may be edited).
    FavoriteTabRecord& Ensure(const FavoriteTabKey& key, std::string_view reset_url);
    [[nodiscard]] const FavoriteTabRecord* Find(const FavoriteTabKey& key) const;
    // Does nothing when no record exists for |key|.
    void SetLastUrl(const FavoriteTabKey& key, std::string_view last_url);
    // Closing a virtual tab: its next activation starts at the reset URL.
    void ResetToFavorite(const FavoriteTabKey& key);
    // Returns true when a record was removed.
    bool Dispose(const FavoriteTabKey& key);
    // Removes every record of |bookmark_id|, in every scope; returns the removed keys.
    std::vector<FavoriteTabKey> DisposeBookmark(std::uint64_t bookmark_id);
    // Removes every record of one favorite list; returns the removed keys.
    std::vector<FavoriteTabKey> DisposeScope(const FavoriteScope& scope);

    [[nodiscard]] const std::map<FavoriteTabKey, FavoriteTabRecord>& records() const noexcept {
        return records_;
    }
    // Stores a record as restored from the session (replaces any record with the same key).
    void Put(const FavoriteTabKey& key, FavoriteTabRecord record);

  private:
    std::map<FavoriteTabKey, FavoriteTabRecord> records_;
};

// Session form. A global record has no space_id; a scoped record stores its SpaceId.
[[nodiscard]] std::vector<FavoriteTabState> ToSessionFavoriteTabs(
    const FavoriteTabRegistry& registry);
[[nodiscard]] FavoriteTabRegistry FavoriteTabsFromSession(
    const std::vector<FavoriteTabState>& states);

}  // namespace island

#endif  // ISLAND_FAVORITE_TABS_H_
