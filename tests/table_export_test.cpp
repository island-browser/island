#include "table_export.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

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
