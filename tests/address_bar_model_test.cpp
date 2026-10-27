#include "address_bar_model.h"

#include <gtest/gtest.h>

namespace island {
namespace {

constexpr char kCommittedUrl[] = "https://example.test/current";
constexpr char kDraftUrl[] = "https://next.example.test/";

ValidatedAddress ValidDraft() { return {.url = kDraftUrl}; }

TEST(AddressBarModel, NeverExposesAnUnknownDataPageUrl) {
    AddressBarModel model;
    model.UpdateCommittedUrl("data:text/html;charset=utf-8,Island%20Browser");

    AddressBarSnapshot snapshot = model.snapshot();
    EXPECT_EQ(snapshot.mode, AddressBarMode::kResting);
    EXPECT_EQ(snapshot.display_text, "island://local-page");
    EXPECT_EQ(snapshot.edit_text, "island://local-page");
    EXPECT_EQ(snapshot.validation_error, std::nullopt);

    model.Focus();
    snapshot = model.snapshot();
    EXPECT_EQ(snapshot.edit_text, "island://local-page");
    EXPECT_EQ(snapshot.edit_text.find("data:"), std::string::npos);
}

TEST(AddressBarModel, UsesAnInternalPagesOwnDisplayUrlAndLeavesStartEmpty) {
    AddressBarModel model;
    model.UpdateInternalPage("island://settings");
    model.Focus();
    EXPECT_EQ(model.snapshot().display_text, "island://settings");
    EXPECT_EQ(model.snapshot().edit_text, "island://settings");

    model.Blur();
    model.UpdateInternalPage("");
    model.Focus();
    EXPECT_TRUE(model.snapshot().display_text.empty());
    EXPECT_TRUE(model.snapshot().edit_text.empty());
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

TEST(AddressBarModel, ASearchQueryShowsInsteadOfTheResultsUrlAndEditingKeepsIt) {
    AddressBarModel model;
    model.UpdateCommittedUrl(kCommittedUrl);
    model.UpdateSearchQuery("island browser");

    EXPECT_EQ(model.snapshot().display_text, "island browser");
    EXPECT_EQ(model.snapshot().edit_text, "island browser");

    model.Focus();
    EXPECT_EQ(model.snapshot().mode, AddressBarMode::kEditing);
    EXPECT_EQ(model.snapshot().edit_text, "island browser");

    model.Blur();
    EXPECT_EQ(model.snapshot().display_text, "island browser");
    EXPECT_EQ(model.snapshot().edit_text, "island browser");
}

TEST(AddressBarModel, ASubmittedSearchReplacesTheQueryWithTheRealUrl) {
    AddressBarModel model;
    model.UpdateSearchQuery("island browser");
    model.Focus();
    model.SetEditText("island browser");

    const std::optional<std::string> url =
        model.Submit({.url = "https://www.google.com/search?q=island%20browser"});
    EXPECT_EQ(url, "https://www.google.com/search?q=island%20browser");
    EXPECT_EQ(model.snapshot().display_text, "https://www.google.com/search?q=island%20browser");
}

// The placeholder is painted by BrowserChrome::ProjectAddress, which this unit test does not reach.
// Here the model side is checked: an empty start page keeps an empty field, and focusing it does
// not turn the placeholder into edit text.
TEST(AddressBarModel, GivenAnEmptyStartPageWhenFocusedThenThePlaceholderIsNotTheFieldText) {
    EXPECT_EQ(kAddressPlaceholderText, "Search or enter address");

    AddressBarModel model;
    model.UpdateInternalPage("");
    EXPECT_EQ(model.snapshot().display_text, "");
    model.Focus();
    EXPECT_EQ(model.snapshot().edit_text, "");
}

}  // namespace
}  // namespace island
