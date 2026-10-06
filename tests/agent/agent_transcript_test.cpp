#include "agent_transcript.h"

#include <gtest/gtest.h>

namespace island::agent {
namespace {

AcpEvent Chunk(AcpEvent::Type type, std::string text) {
    AcpEvent event;
    event.type = type;
    event.text = std::move(text);
    return event;
}

AcpEvent Tool(AcpEvent::Type type, std::string id, std::string title, std::string status) {
    AcpEvent event;
    event.type = type;
    event.tool_call_id = std::move(id);
    event.title = std::move(title);
    event.status = std::move(status);
    return event;
}

TEST(AgentTranscriptTest, StreamsChunksIntoOneBubblePerRun) {
    AgentTranscript transcript;
    transcript.AddUserPrompt("hi");
    transcript.Apply(Chunk(AcpEvent::Type::kAgentThoughtChunk, "hmm "));
    transcript.Apply(Chunk(AcpEvent::Type::kAgentThoughtChunk, "ok"));
    transcript.Apply(Chunk(AcpEvent::Type::kAgentMessageChunk, "Hel"));
    transcript.Apply(Chunk(AcpEvent::Type::kAgentMessageChunk, "lo"));
    ASSERT_EQ(transcript.items().size(), 3U);
    EXPECT_EQ(transcript.items()[1].text, "hmm ok");
    EXPECT_EQ(transcript.items()[2].kind, TranscriptItem::Kind::kAgent);
    EXPECT_EQ(transcript.items()[2].text, "Hello");
}

TEST(AgentTranscriptTest, ToolCallsUpdateInPlace) {
    AgentTranscript transcript;
    transcript.Apply(Tool(AcpEvent::Type::kToolCall, "t1", "Open tab", "pending"));
    transcript.Apply(Chunk(AcpEvent::Type::kAgentMessageChunk, "working"));
    const auto revision = transcript.revision();
    transcript.Apply(Tool(AcpEvent::Type::kToolCallUpdate, "t1", "", "completed"));
    EXPECT_GT(transcript.revision(), revision);
    ASSERT_EQ(transcript.items().size(), 2U);
    EXPECT_EQ(transcript.items()[0].status, "completed");
    EXPECT_EQ(transcript.items()[0].text, "Open tab");
    // An update for an unseen call still shows up.
    transcript.Apply(Tool(AcpEvent::Type::kToolCallUpdate, "t9", "Late", "failed"));
    EXPECT_EQ(transcript.items().back().status, "failed");
}

TEST(AgentTranscriptTest, PlanUpdatesWithinATurnAndPermissionsResolve) {
    AgentTranscript transcript;
    transcript.AddUserPrompt("plan it");
    AcpEvent plan;
    plan.type = AcpEvent::Type::kPlan;
    plan.plan = {{"step 1", "pending"}};
    transcript.Apply(plan);
    plan.plan = {{"step 1", "completed"}, {"step 2", "in_progress"}};
    transcript.Apply(plan);
    ASSERT_EQ(transcript.items().size(), 2U);
    EXPECT_EQ(transcript.items()[1].plan.size(), 2U);

    AcpEvent permission;
    permission.type = AcpEvent::Type::kPermissionRequest;
    permission.request_id = 4;
    permission.title = "Click Buy";
    permission.options = {{"a", "Allow", "allow_once"}};
    transcript.Apply(permission);
    transcript.ResolvePermission(4, "Allow");
    EXPECT_EQ(transcript.items().back().resolution, "Allow");

    transcript.Apply(permission);
    AcpEvent ended;
    ended.type = AcpEvent::Type::kTurnEnded;
    ended.text = "cancelled";
    transcript.Apply(ended);
    EXPECT_EQ(transcript.items()[3].resolution, "Cancelled");
    EXPECT_EQ(transcript.items().back().text, "Stopped.");
}

TEST(AgentTranscriptTest, AFailedTurnClosesOpenPermissionPrompts) {
    AgentTranscript transcript;
    AcpEvent permission;
    permission.type = AcpEvent::Type::kPermissionRequest;
    permission.request_id = 7;
    permission.title = "Submit form";
    permission.options = {{"a", "Allow", "allow_once"}};
    transcript.Apply(permission);
    AcpEvent failed;
    failed.type = AcpEvent::Type::kError;
    failed.text = "The turn failed: boom";
    transcript.Apply(failed);
    ASSERT_EQ(transcript.items().size(), 2U);
    EXPECT_EQ(transcript.items()[0].resolution, "Cancelled");
    EXPECT_EQ(transcript.items()[1].kind, TranscriptItem::Kind::kError);
}

TEST(AgentTranscriptTest, JsonCarriesKindsAndIsBounded) {
    AgentTranscript transcript;
    transcript.AddUserPrompt("a \"quote\"");
    transcript.Apply(Tool(AcpEvent::Type::kToolCall, "t1", "Read", "in_progress"));
    const auto parsed = json::Parse(transcript.ToJson());
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->array_val.size(), 2U);
    EXPECT_EQ(parsed->array_val[0].StringOr("kind", ""), "user");
    EXPECT_EQ(parsed->array_val[0].StringOr("text", ""), "a \"quote\"");
    EXPECT_EQ(parsed->array_val[1].StringOr("status", ""), "in_progress");

    for (std::size_t i = 0; i < AgentTranscript::kMaxItems + 20; ++i) {
        transcript.AddNotice("n" + std::to_string(i));
    }
    EXPECT_EQ(transcript.items().size(), AgentTranscript::kMaxItems);
    EXPECT_EQ(transcript.items().back().text,
              "n" + std::to_string(AgentTranscript::kMaxItems + 19));
}

}  // namespace
}  // namespace island::agent
