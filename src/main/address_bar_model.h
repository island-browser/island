#ifndef ISLAND_ADDRESS_BAR_MODEL_H_
#define ISLAND_ADDRESS_BAR_MODEL_H_

#include <optional>
#include <string>

#include "address_policy.h"

namespace island {

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
    void Focus();
    void SetEditText(std::string text);
    [[nodiscard]] std::optional<std::string> Submit(const ValidatedAddress& address);
    void Blur();
    void Escape();

  private:
    void RestoreCommittedState();

    std::string committed_url_;
    AddressBarSnapshot snapshot_;
};

}  // namespace island

#endif
