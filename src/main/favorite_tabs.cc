#include "favorite_tabs.h"

#include <string>
#include <utility>
#include <vector>

#include "tab_id.h"

namespace island {

FavoriteTabRecord& FavoriteTabRegistry::Ensure(const FavoriteTabKey& key,
                                               std::string_view reset_url) {
    auto [it, inserted] = records_.try_emplace(key);
    if (inserted) {
        it->second.reset_url = std::string(reset_url);
        it->second.last_url = it->second.reset_url;
    } else {
        it->second.reset_url = std::string(reset_url);
    }
    return it->second;
}

const FavoriteTabRecord* FavoriteTabRegistry::Find(const FavoriteTabKey& key) const {
    const auto it = records_.find(key);
    return it == records_.end() ? nullptr : &it->second;
}

void FavoriteTabRegistry::SetLastUrl(const FavoriteTabKey& key, std::string_view last_url) {
    const auto it = records_.find(key);
    if (it != records_.end()) {
        it->second.last_url = std::string(last_url);
    }
}

void FavoriteTabRegistry::ResetToFavorite(const FavoriteTabKey& key) {
    const auto it = records_.find(key);
    if (it != records_.end()) {
        it->second.last_url = it->second.reset_url;
    }
}

bool FavoriteTabRegistry::Dispose(const FavoriteTabKey& key) { return records_.erase(key) > 0; }

std::vector<FavoriteTabKey> FavoriteTabRegistry::DisposeBookmark(std::uint64_t bookmark_id) {
    std::vector<FavoriteTabKey> removed;
    for (auto it = records_.begin(); it != records_.end();) {
        if (it->first.bookmark_id == bookmark_id) {
            removed.push_back(it->first);
            it = records_.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

std::vector<FavoriteTabKey> FavoriteTabRegistry::DisposeScope(const FavoriteScope& scope) {
    std::vector<FavoriteTabKey> removed;
    for (auto it = records_.begin(); it != records_.end();) {
        if (it->first.scope == scope) {
            removed.push_back(it->first);
            it = records_.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

void FavoriteTabRegistry::Put(const FavoriteTabKey& key, FavoriteTabRecord record) {
    records_[key] = std::move(record);
}

std::vector<FavoriteTabState> ToSessionFavoriteTabs(const FavoriteTabRegistry& registry) {
    std::vector<FavoriteTabState> states;
    states.reserve(registry.records().size());
    for (const auto& [key, record] : registry.records()) {
        FavoriteTabState state;
        if (key.scope.has_value()) {
            state.space_id = SpaceId{*key.scope};
        }
        state.bookmark_id = key.bookmark_id;
        state.reset_url = record.reset_url;
        state.last_url = record.last_url;
        states.push_back(std::move(state));
    }
    return states;
}

FavoriteTabRegistry FavoriteTabsFromSession(const std::vector<FavoriteTabState>& states) {
    FavoriteTabRegistry registry;
    for (const FavoriteTabState& state : states) {
        FavoriteTabKey key;
        if (state.space_id.has_value()) {
            key.scope = state.space_id->value;
        }
        key.bookmark_id = state.bookmark_id;
        registry.Put(key,
                     FavoriteTabRecord{.reset_url = state.reset_url, .last_url = state.last_url});
    }
    return registry;
}

}  // namespace island
