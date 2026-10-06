#ifndef ISLAND_TESTS_AGENT_FAKE_BROWSER_HOST_H_
#define ISLAND_TESTS_AGENT_FAKE_BROWSER_HOST_H_

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "browser_tools.h"

namespace island::agent::testing {

// Records every host call and answers DevTools calls from a scripted queue
// (synchronously, like a CEF reply arriving before the next tool step).
class FakeBrowserHost : public AgentBrowserHost {
  public:
    struct DevToolsRecord {
        std::optional<std::size_t> tab;
        std::string method;
        json::Value params;
    };

    std::vector<AgentSpaceInfo> spaces = {
        {.index = 0, .name = "Focus", .color_argb = 0xFF168C99U, .tab_count = 2, .active = true},
        {.index = 1, .name = "Research", .color_argb = 0xFF7B8588U, .tab_count = 0},
    };
    std::vector<AgentTabInfo> tabs = {
        {.index = 0, .title = "Island", .url = "https://island.test/", .active = true},
        {.index = 1, .title = "Docs", .url = "https://docs.test/", .loading = true},
    };
    std::vector<std::string> calls;
    std::vector<DevToolsRecord> devtools;
    std::deque<DevToolsReply> replies;

    std::vector<AgentSpaceInfo> ListSpaces() override { return spaces; }
    std::vector<AgentTabInfo> ListTabs() override { return tabs; }
    HostStatus OpenTab(std::string_view url) override {
        calls.push_back("open:" + std::string(url));
        return HostStatus::Ok();
    }
    HostStatus Navigate(std::optional<std::size_t> tab, std::string_view url) override {
        calls.push_back("navigate:" + (tab ? std::to_string(*tab) : std::string("active")) + ":" +
                        std::string(url));
        return HostStatus::Ok();
    }
    HostStatus ActivateTab(std::size_t tab) override {
        if (tab >= tabs.size()) return HostStatus::Error("No tab at index " + std::to_string(tab));
        calls.push_back("activate:" + std::to_string(tab));
        return HostStatus::Ok();
    }
    HostStatus CloseTab(std::size_t tab) override {
        calls.push_back("close:" + std::to_string(tab));
        return HostStatus::Ok();
    }
    HostStatus PinTab(std::size_t tab, bool pinned) override {
        calls.push_back("pin:" + std::to_string(tab) + ":" + (pinned ? "1" : "0"));
        return HostStatus::Ok();
    }
    HostStatus SwitchSpace(std::size_t space) override {
        calls.push_back("space:" + std::to_string(space));
        return HostStatus::Ok();
    }
    HostStatus NewSpace(std::string_view name) override {
        calls.push_back("new_space:" + std::string(name));
        return HostStatus::Ok();
    }
    HostStatus RunAction(AgentBrowserAction action) override {
        calls.push_back("action:" + std::to_string(static_cast<int>(action)));
        return HostStatus::Ok();
    }
    void DevToolsCall(std::optional<std::size_t> tab, std::string_view method, json::Value params,
                      DevToolsCallback done) override {
        devtools.push_back({tab, std::string(method), std::move(params)});
        DevToolsReply reply;
        if (replies.empty()) {
            reply.ok = true;
            reply.result = json::Value::MakeObject();
        } else {
            reply = std::move(replies.front());
            replies.pop_front();
        }
        done(std::move(reply));
    }

    // Queues a Runtime.evaluate reply whose value is `json_text` as a string,
    // the way the toolbox's page scripts return their results.
    void QueueScriptResult(std::string json_text) {
        DevToolsReply reply;
        reply.ok = true;
        reply.result = json::Value::MakeObject().Set(
            "result", json::Value::MakeObject()
                          .Set("type", json::Value::String("string"))
                          .Set("value", json::Value::String(std::move(json_text))));
        replies.push_back(std::move(reply));
    }
    void QueueOk() { replies.push_back({.ok = true, .result = json::Value::MakeObject()}); }
};

}  // namespace island::agent::testing

#endif  // ISLAND_TESTS_AGENT_FAKE_BROWSER_HOST_H_
