#include "agent_endpoint.h"

#include <gtest/gtest.h>

#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include "agent_process.h"
#include "fake_browser_host.h"
#include "http_client.h"

namespace island::agent {
namespace {

using json::Value;

HttpRequest MakeRequest(std::string token) {
    HttpRequest request;
    request.method = "POST";
    request.target = "/mcp";
    request.headers = {{"Host", "127.0.0.1:9223"}, {"Authorization", "Bearer " + token}};
    return request;
}

TEST(AgentEndpointPolicyTest, LoopbackHostAndOriginChecks) {
    EXPECT_TRUE(IsLoopbackHost("127.0.0.1:9223"));
    EXPECT_TRUE(IsLoopbackHost("localhost"));
    EXPECT_TRUE(IsLoopbackHost("[::1]:80"));
    EXPECT_FALSE(IsLoopbackHost("evil.test:9223"));
    EXPECT_FALSE(IsLoopbackHost("127.0.0.1.evil.test"));
    EXPECT_FALSE(IsLoopbackHost(""));
    EXPECT_TRUE(IsLoopbackOrigin("http://localhost:3000"));
    EXPECT_TRUE(IsLoopbackOrigin("https://127.0.0.1"));
    EXPECT_FALSE(IsLoopbackOrigin("https://evil.test"));
    EXPECT_FALSE(IsLoopbackOrigin("null"));
    EXPECT_FALSE(IsLoopbackOrigin("http://localhost.evil.test"));
}

TEST(AgentEndpointPolicyTest, RequestsNeedTokenLoopbackAndPost) {
    testing::FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    AgentEndpoint endpoint(toolbox, [](std::function<void()> task) { task(); });
    ASSERT_EQ(endpoint.token().size(), 64U);

    EXPECT_EQ(endpoint.CheckRequest(MakeRequest(endpoint.token())).status, 0);

    EXPECT_EQ(endpoint.CheckRequest(MakeRequest("wrong")).status, 401);

    HttpRequest rebinding = MakeRequest(endpoint.token());
    rebinding.headers[0].value = "attacker.test:9223";
    EXPECT_EQ(endpoint.CheckRequest(rebinding).status, 403);

    HttpRequest cross_origin = MakeRequest(endpoint.token());
    cross_origin.headers.push_back({"Origin", "https://attacker.test"});
    EXPECT_EQ(endpoint.CheckRequest(cross_origin).status, 403);

    HttpRequest get = MakeRequest(endpoint.token());
    get.method = "GET";
    EXPECT_EQ(endpoint.CheckRequest(get).status, 405);

    HttpRequest other_path = MakeRequest(endpoint.token());
    other_path.target = "/json";
    EXPECT_EQ(endpoint.CheckRequest(other_path).status, 404);
}

TEST(HttpServerTest, ParsesRequestHeads) {
    HttpRequest request;
    ASSERT_TRUE(ParseHttpRequestHead(
        "POST /mcp?x=1 HTTP/1.1\r\nHost: 127.0.0.1\r\ncontent-length:  12 \r\n", request));
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.target, "/mcp?x=1");
    EXPECT_EQ(request.Header("Content-Length"), "12");
    EXPECT_FALSE(ParseHttpRequestHead("garbage", request));
    EXPECT_FALSE(ParseHttpRequestHead("get / HTTP/1.1", request));
    EXPECT_FALSE(ParseHttpRequestHead("POST / HTTP/1.1\r\nBad Header: x", request));
}

TEST(HttpClientTest, ParsesOnlyLoopbackUrls) {
    const auto url = ParseLoopbackUrl("http://127.0.0.1:9223/mcp");
    ASSERT_TRUE(url.has_value());
    EXPECT_EQ(url->port, 9223);
    EXPECT_EQ(url->path, "/mcp");
    EXPECT_FALSE(ParseLoopbackUrl("http://example.com:9223/mcp").has_value());
    EXPECT_FALSE(ParseLoopbackUrl("https://127.0.0.1:9223/mcp").has_value());
    EXPECT_FALSE(ParseLoopbackUrl("http://127.0.0.1:99999/mcp").has_value());
}

#if !defined(_WIN32)

class LiveEndpointTest : public ::testing::Test {
  protected:
    void SetUp() override {
        std::string error;
        ASSERT_TRUE(endpoint_.Start(0, &error)) << error;
        ASSERT_GT(endpoint_.port(), 0);
    }

    std::optional<HttpResponse> Post(std::string_view body, std::string token) {
        return LoopbackHttpRequest(
            endpoint_.port(), "POST", "/mcp",
            {{"Content-Type", "application/json"}, {"Authorization", "Bearer " + token}}, body);
    }

    testing::FakeBrowserHost host_;
    BrowserToolbox toolbox_{host_};
    AgentEndpoint endpoint_{toolbox_, [](std::function<void()> task) { task(); }};
};

TEST_F(LiveEndpointTest, ServesMcpOverHttp) {
    const auto response =
        Post(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})", endpoint_.token());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->status, 200);
    EXPECT_EQ(response->content_type, "application/json");
    const auto body = json::Parse(response->body);
    ASSERT_TRUE(body.has_value()) << response->body;
    EXPECT_FALSE(body->FindMember("result")->FindMember("tools")->array_val.empty());

    const auto notification =
        Post(R"({"jsonrpc":"2.0","method":"notifications/initialized"})", endpoint_.token());
    ASSERT_TRUE(notification.has_value());
    EXPECT_EQ(notification->status, 202);
    EXPECT_TRUE(notification->body.empty());

    const auto unauthorized = Post(R"({"jsonrpc":"2.0","id":1,"method":"ping"})", "nope");
    ASSERT_TRUE(unauthorized.has_value());
    EXPECT_EQ(unauthorized->status, 401);
}

TEST_F(LiveEndpointTest, HandlesConcurrentClients) {
    std::vector<std::thread> clients;
    std::atomic<int> ok{0};
    for (int i = 0; i < 8; ++i) {
        clients.emplace_back([&, i] {
            const auto response =
                Post(R"({"jsonrpc":"2.0","id":)" + std::to_string(i) + R"(,"method":"ping"})",
                     endpoint_.token());
            if (response && response->status == 200) ++ok;
        });
    }
    for (std::thread& client : clients) client.join();
    EXPECT_EQ(ok.load(), 8);
}

TEST_F(LiveEndpointTest, DiscoveryFileIsOwnerOnlyAndRemovedOnStop) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("island-agent-test-" + endpoint_.token().substr(0, 8));
    const std::filesystem::path file = dir / "agent-endpoint.json";
    ASSERT_TRUE(endpoint_.WriteDiscoveryFile(file));
    std::ifstream in(file);
    std::stringstream text;
    text << in.rdbuf();
    std::string content = text.str();
    while (!content.empty() && content.back() == '\n') content.pop_back();
    const auto parsed = json::Parse(content);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->StringOr("url", ""), endpoint_.url());
    EXPECT_EQ(parsed->StringOr("token", ""), endpoint_.token());
    const auto perms = std::filesystem::status(file).permissions();
    EXPECT_EQ(perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all),
              std::filesystem::perms::none);
    endpoint_.Stop();
    EXPECT_FALSE(std::filesystem::exists(file));
    std::filesystem::remove_all(dir);
}

#if defined(ISLAND_AGENT_BRIDGE_PATH)
TEST_F(LiveEndpointTest, StdioBridgeRelaysToTheEndpoint) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                      ("island-bridge-test-" + endpoint_.token().substr(0, 8));
    const std::filesystem::path file = dir / "endpoint.json";
    ASSERT_TRUE(endpoint_.WriteDiscoveryFile(file));

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> lines;
    bool exited = false;
    AgentProcess bridge;
    std::string error;
    ASSERT_TRUE(bridge.Start(
        {ISLAND_AGENT_BRIDGE_PATH, "--endpoint-file", file.string()}, dir,
        [&](std::string line) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(std::move(line));
            cv.notify_all();
        },
        [&](int, std::string) {
            std::lock_guard<std::mutex> lock(mutex);
            exited = true;
            cv.notify_all();
        },
        &error))
        << error;
    ASSERT_TRUE(bridge.WriteLine(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
    ASSERT_TRUE(bridge.WriteLine(
        R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"browser_list_tabs"}})"));
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&] { return !lines.empty(); }));
    }
    const auto reply = json::Parse(lines.front());
    ASSERT_TRUE(reply.has_value()) << lines.front();
    EXPECT_EQ(reply->IntOr("id", 0), 9);
    const Value* content = reply->FindMember("result")->FindMember("content");
    ASSERT_NE(content, nullptr);
    EXPECT_NE(content->array_val[0].StringOr("text", "").find("island.test"),
              std::string_view::npos);
    bridge.Terminate();
    std::filesystem::remove_all(dir);
}
#endif

#endif

}  // namespace
}  // namespace island::agent
