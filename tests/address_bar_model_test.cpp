#include "address_bar_model.h"

#include <gtest/gtest.h>

namespace island {
namespace {

constexpr char kCommittedUrl[] = "https://example.test/current";
constexpr char kDraftUrl[] = "https://next.example.test/";

ValidatedAddress ValidDraft() { return {.url = kDraftUrl}; }

TEST(AddressBarModel, RestsWithContextualDataPageDisplay) {
    AddressBarModel model;
    model.UpdateCommittedUrl("data:text/html;charset=utf-8,Island%20Browser");

    const AddressBarSnapshot snapshot = model.snapshot();
    EXPECT_EQ(snapshot.mode, AddressBarMode::kResting);
    EXPECT_EQ(snapshot.display_text, "Island");
    EXPECT_EQ(snapshot.edit_text, "data:text/html;charset=utf-8,Island%20Browser");
    EXPECT_EQ(snapshot.validation_error, std::nullopt);
}

TEST(AddressBarModel, EntersEditingWithTheLatestCommittedUrl) {
    AddressBarModel model;
    model.UpdateCommittedUrl(kCommittedUrl);

    model.Focus();

    const AddressBarSnapshot snapshot = model.snapshot();
    EXPECT_EQ(snapshot.mode, AddressBarMode::kEditing);
    EXPECT_EQ(snapshot.display_text, kCommittedUrl);
    EXPECT_EQ(snapshot.edit_text, kCommittedUrl);
}

TEST(AddressBarModel, PreservesAnEditingDraftAcrossNavigationUpdates) {
    AddressBarModel model;
    model.UpdateCommittedUrl(kCommittedUrl);
    model.Focus();
    model.SetEditText("https://typed.example.test/");

    model.UpdateCommittedUrl("https://navigated.example.test/");

    EXPECT_EQ(model.snapshot().mode, AddressBarMode::kEditing);
    EXPECT_EQ(model.snapshot().edit_text, "https://typed.example.test/");
    EXPECT_EQ(model.snapshot().display_text, "https://navigated.example.test/");
}

TEST(AddressBarModel, ReturnsAValidSubmissionOnlyOnceAndRestoresDisplayMode) {
    AddressBarModel model;
    model.UpdateCommittedUrl(kCommittedUrl);
    model.Focus();
    model.SetEditText(kDraftUrl);

    EXPECT_EQ(model.Submit(ValidDraft()), std::optional<std::string>(kDraftUrl));
    EXPECT_EQ(model.Submit(ValidDraft()), std::nullopt);
    EXPECT_EQ(model.snapshot().mode, AddressBarMode::kResting);
    EXPECT_EQ(model.snapshot().display_text, kDraftUrl);
}

TEST(AddressBarModel, RetainsFocusDraftAndErrorForInvalidSubmission) {
    AddressBarModel model;
    model.UpdateCommittedUrl(kCommittedUrl);
    model.Focus();
    model.SetEditText("not a url");

    EXPECT_EQ(model.Submit({.error = AddressError::kNotAbsolute}), std::nullopt);
    EXPECT_EQ(model.snapshot().mode, AddressBarMode::kInvalid);
    EXPECT_EQ(model.snapshot().edit_text, "not a url");
    EXPECT_EQ(model.snapshot().validation_error, AddressError::kNotAbsolute);
}

TEST(AddressBarModel, BlurAndEscapeCancelToTheLatestCommittedState) {
    AddressBarModel model;
    model.UpdateCommittedUrl(kCommittedUrl);
    model.Focus();
    model.SetEditText("https://typed.example.test/");
    model.UpdateCommittedUrl("https://navigated.example.test/");

    model.Blur();
    EXPECT_EQ(model.snapshot().mode, AddressBarMode::kResting);
    EXPECT_EQ(model.snapshot().display_text, "https://navigated.example.test/");
    EXPECT_EQ(model.snapshot().edit_text, "https://navigated.example.test/");

    model.Focus();
    model.SetEditText("https://another.example.test/");
    model.Escape();
    EXPECT_EQ(model.snapshot().mode, AddressBarMode::kResting);
    EXPECT_EQ(model.snapshot().edit_text, "https://navigated.example.test/");
}

}  // namespace
}  // namespace island
