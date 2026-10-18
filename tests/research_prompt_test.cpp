#include "research_prompt.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "plan_graph.h"

namespace island {
namespace {

using agent::TranscriptItem;
using Kind = TranscriptItem::Kind;

TranscriptItem Item(Kind kind, std::string text = {}) {
    TranscriptItem item;
    item.kind = kind;
    item.text = std::move(text);
    return item;
}

TEST(ResearchSpaceNameTest, CollapsesWhitespaceAndFallsBackWhenEmpty) {
    EXPECT_EQ(ResearchSpaceName("  best   laptop\n for\tcoding "), "best laptop for coding");
    EXPECT_EQ(ResearchSpaceName(""), "Research");
    EXPECT_EQ(ResearchSpaceName(" \n\t "), "Research");
}

TEST(ResearchSpaceNameTest, ClipsLongQuestionsWithAnEllipsis) {
    const std::string name = ResearchSpaceName(std::string(200, 'a'));
    EXPECT_EQ(name, std::string(40, 'a') + "...");
}

TEST(ResearchSpaceNameTest, NeverSplitsAMultiByteCharacter) {
    // Each Cyrillic letter is two bytes; 40 bytes is exactly 20 letters, and an
    // odd limit would land inside a letter without the boundary check.
    std::string question;
    for (int i = 0; i < 30; ++i) question += "\xD0\xB6";  // U+0436
    const std::string name = ResearchSpaceName(question);
    ASSERT_GE(name.size(), 3U);
    ASSERT_EQ(name.substr(name.size() - 3), "...");
    const std::string body = name.substr(0, name.size() - 3);
    EXPECT_EQ(body.size() % 2, 0U);
    for (std::size_t i = 0; i < body.size(); i += 2) {
        EXPECT_EQ(body.substr(i, 2), "\xD0\xB6");
    }
    // A three-byte letter forces a clip below the byte limit.
    std::string wide;
    for (int i = 0; i < 20; ++i) wide += "\xE2\x82\xAC";  // euro sign
    const std::string wide_name = ResearchSpaceName(wide);
    const std::string wide_body = wide_name.substr(0, wide_name.size() - 3);
    EXPECT_EQ(wide_body.size() % 3, 0U);
    EXPECT_LE(wide_body.size(), 40U);
}

TEST(BuildResearchPromptTest, CarriesTheQuestionTheSpaceAndTheReportContract) {
    const std::string prompt = BuildResearchPrompt("Why is the sky blue?");
    EXPECT_NE(prompt.find("Why is the sky blue?"), std::string::npos);
    // Island opens the space itself: the agent is told it exists, not asked to create it.
    EXPECT_NE(prompt.find("already opened a space named \"Why is the sky blue?\""),
              std::string::npos);
    EXPECT_EQ(prompt.find("browser_new_space"), std::string::npos);
    EXPECT_EQ(prompt.find("Create a new space"), std::string::npos);
    EXPECT_NE(prompt.find("browser_open_tab"), std::string::npos);
    EXPECT_NE(prompt.find("page_read"), std::string::npos);
    EXPECT_NE(prompt.find("Compare the sources"), std::string::npos);
    EXPECT_NE(prompt.find("Cite every claim"), std::string::npos);
    EXPECT_NE(prompt.find(std::string(kReportBeginMarker)), std::string::npos);
    EXPECT_NE(prompt.find(std::string(kReportEndMarker)), std::string::npos);
    EXPECT_LT(prompt.find(std::string(kReportBeginMarker)),
              prompt.find(std::string(kReportEndMarker)));
}

TEST(BuildResearchPromptTest, AsksForABranchingPlanWithIslandMetadata) {
    const std::string prompt = BuildResearchPrompt("q");
    EXPECT_NE(prompt.find("_meta.island"), std::string::npos);
    for (const char* key : {"\"id\"", "\"after\"", "\"branch\"", "\"kind\""}) {
        EXPECT_NE(prompt.find(key), std::string::npos) << key;
    }
    EXPECT_NE(
        prompt.find("at most " + std::to_string(agent::kMaxBranchDepth) + " levels of branching"),
        std::string::npos);
    EXPECT_NE(prompt.find("wait for approval before running"), std::string::npos);
    // The plan comes before any browser work, so approval gates the steps.
    EXPECT_LT(prompt.find("wait for approval before running"), prompt.find("How to work:"));
}

TEST(BuildResearchPromptTest, SaysWhatToDoWithAPageThatIsNotShown) {
    const std::string prompt = BuildResearchPrompt("why");
    EXPECT_NE(prompt.find("has no live page"), std::string::npos);
    EXPECT_NE(prompt.find("do not activate that page"), std::string::npos);
}

TEST(BuildResearchPromptTest, ClipsAnOversizedQuestion) {
    const std::string prompt = BuildResearchPrompt(std::string(kMaxResearchQuestionBytes * 3, 'q'));
    EXPECT_NE(prompt.find(std::string(kMaxResearchQuestionBytes, 'q')), std::string::npos);
    EXPECT_EQ(prompt.find(std::string(kMaxResearchQuestionBytes + 1, 'q')), std::string::npos);
}

TEST(ExtractResearchReportTest, TakesTheTextBetweenTheMarkers) {
    const std::vector<TranscriptItem> items = {
        Item(Kind::kUser, "prompt"),
        Item(Kind::kAgent, "I will start by searching."),
        Item(Kind::kTool, "page_read"),
        Item(Kind::kAgent, "Here you go:\n<<<ISLAND_REPORT_BEGIN>>>\n# Title\n\nBody [a](https://a.test)\n"
                           "<<<ISLAND_REPORT_END>>>\n"),
    };
    EXPECT_EQ(ExtractResearchReport(items), "# Title\n\nBody [a](https://a.test)");
}

TEST(ExtractResearchReportTest, ToleratesAMissingEndMarkerAndAnOuterFence) {
    std::vector<TranscriptItem> items = {
        Item(Kind::kUser, "p"),
        Item(Kind::kAgent, "<<<ISLAND_REPORT_BEGIN>>>\n# T\ntext"),
    };
    EXPECT_EQ(ExtractResearchReport(items), "# T\ntext");
    items[1].text = "<<<ISLAND_REPORT_BEGIN>>>\n```markdown\n# T\ntext\n```\n<<<ISLAND_REPORT_END>>>";
    EXPECT_EQ(ExtractResearchReport(items), "# T\ntext");
    // A report that itself contains a code block keeps its fences.
    items[1].text = "<<<ISLAND_REPORT_BEGIN>>>\n# T\n```\ncode\n```\n<<<ISLAND_REPORT_END>>>";
    EXPECT_EQ(ExtractResearchReport(items), "# T\n```\ncode\n```");
}

TEST(ExtractResearchReportTest, PrefersTheMarkedMessageOverALaterChatter) {
    const std::vector<TranscriptItem> items = {
        Item(Kind::kUser, "p"),
        Item(Kind::kAgent, "<<<ISLAND_REPORT_BEGIN>>>\n# R\nx\n<<<ISLAND_REPORT_END>>>"),
        Item(Kind::kTool, "t"),
        Item(Kind::kAgent, "Done."),
    };
    EXPECT_EQ(ExtractResearchReport(items), "# R\nx");
}

TEST(ExtractResearchReportTest, FallsBackToTheLastMessageAndIgnoresEarlierTurns) {
    std::vector<TranscriptItem> items = {
        Item(Kind::kUser, "old"),
        Item(Kind::kAgent, "<<<ISLAND_REPORT_BEGIN>>>\n# Old\n<<<ISLAND_REPORT_END>>>"),
        Item(Kind::kUser, "new"),
        Item(Kind::kAgent, "  The unmarked answer.  "),
    };
    EXPECT_EQ(ExtractResearchReport(items), "The unmarked answer.");
    items.push_back(Item(Kind::kNotice, "Stopped."));
    EXPECT_EQ(ExtractResearchReport(items), "The unmarked answer.");
}

TEST(ExtractResearchReportTest, NothingWhenTheTurnProducedNoAgentText) {
    EXPECT_EQ(ExtractResearchReport({}), std::nullopt);
    EXPECT_EQ(ExtractResearchReport({Item(Kind::kUser, "p"), Item(Kind::kTool, "t"),
                                     Item(Kind::kAgent, "  \n")}),
              std::nullopt);
    // An old turn's report never leaks into a new turn that has none.
    EXPECT_EQ(ExtractResearchReport({Item(Kind::kAgent, "<<<ISLAND_REPORT_BEGIN>>>\n# Old\n"),
                                     Item(Kind::kUser, "new")}),
              std::nullopt);
}

TEST(ReportTitleTest, UsesTheFirstHeadingOutsideCodeFences) {
    EXPECT_EQ(ReportTitle("intro\n\n# The   Title ##\n\n## Sub", "q"), "The Title");
    EXPECT_EQ(ReportTitle("```\n# not a title\n```\n# Real", "q"), "Real");
}

TEST(ReportTitleTest, FallsBackToTheQuestion) {
    EXPECT_EQ(ReportTitle("no heading here\n## only h2", "  what  is   X? "), "what is X?");
    EXPECT_EQ(ReportTitle("", ""), "Research report");
    EXPECT_LE(ReportTitle("", std::string(500, 'z')).size(), 80U);
}

agent::AcpEvent Ended(std::string reason) {
    agent::AcpEvent event;
    event.type = agent::AcpEvent::Type::kTurnEnded;
    event.text = std::move(reason);
    return event;
}

agent::AcpEvent Chunk(std::string text) {
    agent::AcpEvent event;
    event.type = agent::AcpEvent::Type::kAgentMessageChunk;
    event.text = std::move(text);
    return event;
}

TEST(ClassifyResearchTurnTest, ASuccessfulTurnYieldsTheReport) {
    agent::AgentTranscript transcript;
    transcript.Apply(Ended("end_turn"));  // an earlier turn
    const std::uint64_t before = transcript.turns_ended();
    transcript.AddUserPrompt("research");
    transcript.Apply(Chunk("<<<ISLAND_REPORT_BEGIN>>>\n# R\nbody\n<<<ISLAND_REPORT_END>>>"));
    transcript.Apply(Ended("end_turn"));
    const ResearchOutcome outcome = ClassifyResearchTurn(transcript, before, "");
    EXPECT_EQ(outcome.kind, ResearchOutcome::Kind::kReport);
    EXPECT_EQ(outcome.markdown, "# R\nbody");
}

TEST(ClassifyResearchTurnTest, CancelErrorEarlyStopAndNoReportAreNotReports) {
    agent::AgentTranscript transcript;
    transcript.AddUserPrompt("research");
    transcript.Apply(Ended("cancelled"));
    EXPECT_EQ(ClassifyResearchTurn(transcript, 0, "").kind, ResearchOutcome::Kind::kCancelled);

    agent::AgentTranscript failed;
    failed.AddUserPrompt("research");
    agent::AcpEvent error;
    error.type = agent::AcpEvent::Type::kError;
    error.text = "boom";
    failed.Apply(error);
    ResearchOutcome outcome = ClassifyResearchTurn(failed, 0, "The agent exited (code 1).");
    EXPECT_EQ(outcome.kind, ResearchOutcome::Kind::kFailed);
    EXPECT_EQ(outcome.message, "The agent exited (code 1).");

    agent::AgentTranscript early;
    early.AddUserPrompt("research");
    early.Apply(Chunk("partial"));
    early.Apply(Ended("max_tokens"));
    outcome = ClassifyResearchTurn(early, 0, "");
    EXPECT_EQ(outcome.kind, ResearchOutcome::Kind::kFailed);
    EXPECT_NE(outcome.message.find("max_tokens"), std::string::npos);

    agent::AgentTranscript silent;
    silent.AddUserPrompt("research");
    silent.Apply(Ended("end_turn"));
    outcome = ClassifyResearchTurn(silent, 0, "");
    EXPECT_EQ(outcome.kind, ResearchOutcome::Kind::kFailed);
    EXPECT_NE(outcome.message.find("without writing a report"), std::string::npos);
}

TEST(ClassifyResearchTurnTest, APlanWithoutAReportAwaitsApproval) {
    agent::AgentTranscript transcript;
    transcript.Apply(Ended("end_turn"));  // an earlier turn
    const std::uint64_t before = transcript.turns_ended();
    transcript.AddUserPrompt("research");
    agent::AcpEvent plan;
    plan.type = agent::AcpEvent::Type::kPlan;
    agent::AcpPlanEntry entry;
    entry.content = "Open the sources";
    plan.plan.push_back(entry);
    transcript.Apply(plan);
    transcript.Apply(Ended("end_turn"));
    const ResearchOutcome outcome = ClassifyResearchTurn(transcript, before, "");
    EXPECT_EQ(outcome.kind, ResearchOutcome::Kind::kAwaitingApproval);
    EXPECT_EQ(outcome.message, "Waiting for approval");
    EXPECT_TRUE(outcome.markdown.empty());
}

TEST(ClassifyResearchTurnTest, AReportWinsOverAPlanInTheSameTurn) {
    agent::AgentTranscript transcript;
    transcript.AddUserPrompt("research");
    agent::AcpEvent plan;
    plan.type = agent::AcpEvent::Type::kPlan;
    plan.plan.push_back(agent::AcpPlanEntry{.content = "step"});
    transcript.Apply(plan);
    transcript.Apply(Chunk("<<<ISLAND_REPORT_BEGIN>>>\n# R\nbody\n<<<ISLAND_REPORT_END>>>"));
    transcript.Apply(Ended("end_turn"));
    EXPECT_EQ(ClassifyResearchTurn(transcript, 0, "").kind, ResearchOutcome::Kind::kReport);
}

TEST(ClassifyResearchTurnTest, APlanWithAgentChatterStillAwaitsApproval) {
    agent::AgentTranscript transcript;
    transcript.AddUserPrompt("research");
    agent::AcpEvent plan;
    plan.type = agent::AcpEvent::Type::kPlan;
    plan.plan.push_back(agent::AcpPlanEntry{.content = "Open the sources"});
    transcript.Apply(plan);
    // Plain explanatory text next to the plan is not a report.
    transcript.Apply(Chunk("Here is my plan. I will check the sources first."));
    transcript.Apply(Ended("end_turn"));
    const ResearchOutcome outcome = ClassifyResearchTurn(transcript, 0, "");
    EXPECT_EQ(outcome.kind, ResearchOutcome::Kind::kAwaitingApproval);
    EXPECT_TRUE(outcome.markdown.empty());
}

TEST(ClassifyResearchTurnTest, AnApprovedRunWithoutMarkersStillReportsItsText) {
    agent::AgentTranscript transcript;
    transcript.AddUserPrompt("the plan was approved");
    agent::AcpEvent plan;
    plan.type = agent::AcpEvent::Type::kPlan;
    plan.plan.push_back(agent::AcpPlanEntry{.content = "Open the sources", .status = "completed"});
    transcript.Apply(plan);
    transcript.Apply(Chunk("# Answer\nThe unmarked report."));
    transcript.Apply(Ended("end_turn"));
    const ResearchOutcome outcome =
        ClassifyResearchTurn(transcript, 0, "", /*plan_approved=*/true);
    EXPECT_EQ(outcome.kind, ResearchOutcome::Kind::kReport);
    EXPECT_EQ(outcome.markdown, "# Answer\nThe unmarked report.");
}

TEST(ClassifyResearchTurnTest, ATurnThatNeverEndedIsAFailure) {
    agent::AgentTranscript transcript;
    transcript.Apply(Ended("end_turn"));
    transcript.AddUserPrompt("research");
    EXPECT_EQ(ClassifyResearchTurn(transcript, 1, "").kind, ResearchOutcome::Kind::kFailed);
    EXPECT_EQ(ClassifyResearchTurn(transcript, 1, "No agent command is set.").message,
              "No agent command is set.");
}

TEST(ReportFileNameTest, SanitizesTheTitleAndAddsTheExtension) {
    EXPECT_EQ(ReportFileName("Why  is the sky blue?"), "Why is the sky blue?.md");
    EXPECT_EQ(ReportFileName("a/b\\c"), "c.md");
    EXPECT_EQ(ReportFileName(""), "report.md");
}

}  // namespace
}  // namespace island
