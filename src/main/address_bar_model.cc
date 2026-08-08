#include "address_bar_model.h"

#include <string_view>
#include <utility>

namespace island {
namespace {

constexpr std::string_view kDataPageDisplayText = "Island";

std::string DisplayTextForUrl(const std::string& url) {
    return url.starts_with("data:") ? std::string(kDataPageDisplayText) : url;
}

}  // namespace

const AddressBarSnapshot& AddressBarModel::snapshot() const noexcept { return snapshot_; }

void AddressBarModel::UpdateCommittedUrl(std::string url) {
    committed_url_ = std::move(url);
    snapshot_.display_text = DisplayTextForUrl(committed_url_);
    if (snapshot_.mode == AddressBarMode::kResting) {
        snapshot_.edit_text = committed_url_;
    }
}

void AddressBarModel::Focus() {
    if (snapshot_.mode == AddressBarMode::kResting) {
        snapshot_.edit_text = committed_url_;
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

    committed_url_ = address.url;
    RestoreCommittedState();
    return committed_url_;
}

void AddressBarModel::Blur() { RestoreCommittedState(); }

void AddressBarModel::Escape() { RestoreCommittedState(); }

void AddressBarModel::RestoreCommittedState() {
    snapshot_.display_text = DisplayTextForUrl(committed_url_);
    snapshot_.edit_text = committed_url_;
    snapshot_.mode = AddressBarMode::kResting;
    snapshot_.validation_error.reset();
}

}  // namespace island
