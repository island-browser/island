#include "command_palette.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "address_policy.h"
#include "space_icon.h"
#include "tab_id.h"

namespace island {
namespace {

PaletteTabEntry Tab(std::uint64_t id, std::string title, std::string url) {
    return {.id = TabId{id}, .title = std::move(title), .url = std::move(url)};
}

PaletteSpaceEntry Space(std::uint64_t id, std::string name) {
    return {.id = SpaceId{id}, .name = std::move(name)};
}

PaletteBookmarkEntry Bookmark(std::string title, std::string url) {
    return {.title = std::move(title), .url = std::move(url)};
}

PaletteHistoryEntry History(std::string title, std::string url, double score) {
    return {.title = std::move(title),
            .url = std::move(url),
            .host = "history.test",
            .visited_at_ms = 100,
            .visit_count = 1,
            .score = score};
}

// For ranking tests, where the palette must never touch URL validation: the
// validator is only invoked from Submit() on a kUrl row.
PaletteUrlValidator NeverCalledValidator() {
    return [](std::string_view input) {
        ADD_FAILURE() << "validator invoked outside a kUrl submission: " << input;
        return ValidatedAddress{.url = "", .error = AddressError::kInvalidHost};
    };
}

CommandPaletteModel OpenedPalette(PaletteUrlValidator validator, std::vector<PaletteTabEntry> tabs,
                                  std::vector<PaletteSpaceEntry> spaces) {
    CommandPaletteModel palette(std::move(validator));
    palette.Open();
    palette.SetTabs(std::move(tabs));
    palette.SetSpaces(std::move(spaces));
    return palette;
}

// A stand-in for the real island::ParseAndValidate (cef_address_parser.h),
// which cannot be linked into a CEF-free harness because it calls CefParseURL.
// It is table-driven over exactly the input corpus of
// tests/cef_address_parser_test.cpp — same inputs, same outcomes, and the same
// assertion strength (a pinned AddressError only where that suite pins one).
// The integrator must bind the real ParseAndValidate; these tests then pin the
// delegation contract: the palette forwards raw text and returns the
// validator's address verbatim.
struct ValidatorCase {
    std::string input;
    bool valid = false;
    std::string canonical_url;
    std::optional<AddressError> error;
};

ValidatorCase Accepts(std::string input, std::string canonical_url) {
    return {.input = std::move(input),
            .valid = true,
            .canonical_url = std::move(canonical_url),
            .error = std::nullopt};
}

ValidatorCase Rejects(std::string input, AddressError error) {
    return {.input = std::move(input), .valid = false, .canonical_url = "", .error = error};
}

// Cases where tests/cef_address_parser_test.cpp asserts only !is_valid(), so
// the parity check below asserts validity only, not the error code.
ValidatorCase RejectsUnpinned(std::string input) {
    return Rejects(std::move(input), AddressError::kInvalidHost);
}

const std::vector<ValidatorCase>& ValidatorCorpus() {
    static const std::vector<ValidatorCase> corpus = {
        Accepts(" \thTTps://WWW.Example.test:443/a/../path?q=value#section\r\n",
                "https://www.example.test/path?q=value#section"),
        Accepts("http://localhost:8080/", "http://localhost:8080/"),
        Accepts("https://api.localhost/", "https://api.localhost/"),
        Accepts("http://127.0.0.1:3000/", "http://127.0.0.1:3000/"),
        Accepts("https://[::1]/", "https://[::1]/"),
        Accepts("https://[::1]", "https://[::1]"),
        Accepts("https://[::1]:8443/", "https://[::1]:8443/"),
        Accepts("https://[::1]/path?query=value#section", "https://[::1]/path?query=value#section"),
        Accepts("https://example.test/", "https://example.test/"),
        Rejects("/relative/path", AddressError::kNotAbsolute),
        Rejects("example.test", AddressError::kNotAbsolute),
        Rejects("example test", AddressError::kInvalidCharacter),
        Rejects("search terms", AddressError::kInvalidCharacter),
        Rejects("data:text/html,Island", AddressError::kUnsupportedScheme),
        Rejects("file:///tmp/island.html", AddressError::kUnsupportedScheme),
        Rejects("javascript:alert(1)", AddressError::kUnsupportedScheme),
        Rejects("ftp://example.test/", AddressError::kUnsupportedScheme),
        Rejects("https://user@example.test/", AddressError::kCredentialsNotAllowed),
        Rejects("https://:password@example.test/", AddressError::kCredentialsNotAllowed),
        Rejects("https://example.test/a b", AddressError::kInvalidCharacter),
        Rejects("https://example.test/a\tb", AddressError::kInvalidCharacter),
        Rejects("https://example.test/a\nb", AddressError::kInvalidCharacter),
        Rejects(std::string("https://example.test/") + '\0', AddressError::kInvalidCharacter),
        Rejects("https://example.test:0/", AddressError::kInvalidPort),
        Rejects("https://example.test:65536/", AddressError::kInvalidPort),
        Rejects("https://example.test:abc/", AddressError::kInvalidPort),
        Rejects("https://example.test:80x/", AddressError::kInvalidPort),
        RejectsUnpinned("https:///path"),
        RejectsUnpinned("https://.localhost/"),
        RejectsUnpinned("https://192.0.2.1/"),
        RejectsUnpinned("https://[::2]/"),
        RejectsUnpinned("https://[::1]evil"),
        RejectsUnpinned("https://[::1]:443evil/"),
        RejectsUnpinned("https://[::1]]/"),
        RejectsUnpinned("https://[::1]@example.test/"),
        RejectsUnpinned("https://[::1]%2fevil/"),
        RejectsUnpinned("https://[::1]%00/"),
        RejectsUnpinned("https://[::1]%0a/"),
        RejectsUnpinned("https://[::1"),
    };
    return corpus;
}

std::string_view TrimmedEdges(std::string_view input) {
    // Edge-trim, exactly like the real ParseAndValidate.
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())) != 0) {
        input.remove_prefix(1);
    }
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())) != 0) {
        input.remove_suffix(1);
    }
    return input;
}

ValidatedAddress FakeParseAndValidate(std::string_view input) {
    input = TrimmedEdges(input);
    for (const ValidatorCase& validator_case : ValidatorCorpus()) {
        if (TrimmedEdges(validator_case.input) == input) {
            return validator_case.valid
                       ? ValidatedAddress{.url = validator_case.canonical_url,
                                          .error = std::nullopt}
                       : ValidatedAddress{.url = "", .error = validator_case.error};
        }
    }
    return {.url = "", .error = AddressError::kInvalidHost};
}

TEST(CommandPaletteTest, GivenANewPaletteWhenInspectedThenItIsHiddenAndEmpty) {
    const CommandPaletteModel palette(NeverCalledValidator());

    EXPECT_FALSE(palette.visible());
    EXPECT_EQ(palette.query(), "");
    EXPECT_EQ(palette.highlighted_index(), 0U);
    EXPECT_FALSE(palette.last_rejection().has_value());
    EXPECT_TRUE(palette.Results().empty());
}

TEST(CommandPaletteTest, GivenAnArrowedRowWhenTheSameQueryIsEchoedThenItStaysHighlighted) {
    // The Return key re-applies the field's text; that must not reset the arrowed row.
    CommandPaletteModel palette(NeverCalledValidator());
    palette.Open();
    palette.SetCommands({{.id = "first", .title = "First"}, {.id = "second", .title = "Second"}});
    palette.MoveHighlight(1);

    palette.SetQuery("");

    EXPECT_EQ(palette.highlighted_index(), 1U);
    const std::optional<PaletteSelection> selection = palette.Submit();
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->kind, PaletteEntryKind::kCommand);
    EXPECT_EQ(selection->command.id, "second");
}

TEST(CommandPaletteTest, GivenAChangedQueryThenTheHighlightResetsToTheFirstRow) {
    CommandPaletteModel palette(NeverCalledValidator());
    palette.Open();
    palette.SetCommands({{.id = "first", .title = "First"}, {.id = "second", .title = "Second"}});
    palette.MoveHighlight(1);

    palette.SetQuery("sec");

    EXPECT_EQ(palette.highlighted_index(), 0U);
}

TEST(CommandPaletteTest, GivenAHiddenPaletteWhenSubmittedThenNoActionIsProduced) {
    CommandPaletteModel palette(NeverCalledValidator());
    palette.SetQuery("https://example.test/");

    EXPECT_FALSE(palette.Submit().has_value());
    EXPECT_FALSE(palette.visible());
}

TEST(CommandPaletteTest, GivenSnapshotsWhenTheQueryIsEmptyThenEverythingIsListedInCanonicalOrder) {
    const CommandPaletteModel palette = OpenedPalette(
        NeverCalledValidator(),
        {Tab(1, "Island Home", "https://island.test/"), Tab(2, "Docs", "https://docs.test/")},
        {Space(9, "Work"), Space(4, "Personal")});

    const std::vector<PaletteEntry> results = palette.Results();

    ASSERT_EQ(results.size(), 4U);
    EXPECT_EQ(results[0].kind, PaletteEntryKind::kTab);
    EXPECT_EQ(results[0].tab.id, TabId{1});
    EXPECT_EQ(results[1].kind, PaletteEntryKind::kTab);
    EXPECT_EQ(results[1].tab.id, TabId{2});
    EXPECT_EQ(results[2].kind, PaletteEntryKind::kSpace);
    EXPECT_EQ(results[2].space.id, SpaceId{9});
    EXPECT_EQ(results[3].kind, PaletteEntryKind::kSpace);
    EXPECT_EQ(results[3].space.id, SpaceId{4});
}

TEST(CommandPaletteTest, GivenAUsedPaletteWhenReopenedThenItResetsToAFreshDraft) {
    CommandPaletteModel palette(FakeParseAndValidate);
    palette.Open();
    palette.SetTabs({Tab(1, "Alpha", "https://alpha.test/")});
    palette.SetQuery("javascript:alert(1)");
    EXPECT_FALSE(palette.Submit().has_value());
    EXPECT_TRUE(palette.last_rejection().has_value());
    palette.MoveHighlight(3);

    palette.Open();

    EXPECT_TRUE(palette.visible());
    EXPECT_EQ(palette.query(), "");
    EXPECT_EQ(palette.highlighted_index(), 0U);
    EXPECT_FALSE(palette.last_rejection().has_value());
}

TEST(CommandPaletteTest, GivenAVisiblePaletteWhenClosedThenItHidesWithoutNavigatingOrSwitching) {
    // An accepted-looking URL row is on screen; Escape must not navigate.
    CommandPaletteModel palette = OpenedPalette(FakeParseAndValidate, {}, {Space(1, "Work")});
    palette.SetQuery("https://example.test/");
    ASSERT_EQ(palette.Results().size(), 1U);

    EXPECT_TRUE(palette.Close());
    EXPECT_FALSE(palette.visible());
    EXPECT_FALSE(palette.Submit().has_value());
    EXPECT_FALSE(palette.Close());

    // Same for a space row: Escape must not switch.
    palette.Open();
    palette.SetQuery("work");
    palette.SetHighlightedIndex(0);
    EXPECT_TRUE(palette.Close());
    EXPECT_FALSE(palette.Submit().has_value());
}

TEST(CommandPaletteTest, GivenRankedTabsWhenTheQueryIsDOcsThenTheMatchClassesOrderTheResults) {
    const std::vector<PaletteEntry> results = ComposePaletteResults(
        "DOCS",
        {Tab(1, "DevOps Checklist", "https://dev.test/checklist"),
         Tab(2, "Team Docs Review", "https://team.test/review"),
         Tab(3, "Docs to Go", "https://togo.test/"), Tab(4, "Docs", "https://docs.test/")},
        {});

    ASSERT_EQ(results.size(), 5U);
    EXPECT_EQ(results[0].kind, PaletteEntryKind::kTab);
    EXPECT_EQ(results[0].tab.id, TabId{4});  // exact
    EXPECT_EQ(results[1].tab.id, TabId{3});  // prefix
    EXPECT_EQ(results[2].tab.id, TabId{2});  // word boundary
    EXPECT_EQ(results[3].tab.id, TabId{1});  // subsequence
    EXPECT_EQ(results[4].kind, PaletteEntryKind::kUrl);
}

TEST(CommandPaletteTest, GivenATabThatOnlyMatchesThroughItsUrlWhenRankedThenItIsStillListed) {
    const std::vector<PaletteEntry> results =
        ComposePaletteResults("docs",
                              {Tab(1, "Island Home", "https://docs.example.test/guide"),
                               Tab(2, "Docs", "https://other.test/")},
                              {});

    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].tab.id, TabId{2});  // exact title match
    EXPECT_EQ(results[1].tab.id, TabId{1});  // URL match ("docs" after "/")
    EXPECT_EQ(results[2].kind, PaletteEntryKind::kUrl);
}

TEST(CommandPaletteTest, GivenTabsOfEqualRankWhenRankedThenInputOrderBreaksTheTie) {
    const std::vector<PaletteEntry> results = ComposePaletteResults(
        "docs", {Tab(1, "Docs Two", "https://two.test/"), Tab(2, "Docs One", "https://one.test/")},
        {});

    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].tab.id, TabId{1});
    EXPECT_EQ(results[1].tab.id, TabId{2});
}

TEST(CommandPaletteTest, GivenATabAndASpaceOfEqualRankWhenRankedThenTheTabComesFirst) {
    const std::vector<PaletteEntry> results =
        ComposePaletteResults("work", {Tab(1, "Work", "https://work.test/")}, {Space(5, "Work")});

    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].kind, PaletteEntryKind::kTab);
    EXPECT_EQ(results[0].tab.id, TabId{1});
    EXPECT_EQ(results[1].kind, PaletteEntryKind::kSpace);
    EXPECT_EQ(results[1].space.id, SpaceId{5});
    EXPECT_EQ(results[2].kind, PaletteEntryKind::kUrl);
}

TEST(CommandPaletteTest, GivenFakeTabBookmarkAndHistoryResultsThenSectionsKeepTheirCanonicalOrder) {
    const std::vector<PaletteEntry> results =
        ComposePaletteResults("docs", {Tab(1, "Docs tab", "https://tab.test/")}, {},
                              {Bookmark("Docs bookmark", "https://bookmark.test/")},
                              {History("Docs recent", "https://history.test/recent", 9.0),
                               History("Docs older", "https://history.test/older", 4.0)});

    ASSERT_EQ(results.size(), 5U);
    EXPECT_EQ(results[0].kind, PaletteEntryKind::kTab);
    EXPECT_EQ(results[1].kind, PaletteEntryKind::kBookmark);
    EXPECT_EQ(results[2].kind, PaletteEntryKind::kHistory);
    EXPECT_EQ(results[2].history.title, "Docs recent");
    EXPECT_EQ(results[3].kind, PaletteEntryKind::kHistory);
    EXPECT_EQ(results[4].kind, PaletteEntryKind::kUrl);
}

TEST(CommandPaletteTest, GivenLibraryBookmarksThenFolderAndFavoriteMetadataStayOnOneBookmarkRow) {
    const PaletteBookmarkEntry bookmark{.id = 42,
                                        .title = "Project notes",
                                        .url = "https://example.test/notes",
                                        .folder_path = "Work",
                                        .scope_label = "Global"};
    const std::vector<PaletteEntry> results = ComposePaletteResults("project", {}, {}, {bookmark});

    ASSERT_EQ(results.size(), 2U);  // one bookmark result plus the URL affordance
    EXPECT_EQ(results[0].kind, PaletteEntryKind::kBookmark);
    EXPECT_EQ(results[0].bookmark.id, 42U);
    EXPECT_EQ(results[0].bookmark.folder_path, "Work");
    EXPECT_EQ(results[0].bookmark.scope_label, "Global");
}

TEST(CommandPaletteTest, GivenABookmarkSelectionThenTheModelReturnsItForTheWindowToValidate) {
    CommandPaletteModel palette(NeverCalledValidator());
    palette.SetBookmarks(
        {{.id = 7, .title = "Saved", .url = "https://saved.test/", .folder_path = "Unfiled"}});
    palette.Open();

    const std::optional<PaletteSelection> selection = palette.Submit();

    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->kind, PaletteEntryKind::kBookmark);
    EXPECT_EQ(selection->bookmark.id, 7U);
}

TEST(CommandPaletteTest, GivenAHistoryRowWhenSubmittedThenItIsReturnedWithoutUrlValidation) {
    CommandPaletteModel palette(NeverCalledValidator());
    palette.Open();
    palette.SetQuery("docs");
    palette.SetHistory({History("Docs", "https://history.test/docs", 3.0)});

    const std::optional<PaletteSelection> selection = palette.Submit();

    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->kind, PaletteEntryKind::kHistory);
    EXPECT_EQ(selection->history.url, "https://history.test/docs");
    EXPECT_FALSE(palette.visible());
}

TEST(CommandPaletteTest, GivenSpacesWhenRankedThenTheyAreMatchedByNameByTheSameClasses) {
    const std::vector<PaletteEntry> results = ComposePaletteResults(
        "work", {},
        {Space(1, "Network"), Space(2, "Work"), Space(3, "Homework"), Space(4, "Workbench")});

    ASSERT_EQ(results.size(), 5U);
    EXPECT_EQ(results[0].space.id, SpaceId{2});  // exact
    EXPECT_EQ(results[1].space.id, SpaceId{4});  // prefix
    EXPECT_EQ(results[2].space.id, SpaceId{1});  // subsequence, input order
    EXPECT_EQ(results[3].space.id, SpaceId{3});  // subsequence, input order
    EXPECT_EQ(results[4].kind, PaletteEntryKind::kUrl);
}

TEST(CommandPaletteTest, GivenANonEmptyQueryWhenComposedThenOneRawUrlRowIsAppendedLast) {
    const std::vector<PaletteEntry> results =
        ComposePaletteResults("  docs  ", {Tab(1, "Docs", "https://docs.test/")}, {});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].kind, PaletteEntryKind::kTab);
    EXPECT_EQ(results[1].kind, PaletteEntryKind::kUrl);
    // The affordance carries the query exactly as typed; the validator owns
    // edge-whitespace handling.
    EXPECT_EQ(results[1].url_query, "  docs  ");
    EXPECT_EQ(results[1].tab, PaletteTabEntry{});
    EXPECT_EQ(results[1].space, PaletteSpaceEntry{});
}

TEST(CommandPaletteTest, GivenAWhitespaceOnlyQueryWhenComposedThenItBehavesLikeAnEmptyQuery) {
    const std::vector<PaletteEntry> results = ComposePaletteResults(
        " \t\r\n", {Tab(1, "Island Home", "https://island.test/")}, {Space(2, "Work")});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].kind, PaletteEntryKind::kTab);
    EXPECT_EQ(results[1].kind, PaletteEntryKind::kSpace);
}

TEST(CommandPaletteTest, GivenAQueryNothingMatchesWhenComposedThenOnlyTheUrlRowIsListed) {
    CommandPaletteModel palette =
        OpenedPalette(NeverCalledValidator(), {Tab(1, "Island Home", "https://island.test/")}, {});

    palette.SetQuery("https://nothing.matches.here");

    const std::vector<PaletteEntry> results = palette.Results();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].kind, PaletteEntryKind::kUrl);
    EXPECT_EQ(palette.highlighted_index(), 0U);
}

TEST(CommandPaletteTest,
     GivenAValidUrlQueryWhenSubmittedThenItReturnsTheValidatedAddressAndCloses) {
    CommandPaletteModel palette =
        OpenedPalette(FakeParseAndValidate, {Tab(1, "Island Home", "https://island.test/")}, {});
    palette.SetQuery("https://example.test/");

    const std::optional<PaletteSelection> selection = palette.Submit();

    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->kind, PaletteEntryKind::kUrl);
    EXPECT_TRUE(selection->address.is_valid());
    EXPECT_EQ(selection->address.url, "https://example.test/");
    EXPECT_FALSE(palette.visible());
    EXPECT_FALSE(palette.last_rejection().has_value());
}

TEST(CommandPaletteTest,
     GivenTheAddressParserCorpusWhenSubmittedThroughThePaletteThenAcceptRejectParityHolds) {
    for (const ValidatorCase& validator_case : ValidatorCorpus()) {
        CommandPaletteModel palette(FakeParseAndValidate);
        palette.Open();
        palette.SetQuery(validator_case.input);

        const std::optional<PaletteSelection> selection = palette.Submit();

        if (validator_case.valid) {
            ASSERT_TRUE(selection.has_value()) << validator_case.input;
            EXPECT_EQ(selection->kind, PaletteEntryKind::kUrl) << validator_case.input;
            EXPECT_TRUE(selection->address.is_valid()) << validator_case.input;
            EXPECT_EQ(selection->address.url, validator_case.canonical_url) << validator_case.input;
            EXPECT_FALSE(palette.visible()) << validator_case.input;
            EXPECT_FALSE(palette.last_rejection().has_value()) << validator_case.input;
        } else {
            EXPECT_FALSE(selection.has_value()) << validator_case.input;
            EXPECT_TRUE(palette.visible()) << validator_case.input;
            EXPECT_TRUE(palette.last_rejection().has_value()) << validator_case.input;
            if (validator_case.error.has_value()) {
                EXPECT_EQ(palette.last_rejection(), validator_case.error) << validator_case.input;
            }
        }
    }
}

TEST(CommandPaletteTest,
     GivenAnInvalidUrlWhenSubmittedThenThePaletteStaysOpenAndTheDraftStaysEditable) {
    CommandPaletteModel palette(FakeParseAndValidate);
    palette.Open();
    palette.SetQuery("javascript:alert(1)");

    EXPECT_FALSE(palette.Submit().has_value());
    EXPECT_TRUE(palette.visible());
    EXPECT_EQ(palette.last_rejection(),
              std::optional<AddressError>(AddressError::kUnsupportedScheme));

    // The rejection consumed nothing: correcting the draft submits normally.
    palette.SetQuery("https://example.test/");
    const std::optional<PaletteSelection> selection = palette.Submit();
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->address.url, "https://example.test/");
    EXPECT_FALSE(palette.visible());
}

TEST(CommandPaletteTest, GivenANewDraftWhenSetThenAnyStaleRejectionIsCleared) {
    CommandPaletteModel palette(FakeParseAndValidate);
    palette.Open();
    palette.SetQuery("/relative/path");
    EXPECT_FALSE(palette.Submit().has_value());
    EXPECT_TRUE(palette.last_rejection().has_value());

    palette.SetQuery("anything at all");
    EXPECT_FALSE(palette.last_rejection().has_value());
}

TEST(CommandPaletteTest, GivenAValidSubmissionWhenSubmittedTwiceThenOnlyTheFirstProducesAnAction) {
    CommandPaletteModel palette(FakeParseAndValidate);
    palette.Open();
    palette.SetQuery("https://example.test/");

    const std::optional<PaletteSelection> first = palette.Submit();
    ASSERT_TRUE(first.has_value());

    EXPECT_FALSE(palette.Submit().has_value());
    EXPECT_FALSE(palette.visible());
}

TEST(CommandPaletteTest, GivenARejectedSubmissionWhenClosedThenCancelYieldsNoAction) {
    CommandPaletteModel palette(FakeParseAndValidate);
    palette.Open();
    palette.SetQuery("/relative/path");
    EXPECT_FALSE(palette.Submit().has_value());

    EXPECT_TRUE(palette.Close());
    EXPECT_FALSE(palette.last_rejection().has_value());
    EXPECT_FALSE(palette.Submit().has_value());
}

TEST(CommandPaletteTest, GivenACancelledSessionWhenReopenedThenSubmissionWorksAgain) {
    CommandPaletteModel palette(FakeParseAndValidate);
    palette.Open();
    palette.SetQuery("https://example.test/");
    EXPECT_TRUE(palette.Close());

    palette.Open();
    palette.SetQuery("https://example.test/");

    const std::optional<PaletteSelection> selection = palette.Submit();
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->address.url, "https://example.test/");
}

TEST(CommandPaletteTest, GivenATabRowWhenSubmittedThenTheTabIsReturnedOnceAndNothingIsValidated) {
    CommandPaletteModel palette = OpenedPalette(
        NeverCalledValidator(),
        {Tab(7, "Alpha", "https://alpha.test/"), Tab(3, "Beta", "https://beta.test/")},
        {Space(5, "Work")});
    palette.SetQuery("beta");

    const std::optional<PaletteSelection> selection = palette.Submit();

    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->kind, PaletteEntryKind::kTab);
    EXPECT_EQ(selection->tab, Tab(3, "Beta", "https://beta.test/"));
    EXPECT_FALSE(palette.visible());
    EXPECT_FALSE(palette.Submit().has_value());
}

TEST(CommandPaletteTest,
     GivenASpaceRowWhenSubmittedThenTheSpaceIsReturnedOnceAndNothingIsValidated) {
    CommandPaletteModel palette = OpenedPalette(
        NeverCalledValidator(), {Tab(1, "Alpha", "https://alpha.test/")}, {Space(5, "Work")});
    palette.SetQuery("work");
    palette.SetHighlightedIndex(0);

    const std::optional<PaletteSelection> selection = palette.Submit();

    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->kind, PaletteEntryKind::kSpace);
    EXPECT_EQ(selection->space, Space(5, "Work"));
    EXPECT_FALSE(palette.visible());
    EXPECT_FALSE(palette.Submit().has_value());
}

TEST(CommandPaletteTest, GivenTheHighlightWhenMovedThenItWrapsWithinTheCurrentResults) {
    CommandPaletteModel palette = OpenedPalette(
        NeverCalledValidator(),
        {Tab(1, "Docs Alpha", "https://alpha.test/"), Tab(2, "Docs Beta", "https://beta.test/")},
        {});
    palette.SetQuery("docs");
    ASSERT_EQ(palette.Results().size(), 3U);  // two tabs plus the URL row

    palette.MoveHighlight(-1);
    EXPECT_EQ(palette.highlighted_index(), 2U);
    palette.MoveHighlight(1);
    EXPECT_EQ(palette.highlighted_index(), 0U);
    palette.MoveHighlight(4);
    EXPECT_EQ(palette.highlighted_index(), 1U);
}

TEST(CommandPaletteTest, GivenNoResultsWhenTheHighlightIsMovedThenNothingChanges) {
    CommandPaletteModel palette = OpenedPalette(NeverCalledValidator(), {}, {});
    palette.MoveHighlight(1);
    palette.MoveHighlight(-1);

    EXPECT_EQ(palette.highlighted_index(), 0U);
}

TEST(CommandPaletteTest, GivenAnOutOfRangeRowWhenHighlightedThenTheHighlightIsUnchanged) {
    CommandPaletteModel palette =
        OpenedPalette(NeverCalledValidator(), {Tab(1, "Docs", "https://docs.test/")}, {});
    palette.SetQuery("docs");
    ASSERT_EQ(palette.Results().size(), 2U);

    palette.SetHighlightedIndex(2);
    EXPECT_EQ(palette.highlighted_index(), 0U);
    palette.SetHighlightedIndex(1);
    EXPECT_EQ(palette.highlighted_index(), 1U);
}

TEST(CommandPaletteTest, GivenAQueryOrSnapshotChangeWhenItArrivesThenTheHighlightResets) {
    CommandPaletteModel palette = OpenedPalette(
        NeverCalledValidator(),
        {Tab(1, "Docs Alpha", "https://alpha.test/"), Tab(2, "Docs Beta", "https://beta.test/")},
        {Space(3, "Docs Space")});
    palette.SetQuery("docs");
    palette.SetHighlightedIndex(2);

    palette.SetQuery("beta");
    EXPECT_EQ(palette.highlighted_index(), 0U);

    palette.SetHighlightedIndex(1);
    palette.SetTabs({Tab(4, "Beta", "https://beta.test/")});
    EXPECT_EQ(palette.highlighted_index(), 0U);

    palette.SetHighlightedIndex(1);
    palette.SetSpaces({Space(5, "Beta")});
    EXPECT_EQ(palette.highlighted_index(), 0U);
}

TEST(CommandPaletteTest, GivenANewSnapshotWhenItArrivesThenResultsFollowTheNewInputOrder) {
    CommandPaletteModel palette =
        OpenedPalette(NeverCalledValidator(), {Tab(1, "Docs", "https://one.test/")}, {});
    palette.SetQuery("docs");

    palette.SetTabs({Tab(2, "Docs", "https://two.test/"), Tab(1, "Docs", "https://one.test/")});

    const std::vector<PaletteEntry> results = palette.Results();
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].tab.id, TabId{2});
    EXPECT_EQ(results[1].tab.id, TabId{1});
    EXPECT_EQ(results[2].kind, PaletteEntryKind::kUrl);
}

TEST(CommandPaletteTest, GivenNoValidatorWhenAUrlRowIsSubmittedThenNoActionIsProduced) {
    CommandPaletteModel palette(nullptr);
    palette.Open();
    palette.SetQuery("https://example.test/");

    EXPECT_FALSE(palette.Submit().has_value());
    EXPECT_TRUE(palette.visible());
    EXPECT_FALSE(palette.last_rejection().has_value());
}

TEST(CommandPaletteTest, GivenCandidateAndQueryPairsWhenClassifiedThenTheRankOrderHolds) {
    // Exact, with ASCII case folding.
    EXPECT_EQ(ClassifyPaletteMatch("Docs", "docs"), PaletteMatchRank::kExact);
    EXPECT_EQ(ClassifyPaletteMatch("HTTPS://EXAMPLE.test/", "https://example.test/"),
              PaletteMatchRank::kExact);
    // Prefix.
    EXPECT_EQ(ClassifyPaletteMatch("Docs to Go", "docs"), PaletteMatchRank::kPrefix);
    EXPECT_EQ(ClassifyPaletteMatch("Workbench", "work"), PaletteMatchRank::kPrefix);
    // Word boundary: any non-alphanumeric character before the match counts.
    EXPECT_EQ(ClassifyPaletteMatch("Team Docs Review", "docs"), PaletteMatchRank::kWordBoundary);
    EXPECT_EQ(ClassifyPaletteMatch("example.test/", "test"), PaletteMatchRank::kWordBoundary);
    // A match preceded by an alphanumeric character is not a word boundary.
    EXPECT_EQ(ClassifyPaletteMatch("Network", "work"), PaletteMatchRank::kSubsequence);
    EXPECT_EQ(ClassifyPaletteMatch("exampletest", "test"), PaletteMatchRank::kSubsequence);
    // Subsequence.
    EXPECT_EQ(ClassifyPaletteMatch("DevOps Checklist", "docs"), PaletteMatchRank::kSubsequence);
    // No match.
    EXPECT_EQ(ClassifyPaletteMatch("Island", "zebra"), PaletteMatchRank::kNone);
    // The query is edge-trimmed; candidates are not.
    EXPECT_EQ(ClassifyPaletteMatch("Docs", " doc "), PaletteMatchRank::kPrefix);
    EXPECT_EQ(ClassifyPaletteMatch(" docs ", "docs"), PaletteMatchRank::kWordBoundary);
    // An empty or whitespace-only query matches nothing through the ranker;
    // callers list everything in canonical order instead.
    EXPECT_EQ(ClassifyPaletteMatch("Anything", ""), PaletteMatchRank::kNone);
    EXPECT_EQ(ClassifyPaletteMatch("Anything", "   "), PaletteMatchRank::kNone);
}

TEST(CommandPaletteTest,
     GivenReportsWhenComposedThenTheyListAfterBookmarksAndRankByTitleOrQuestion) {
    const std::vector<PaletteReportEntry> reports = {
        {.id = "r1", .title = "Rust in short", .question = "what is rust"},
        {.id = "r2", .title = "Sky colour", .question = "why is the sky blue"},
    };
    const std::vector<PaletteBookmarkEntry> bookmarks = {
        {.title = "Rust book", .url = "https://r.test/"}};
    // Empty query: canonical order, reports after bookmarks, no URL row.
    const std::vector<PaletteEntry> all =
        ComposePaletteResults("", {}, {}, bookmarks, {}, {}, reports);
    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[0].kind, PaletteEntryKind::kBookmark);
    EXPECT_EQ(all[1].kind, PaletteEntryKind::kReport);
    EXPECT_EQ(all[1].report.id, "r1");

    // The question matches too; the URL row stays last.
    const std::vector<PaletteEntry> sky =
        ComposePaletteResults("blue", {}, {}, {}, {}, {}, reports);
    ASSERT_EQ(sky.size(), 2U);
    EXPECT_EQ(sky[0].kind, PaletteEntryKind::kReport);
    EXPECT_EQ(sky[0].report.id, "r2");
    EXPECT_EQ(sky[1].kind, PaletteEntryKind::kUrl);
}

TEST(CommandPaletteTest, GivenAReportRowWhenSubmittedThenTheSelectionCarriesItsId) {
    CommandPaletteModel palette(NeverCalledValidator());
    palette.Open();
    palette.SetReports({{.id = "r42", .title = "Findings", .question = "q"}});
    const std::optional<PaletteSelection> selection = palette.Submit();
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->kind, PaletteEntryKind::kReport);
    EXPECT_EQ(selection->report.id, "r42");
    EXPECT_FALSE(palette.visible());
}

TEST(CommandPaletteCommandsTest, GivenCommandsThenTheyFollowSpacesAndMatchByTitle) {
    const std::vector<PaletteTabEntry> tabs{
        {.id = TabId{1}, .title = "Sleepy docs", .url = "https://a.example/"}};
    const std::vector<PaletteSpaceEntry> spaces{{.id = SpaceId{1}, .name = "Work"}};
    const std::vector<PaletteCommandEntry> commands{
        {.id = "sleep_inactive_tabs", .title = "Sleep inactive tabs now"}};

    const std::vector<PaletteEntry> all = ComposePaletteResults("", tabs, spaces, {}, {}, commands);
    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[0].kind, PaletteEntryKind::kTab);
    EXPECT_EQ(all[1].kind, PaletteEntryKind::kSpace);
    EXPECT_EQ(all[2].kind, PaletteEntryKind::kCommand);

    const std::vector<PaletteEntry> found =
        ComposePaletteResults("inactive", tabs, spaces, {}, {}, commands);
    ASSERT_EQ(found.size(), 2U);  // the command, then the go-to row
    EXPECT_EQ(found[0].kind, PaletteEntryKind::kCommand);
    EXPECT_EQ(found[0].command.id, "sleep_inactive_tabs");
}

TEST(CommandPaletteCommandsTest, GivenBookmarkCommandsThenTheyHeadTheBookmarksGroupBeforeItsRows) {
    const std::vector<PaletteCommandEntry> commands{
        {.id = "sleep_inactive_tabs", .title = "Sleep inactive tabs now"},
        {.id = "add_bookmark", .title = "Add bookmark", .bookmark_group = true}};
    const std::vector<PaletteBookmarkEntry> bookmarks = {Bookmark("Docs", "https://docs.test/")};

    const std::vector<PaletteEntry> all =
        ComposePaletteResults("", {}, {}, bookmarks, {}, commands);

    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[0].kind, PaletteEntryKind::kCommand);
    EXPECT_EQ(all[0].command.id, "sleep_inactive_tabs");
    EXPECT_EQ(all[1].kind, PaletteEntryKind::kCommand);
    EXPECT_EQ(all[1].command.id, "add_bookmark");
    EXPECT_EQ(all[2].kind, PaletteEntryKind::kBookmark);
    EXPECT_EQ(PaletteGroupHeader(all[0]), "Commands");
    EXPECT_EQ(PaletteGroupHeader(all[1]), "Bookmarks");
    EXPECT_EQ(PaletteGroupHeader(all[2]), "Bookmarks");
    EXPECT_EQ(PaletteKindLabel(all[1], /*sentence_case=*/true), "Bookmark");

    // A query that matches only the bookmark command still lists it in its group.
    const std::vector<PaletteEntry> found =
        ComposePaletteResults("add book", {}, {}, bookmarks, {}, commands);
    ASSERT_GE(found.size(), 1U);
    EXPECT_EQ(found[0].command.id, "add_bookmark");
    EXPECT_EQ(PaletteGroupHeader(found[0]), "Bookmarks");
}

TEST(CommandPaletteCommandsTest, GivenAHighlightedCommandWhenSubmittedThenItsIdIsReturnedOnce) {
    CommandPaletteModel model([](std::string_view) { return ValidatedAddress{}; });
    model.SetCommands({{.id = "sleep_inactive_tabs", .title = "Sleep inactive tabs now"}});
    model.Open();

    const std::optional<PaletteSelection> selection = model.Submit();

    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(selection->kind, PaletteEntryKind::kCommand);
    EXPECT_EQ(selection->command.id, "sleep_inactive_tabs");
    EXPECT_FALSE(model.Submit().has_value());
}

TEST(CommandPaletteSpaceIconTest, GivenThePickerRowsThenNoneComesFirstAndEveryIconFollows) {
    const std::vector<PaletteCommandEntry> rows = SpaceIconPaletteCommands();
    ASSERT_EQ(rows.size(), kSpaceIconChoices.size() + 1U);
    EXPECT_EQ(rows[0].title, "None");
    EXPECT_EQ(rows[1].title, "Home");
    for (const PaletteCommandEntry& row : rows) {
        EXPECT_TRUE(ParseSpaceIconCommandId(row.id).has_value()) << row.id;
    }
    EXPECT_EQ(ParseSpaceIconCommandId(rows[0].id), std::string());
    EXPECT_EQ(ParseSpaceIconCommandId("space_icon:rocket"), std::string("rocket"));
}

TEST(CommandPaletteSpaceIconTest, GivenOtherOrUnknownIdsThenTheyAreNotPickerRows) {
    EXPECT_FALSE(ParseSpaceIconCommandId("sleep_inactive_tabs").has_value());
    EXPECT_FALSE(ParseSpaceIconCommandId(kChangeSpaceIconCommandId).has_value());
    EXPECT_FALSE(ParseSpaceIconCommandId("space_icon:../etc").has_value());
    EXPECT_FALSE(ParseSpaceIconCommandId("space_icon:").has_value());
}

TEST(CommandPaletteSpaceIconTest, GivenTheChangeCommandWhenQueriedThenItMatchesByTitle) {
    const std::vector<PaletteCommandEntry> commands{
        {.id = std::string(kChangeSpaceIconCommandId), .title = "Change space icon\xE2\x80\xA6"}};
    const std::vector<PaletteEntry> found =
        ComposePaletteResults("space icon", {}, {}, {}, {}, commands);
    ASSERT_GE(found.size(), 1U);
    EXPECT_EQ(found[0].command.id, kChangeSpaceIconCommandId);
}

TEST(CommandPaletteNativeStyleTest, GivenEveryResultKindThenItAppearsOnceInTheStyleTable) {
    const std::array<PaletteEntryKind, 7> kinds = {
        PaletteEntryKind::kTab,      PaletteEntryKind::kSpace,   PaletteEntryKind::kUrl,
        PaletteEntryKind::kBookmark, PaletteEntryKind::kHistory, PaletteEntryKind::kCommand,
        PaletteEntryKind::kReport,
    };
    ASSERT_EQ(PaletteKindStyles().size(), kinds.size());
    for (const PaletteEntryKind kind : kinds) {
        int matches = 0;
        for (const PaletteKindStyle& style : PaletteKindStyles()) {
            matches += style.kind == kind ? 1 : 0;
        }
        EXPECT_EQ(matches, 1);
    }
}

TEST(CommandPaletteNativeStyleTest, GivenNativeLabelsThenNoneIsAllCapsAndEveryHeaderIsSet) {
    for (const PaletteKindStyle& style : PaletteKindStyles()) {
        EXPECT_FALSE(style.group_header.empty());
        const bool has_lowercase =
            std::any_of(style.sentence_label.begin(), style.sentence_label.end(),
                        [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; });
        EXPECT_TRUE(has_lowercase) << style.sentence_label;
    }
    // The URL row and the command rows must not both print "Commands" as a group title.
    EXPECT_NE(PaletteKindStyleFor(PaletteEntryKind::kUrl).group_header,
              PaletteKindStyleFor(PaletteEntryKind::kCommand).group_header);
}

TEST(CommandPaletteNativeStyleTest, GivenKindLabelsThenNativeIsSentenceCaseAndLegacyStaysUpper) {
    PaletteEntry tab;
    tab.kind = PaletteEntryKind::kTab;
    EXPECT_EQ(PaletteKindLabel(tab, /*sentence_case=*/true), "Tab");
    EXPECT_EQ(PaletteKindLabel(tab, /*sentence_case=*/false), "TAB");

    PaletteEntry url;
    url.kind = PaletteEntryKind::kUrl;
    EXPECT_EQ(PaletteKindLabel(url, /*sentence_case=*/true), "Go to");
    EXPECT_EQ(PaletteKindLabel(url, /*sentence_case=*/false), "GO TO");

    PaletteEntry table;
    table.kind = PaletteEntryKind::kReport;
    table.report.kind = "collection";
    EXPECT_EQ(PaletteKindLabel(table, /*sentence_case=*/true), "Table");
    EXPECT_EQ(PaletteKindLabel(table, /*sentence_case=*/false), "TABLE");
}

}  // namespace
}  // namespace island
