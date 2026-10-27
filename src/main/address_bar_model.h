#ifndef ISLAND_ADDRESS_BAR_MODEL_H_
#define ISLAND_ADDRESS_BAR_MODEL_H_

#include <optional>
#include <string>
#include <string_view>

#include "address_policy.h"

namespace island {

// Drawn in the address field while it is empty and at rest (Native projects the field through a
// label, so the label shows this text). It is a placeholder, never the field's text.
inline constexpr std::string_view kAddressPlaceholderText = "Search or enter address";

enum class AddressBarMode {
    kResting,
    kEditing,
    kInvalid,
};

struct AddressBarSnapshot {
    std::string display_text;
    std::string edit_text;
    AddressBarMode mode = AddressBarMode::kResting;
    std::optional<AddressError> validation_error;

    bool operator==(const AddressBarSnapshot&) const = default;
};

class AddressBarModel {
  public:
    [[nodiscard]] const AddressBarSnapshot& snapshot() const noexcept;

    void UpdateCommittedUrl(std::string url);
    void UpdateInternalPage(std::string display_url);
    // A results page of a search engine: the field shows the query it was opened for.
    void UpdateSearchQuery(std::string query);
    void Focus();
    void SetEditText(std::string text);
    [[nodiscard]] std::optional<std::string> Submit(const ValidatedAddress& address);
    void Blur();
    void Escape();

  private:
    void UpdateCommittedText(std::string text);
    void RestoreCommittedState();

    std::string committed_text_;
    AddressBarSnapshot snapshot_;
};

}  // namespace island

#endif
