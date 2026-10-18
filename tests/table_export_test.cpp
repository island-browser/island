#include "table_export.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace island {
namespace {

TEST(TableExportTest, CsvQuotesPipesNewlinesAndKeepsUtf8) {
    std::optional<Table> t = FirstMarkdownTable(
        "intro\n\n| Source | Цена | Note |\n|---|---|---|\n"
        "| [Shop](https://a.test) | 1 299 ₽ | says \"new\", a\\|b |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(TableToCsv(*t),
              "\xEF\xBB\xBFSource,Source title,Цена,Note\r\n"
              "https://a.test,Shop,1 299 ₽,\"says \"\"new\"\", a|b\"\r\n");
}

TEST(TableExportTest, LineBreakTagsBecomeQuotedCsvNewlines) {
    std::optional<Table> t = FirstMarkdownTable("| a | b |\n|---|---|\n| x<br>y | z |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(TableToCsv(*t),
              "\xEF\xBB\xBF"
              "a,b\r\n\"x\ny\",z\r\n");
}

TEST(TableExportTest, JsonIsAnArrayOfObjects) {
    std::optional<Table> t = FirstMarkdownTable("| a | b |\n|---|---|\n| 1 | 2 |\n| 3 |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(TableToJson(*t), R"([{"a":"1","b":"2"},{"a":"3","b":""}])");
}

TEST(TableExportTest, JsonEscapesControlsAndKeepsUtf8) {
    std::optional<Table> t = FirstMarkdownTable("| k |\n|---|\n| \"q\"<br>₽ |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(TableToJson(*t), "[{\"k\":\"\\\"q\\\"\\n₽\"}]");
}

TEST(TableExportTest, CsvNeutralizesFormulaCells) {
    // Page text is untrusted: a cell that a spreadsheet would run as a formula gets a quote.
    Table t;
    t.header = {"=Name", "Price"};
    t.rows = {{"=HYPERLINK(\"https://evil.test\",\"x\")", "189"},
              {"+1+1", "-5"},
              {"@SUM(A1)", "\tcmd"},
              {"\rcmd", "plain"},
              {"https://a.test/?q=1", ""}};
    EXPECT_EQ(TableToCsv(t),
              "\xEF\xBB\xBF'=Name,Price\r\n"
              "\"'=HYPERLINK(\"\"https://evil.test\"\",\"\"x\"\")\",189\r\n"
              "'+1+1,'-5\r\n"
              "'@SUM(A1),'\tcmd\r\n"
              "\"'\rcmd\",plain\r\n"
              "https://a.test/?q=1,\r\n");
    // Only a leading character counts: a mid-cell sign stays as it is.
    t.header = {"a"};
    t.rows = {{"x=1"}};
    EXPECT_EQ(TableToCsv(t),
              "\xEF\xBB\xBF"
              "a\r\nx=1\r\n");
    // JSON keeps the text as it is.
    EXPECT_EQ(TableToJson(t), R"([{"a":"x=1"}])");
    t.rows = {{"=1+1"}};
    EXPECT_EQ(TableToJson(t), R"([{"a":"=1+1"}])");
}

TEST(TableExportTest, JsonRepeatedHeadersGetNumberedKeys) {
    // "Source" is a link column with a title column, and the header already has "Source title".
    std::optional<Table> t = FirstMarkdownTable(
        "| Source | Source title | Source |\n|---|---|---|\n"
        "| [Shop](https://a.test) | Real | x |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(TableToJson(*t), R"([{"Source":"https://a.test","Source title":"Shop",)"
                               R"("Source title 2":"Real","Source 2":"x"}])");
}

TEST(TableExportTest, JsonShortHandBuiltRowReadsAsEmptyCells) {
    Table t;
    t.header = {"a", "b"};
    t.rows = {{"1"}};
    EXPECT_EQ(TableToJson(t), R"([{"a":"1","b":""}])");
    EXPECT_EQ(TableToCsv(t),
              "\xEF\xBB\xBF"
              "a,b\r\n1,\r\n");
}

TEST(TableExportTest, TablesInsideFencedCodeAreIgnored) {
    std::optional<Table> t = FirstMarkdownTable(
        "An example:\n\n```\n| a | b |\n|---|---|\n| 1 | 2 |\n```\n\n"
        "| c |\n|---|\n| 3 |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->header, (std::vector<std::string>{"c"}));
    EXPECT_EQ(t->rows.size(), 1U);
    EXPECT_FALSE(FirstMarkdownTable("```\n| a |\n|---|\n| 1 |\n```\n").has_value());
}

TEST(TableExportTest, TwoLinksInOneCellAreNotOneLink) {
    std::optional<Table> t =
        FirstMarkdownTable("| Link |\n|---|\n| [a](https://a.test)[c](https://c.test) |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->rows.front().front(), "[a](https://a.test)[c](https://c.test)");
}

TEST(TableExportTest, NoTableIsNullopt) {
    EXPECT_FALSE(FirstMarkdownTable("just text | with a pipe").has_value());
}

TEST(TableExportTest, LinkColumnWithSameTextAndUrlHasNoTitleColumn) {
    std::optional<Table> t = FirstMarkdownTable(
        "| Site | Note |\n|---|---|\n| [https://a.test](https://a.test) | ok |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->header, (std::vector<std::string>{"Site", "Note"}));
    EXPECT_EQ(t->rows.front().front(), "https://a.test");
}

}  // namespace
}  // namespace island
