#include "acp_client.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace island::agent {
namespace {

using json::Value;

class AcpClientTest : public ::testing::Test {
  protected:
    std::vector<std::string> sent_;
    std::vector<AcpEvent> events_;
    AcpClient client_{[this](std::string line) { sent_.push_back(std::move(line)); },
                      [this](const AcpEvent& event) { events_.push_back(event); }};

    Value LastSent() {
        EXPECT_FALSE(sent_.empty());
        auto parsed = json::Parse(sent_.back());
        EXPECT_TRUE(parsed.has_value());
        return parsed.value_or(Value{});
    }

    void Handshake(bool http_mcp) {
        client_.Start("/home/user", {{.name = "island",
                                      .url = "http://127.0.0.1:9223/mcp",
                                      .bearer_token = "tok",
                                      .bridge_command = "/opt/island/island_mcp_bridge"}});
        ASSERT_EQ(LastSent().StringOr("method", ""), "initialize");
        client_.HandleLine(
            std::string(
                R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":1,"agentCapabilities":{"loadSession":false,"mcpCapabilities":{"http":)") +
            (http_mcp ? "true" : "false") +
            R"(}},"agentInfo":{"name":"claude-code","title":"Claude Code"},"authMethods":[]}})");
        ASSERT_EQ(LastSent().StringOr("method", ""), "session/new");
        client_.HandleLine(R"({"jsonrpc":"2.0","id":2,"result":{"sessionId":"sess-1"}})");
        ASSERT_EQ(client_.state(), AcpState::kReady);
    }

    std::vector<AcpEvent::Type> EventTypes() const {
        std::vector<AcpEvent::Type> types;
        for (const AcpEvent& event : events_) types.push_back(event.type);
        return types;
    }
};

TEST_F(AcpClientTest, InitializeAdvertisesNoFsOrTerminal) {
    client_.Start("/tmp", {});
    const Value init = LastSent();
    EXPECT_EQ(init.StringOr("jsonrpc", ""), "2.0");
    const Value* params = init.FindMember("params");
    EXPECT_EQ(params->IntOr("protocolVersion", 0), 1);
    const Value* caps = params->FindMember("clientCapabilities");
    EXPECT_FALSE(caps->FindMember("fs")->BoolOr("readTextFile", true));
    EXPECT_FALSE(caps->BoolOr("terminal", true));
    EXPECT_EQ(client_.state(), AcpState::kInitializing);
}

TEST_F(AcpClientTest, HttpCapableAgentsGetTheHttpMcpServer) {
    Handshake(/*http_mcp=*/true);
    const Value new_session = json::Parse(sent_[1]).value();
    const Value* params = new_session.FindMember("params");
    EXPECT_EQ(params->StringOr("cwd", ""), "/home/user");
    const Value& server = params->FindMember("mcpServers")->array_val.at(0);
    EXPECT_EQ(server.StringOr("type", ""), "http");
    EXPECT_EQ(server.StringOr("url", ""), "http://127.0.0.1:9223/mcp");
    EXPECT_EQ(server.FindMember("headers")->array_val.at(0).StringOr("value", ""), "Bearer tok");
    EXPECT_EQ(client_.agent_name(), "Claude Code");
    EXPECT_EQ(client_.session_id(), "sess-1");
}

TEST_F(AcpClientTest, OtherAgentsGetTheStdioBridgeWithTokenInEnv) {
    Handshake(/*http_mcp=*/false);
    const Value new_session = json::Parse(sent_[1]).value();
    const Value& server =
        new_session.FindMember("params")->FindMember("mcpServers")->array_val.at(0);
    EXPECT_EQ(server.FindMember("type"), nullptr);
    EXPECT_EQ(server.StringOr("command", ""), "/opt/island/island_mcp_bridge");
    EXPECT_TRUE(server.FindMember("args")->array_val.empty());
    const Value& env = *server.FindMember("env");
    ASSERT_EQ(env.array_val.size(), 2U);
    EXPECT_EQ(env.array_val[1].StringOr("name", ""), "ISLAND_MCP_TOKEN");
    EXPECT_EQ(env.array_val[1].StringOr("value", ""), "tok");
}

TEST_F(AcpClientTest, PromptStreamsUpdatesAndEndsTurn) {
    Handshake(true);
    events_.clear();
    EXPECT_TRUE(client_.Prompt("Open the docs"));
    EXPECT_FALSE(client_.Prompt("again"));  // one turn at a time
    const Value prompt = LastSent();
    EXPECT_EQ(prompt.StringOr("method", ""), "session/prompt");
    EXPECT_EQ(prompt.FindMember("params")->StringOr("sessionId", ""), "sess-1");
    EXPECT_EQ(prompt.FindMember("params")->FindMember("prompt")->array_val[0].StringOr("text", ""),
              "Open the docs");
    const std::int64_t prompt_id = prompt.IntOr("id", 0);

    client_.HandleLine(
        R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"plan","entries":[{"content":"Find docs","priority":"high","status":"in_progress"}]}}})");
    client_.HandleLine(
        R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"agent_thought_chunk","content":{"type":"text","text":"thinking"}}}})");
    client_.HandleLine(
        R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"tool_call","toolCallId":"t1","title":"browser_open_tab","kind":"fetch","status":"pending"}}})");
    client_.HandleLine(
        R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"tool_call_update","toolCallId":"t1","status":"completed"}}})");
    client_.HandleLine(
        R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"sess-1","update":{"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"Done \u2713"}}}})");
    client_.HandleLine("debug output that is not JSON");
    client_.HandleLine(R"({"jsonrpc":"2.0","id":)" + std::to_string(prompt_id) +
                       R"(,"result":{"stopReason":"end_turn"}})");

    EXPECT_EQ(EventTypes(), (std::vector<AcpEvent::Type>{
                                AcpEvent::Type::kStateChanged, AcpEvent::Type::kPlan,
                                AcpEvent::Type::kAgentThoughtChunk, AcpEvent::Type::kToolCall,
                                AcpEvent::Type::kToolCallUpdate, AcpEvent::Type::kAgentMessageChunk,
                                AcpEvent::Type::kStateChanged, AcpEvent::Type::kTurnEnded}));
    EXPECT_EQ(events_[1].plan.at(0).content, "Find docs");
    EXPECT_EQ(events_[3].tool_call_id, "t1");
    EXPECT_EQ(events_[4].status, "completed");
    EXPECT_EQ(events_[5].text, "Done \xE2\x9C\x93");
    EXPECT_EQ(events_[7].text, "end_turn");
    EXPECT_EQ(client_.state(), AcpState::kReady);
}

TEST_F(AcpClientTest, PermissionRequestsRoundTripTheAgentId) {
    Handshake(true);
    ASSERT_TRUE(client_.Prompt("go"));
    events_.clear();
    client_.HandleLine(
        R"({"jsonrpc":"2.0","id":"perm-7","method":"session/request_permission","params":{"sessionId":"sess-1","toolCall":{"toolCallId":"t2","title":"page_click","kind":"execute"},"options":[{"optionId":"allow","name":"Allow","kind":"allow_once"},{"optionId":"deny","name":"Deny","kind":"reject_once"}]}})");
    ASSERT_EQ(events_.size(), 1U);
    const AcpEvent& request = events_[0];
    EXPECT_EQ(request.type, AcpEvent::Type::kPermissionRequest);
    ASSERT_EQ(request.options.size(), 2U);
    EXPECT_EQ(request.options[0].kind, "allow_once");
    EXPECT_EQ(client_.pending_permission_count(), 1U);

    client_.ResolvePermission(request.request_id, std::string("allow"));
    const Value answer = LastSent();
    EXPECT_EQ(answer.StringOr("id", ""), "perm-7");
    const Value* outcome = answer.FindMember("result")->FindMember("outcome");
    EXPECT_EQ(outcome->StringOr("outcome", ""), "selected");
    EXPECT_EQ(outcome->StringOr("optionId", ""), "allow");
    EXPECT_EQ(client_.pending_permission_count(), 0U);
    const std::size_t sent = sent_.size();
    client_.ResolvePermission(request.request_id, std::string("allow"));  // already answered
    EXPECT_EQ(sent_.size(), sent);
}

TEST_F(AcpClientTest, CancelAnswersOpenPermissionsThenNotifies) {
    Handshake(true);
    ASSERT_TRUE(client_.Prompt("go"));
    client_.HandleLine(
        R"({"jsonrpc":"2.0","id":11,"method":"session/request_permission","params":{"sessionId":"sess-1","toolCall":{"toolCallId":"t3"},"options":[]}})");
    const std::size_t before = sent_.size();
    client_.Cancel();
    ASSERT_EQ(sent_.size(), before + 2);
    const Value answer = json::Parse(sent_[before]).value();
    EXPECT_EQ(answer.IntOr("id", 0), 11);
    EXPECT_EQ(answer.FindMember("result")->FindMember("outcome")->StringOr("outcome", ""),
              "cancelled");
    EXPECT_EQ(LastSent().StringOr("method", ""), "session/cancel");
}

TEST_F(AcpClientTest, UnsupportedClientMethodsGetMethodNotFound) {
    Handshake(true);
    client_.HandleLine(
        R"({"jsonrpc":"2.0","id":5,"method":"fs/read_text_file","params":{"path":"/etc/passwd"}})");
    const Value reply = LastSent();
    EXPECT_EQ(reply.IntOr("id", 0), 5);
    EXPECT_EQ(reply.FindMember("error")->IntOr("code", 0), -32601);
}

TEST_F(AcpClientTest, VersionMismatchAndAuthAndExitFailTheSession) {
    client_.Start("/tmp", {});
    client_.HandleLine(R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":99}})");
    EXPECT_EQ(client_.state(), AcpState::kFailed);
    EXPECT_NE(events_[events_.size() - 2].text.find("version 99"), std::string::npos);

    events_.clear();
    client_.Start("/tmp", {});
    const std::int64_t init_id = LastSent().IntOr("id", 0);
    client_.HandleLine(R"({"jsonrpc":"2.0","id":)" + std::to_string(init_id) +
                       R"(,"result":{"protocolVersion":1}})");
    const std::int64_t session_id = LastSent().IntOr("id", 0);
    client_.HandleLine(R"({"jsonrpc":"2.0","id":)" + std::to_string(session_id) +
                       R"(,"error":{"code":-32000,"message":"Authentication required"}})");
    EXPECT_EQ(client_.state(), AcpState::kFailed);
    bool saw_sign_in = false;
    for (const AcpEvent& event : events_) {
        if (event.type == AcpEvent::Type::kError &&
            event.text.find("sign in") != std::string::npos) {
            saw_sign_in = true;
        }
    }
    EXPECT_TRUE(saw_sign_in);

    events_.clear();
    client_.Start("/tmp", {});
    client_.OnTransportClosed("exited with code 1");
    EXPECT_EQ(client_.state(), AcpState::kFailed);
    EXPECT_FALSE(client_.Prompt("hello"));
}

TEST_F(AcpClientTest, PromptErrorsReturnToReady) {
    Handshake(true);
    ASSERT_TRUE(client_.Prompt("go"));
    const std::int64_t id = LastSent().IntOr("id", 0);
    client_.HandleLine(R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
                       R"(,"error":{"code":-32603,"message":"rate limited"}})");
    EXPECT_EQ(client_.state(), AcpState::kReady);
    EXPECT_EQ(events_.back().type, AcpEvent::Type::kError);
    EXPECT_NE(events_.back().text.find("rate limited"), std::string::npos);
}

}  // namespace
}  // namespace island::agent
