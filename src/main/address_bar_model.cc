#include "address_bar_model.h"

#include <string_view>
#include <utility>

namespace island {
namespace {

constexpr std::string_view kFallbackInternalDisplayUrl = "island://local-page";

}  // namespace

const AddressBarSnapshot& AddressBarModel::snapshot() const noexcept { return snapshot_; }

void AddressBarModel::UpdateCommittedUrl(std::string url) {
    // A data: URL may contain an entire built-in page. Never retain that payload as text the
    // address field can reveal; callers with a known page use UpdateInternalPage instead.
    UpdateCommittedText(url.starts_with("data:") ? std::string(kFallbackInternalDisplayUrl)
                                                 : std::move(url));
}

void AddressBarModel::UpdateInternalPage(std::string display_url) {
    UpdateCommittedText(std::move(display_url));
}

void AddressBarModel::UpdateSearchQuery(std::string query) {
    UpdateCommittedText(std::move(query));
}

void AddressBarModel::UpdateCommittedText(std::string text) {
    committed_text_ = std::move(text);
    snapshot_.display_text = committed_text_;
    if (snapshot_.mode == AddressBarMode::kResting) {
        snapshot_.edit_text = committed_text_;
    }
}

void AddressBarModel::Focus() {
    if (snapshot_.mode == AddressBarMode::kResting) {
        snapshot_.edit_text = committed_text_;
    }
    snapshot_.mode = AddressBarMode::kEditing;
    snapshot_.validation_error.reset();
}

void AddressBarModel::SetEditText(std::string text) {
    snapshot_.edit_text = std::move(text);
    if (snapshot_.mode != AddressBarMode::kResting) {
        snapshot_.mode = AddressBarMode::kEditing;
        snapshot_.validation_error.reset();
    }
}

std::optional<std::string> AddressBarModel::Submit(const ValidatedAddress& address) {
    if (snapshot_.mode == AddressBarMode::kResting) {
        return std::nullopt;
    }
    if (!address.is_valid()) {
        snapshot_.mode = AddressBarMode::kInvalid;
        snapshot_.validation_error = address.error;
        return std::nullopt;
    }

    committed_text_ = address.url;
    RestoreCommittedState();
    return committed_text_;
}

void AddressBarModel::Blur() { RestoreCommittedState(); }

void AddressBarModel::Escape() { RestoreCommittedState(); }

void AddressBarModel::RestoreCommittedState() {
    snapshot_.display_text = committed_text_;
    snapshot_.edit_text = committed_text_;
    snapshot_.mode = AddressBarMode::kResting;
    snapshot_.validation_error.reset();
}

}  // namespace island
