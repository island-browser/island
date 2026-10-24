#ifndef ISLAND_TESTS_AGENT_FAKE_BROWSER_HOST_H_
#define ISLAND_TESTS_AGENT_FAKE_BROWSER_HOST_H_

#include <algorithm>
#include <deque>
#include <functional>
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
    std::vector<std::string> resolved_targets;
    std::vector<DevToolsRecord> devtools;
    std::deque<DevToolsReply> replies;
    std::vector<std::string> trusted;
    std::vector<AgentBookmarkInfo> bookmarks = {
        {.id = 7,
         .title = "Island",
         .url = "https://island.test/",
         .folder_path = "Work / References",
         .favorite_scopes = {{}, {.space_id = 12}}},
        {.id = 8, .title = "Docs", .url = "https://docs.test/", .folder_path = "Unfiled"},
    };
    HostStatus bookmark_status = HostStatus::Ok();
    std::vector<ConfirmationRequest> confirmations;
    std::deque<ConfirmationAnswer> answers;  // empty answers kCancel
    bool defer_confirmations = false;
    std::deque<std::function<void(ConfirmationAnswer)>> pending_confirmations;

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

    std::vector<std::string> TrustedSites() override { return trusted; }
    void RequestConfirmation(ConfirmationRequest request,
                             std::function<void(ConfirmationAnswer)> done) override {
        const std::string host = request.host;
        confirmations.push_back(std::move(request));
        ConfirmationAnswer answer = ConfirmationAnswer::kCancel;
        if (!answers.empty()) {
            answer = answers.front();
            answers.pop_front();
        }
        if (answer == ConfirmationAnswer::kTrustSite) trusted.push_back(host);
        if (defer_confirmations) {
            pending_confirmations.push_back(std::move(done));
            return;
        }
        done(answer);
    }
    void AnswerNext(ConfirmationAnswer answer) {
        auto done = std::move(pending_confirmations.front());
        pending_confirmations.pop_front();
        done(answer);
    }
    std::optional<std::string> ResolveNavigationTarget(std::string_view text) override {
        resolved_targets.emplace_back(text);
        if (text.empty()) return std::nullopt;
        if (text.starts_with("javascript:")) return std::nullopt;
        if (text.find("://") != std::string_view::npos) return std::string(text);
        return "https://" + std::string(text);
    }
    void ListBookmarks(std::optional<std::string> folder_path,
                       AgentBookmarksCallback done) override {
        calls.push_back("list_bookmarks" + (folder_path ? ":" + *folder_path : std::string()));
        if (!bookmark_status.ok) {
            done(bookmark_status, {});
            return;
        }
        std::vector<AgentBookmarkInfo> result;
        for (const AgentBookmarkInfo& bookmark : bookmarks) {
            if (!folder_path || bookmark.folder_path == *folder_path) result.push_back(bookmark);
        }
        done(HostStatus::Ok(), std::move(result));
    }
    void ListFavorites(std::optional<AgentFavoriteScope> scope,
                       AgentBookmarksCallback done) override {
        calls.push_back("list_favorites");
        if (!bookmark_status.ok) {
            done(bookmark_status, {});
            return;
        }
        std::vector<AgentBookmarkInfo> result;
        for (const AgentBookmarkInfo& bookmark : bookmarks) {
            const bool matches =
                !bookmark.favorite_scopes.empty() &&
                (!scope ||
                 std::any_of(bookmark.favorite_scopes.begin(), bookmark.favorite_scopes.end(),
                             [&scope](const AgentFavoriteScope& favorite_scope) {
                                 return favorite_scope.space_id == scope->space_id;
                             }));
            if (matches) result.push_back(bookmark);
        }
        done(HostStatus::Ok(), std::move(result));
    }
    void AddBookmark(AgentBookmarkDraft bookmark, AgentBookmarkStatusCallback done) override {
        calls.push_back("add_bookmark:" + bookmark.url);
        done(bookmark_status);
    }
    void RemoveBookmark(std::uint64_t id, AgentBookmarkStatusCallback done) override {
        calls.push_back("remove_bookmark:" + std::to_string(id));
        done(bookmark_status);
    }
    void AddFavorite(std::uint64_t id, AgentFavoriteScope scope,
                     AgentBookmarkStatusCallback done) override {
        calls.push_back("add_favorite:" + std::to_string(id) + ":" +
                        (scope.space_id ? std::to_string(*scope.space_id) : "global"));
        done(bookmark_status);
    }
    void RemoveFavorite(std::uint64_t id, AgentFavoriteScope scope,
                        AgentBookmarkStatusCallback done) override {
        calls.push_back("remove_favorite:" + std::to_string(id) + ":" +
                        (scope.space_id ? std::to_string(*scope.space_id) : "global"));
        done(bookmark_status);
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
