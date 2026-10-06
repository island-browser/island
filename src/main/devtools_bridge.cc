#include "devtools_bridge.h"

#include <map>
#include <optional>
#include <utility>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_registration.h"
#include "include/cef_task.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "json_util.h"

namespace island {

namespace {

agent::DevToolsReply ErrorReply(std::string message) {
    agent::DevToolsReply reply;
    reply.ok = false;
    reply.error = std::move(message);
    return reply;
}

}  // namespace

struct DevToolsBridge::State {
    class Observer;

    struct BrowserEntry {
        CefRefPtr<CefRegistration> registration;
        std::map<int, agent::DevToolsCallback> pending;
    };

    std::map<int, BrowserEntry> browsers;
    int next_message_id = 1;
    bool shut_down = false;

    // Removes and returns the pending callback, if it is still pending.
    agent::DevToolsCallback Take(int browser_id, int message_id) {
        auto browser = browsers.find(browser_id);
        if (browser == browsers.end()) return {};
        auto it = browser->second.pending.find(message_id);
        if (it == browser->second.pending.end()) return {};
        agent::DevToolsCallback done = std::move(it->second);
        browser->second.pending.erase(it);
        return done;
    }
};

class DevToolsBridge::State::Observer final : public CefDevToolsMessageObserver {
  public:
    explicit Observer(std::weak_ptr<State> state) : state_(std::move(state)) {}

    void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int message_id, bool success,
                                const void* result, size_t result_size) override {
        CEF_REQUIRE_UI_THREAD();
        std::shared_ptr<State> state = state_.lock();
        if (state == nullptr) return;
        agent::DevToolsCallback done = state->Take(browser->GetIdentifier(), message_id);
        if (!done) return;
        const std::string_view text(static_cast<const char*>(result),
                                    result != nullptr ? result_size : 0);
        std::optional<json::Value> parsed =
            text.empty() ? std::optional<json::Value>(json::Value::MakeObject())
                         : json::Parse(text);
        if (!success) {
            std::string message = "DevTools error";
            if (parsed && parsed->IsObject()) {
                const std::string_view detail = parsed->StringOr("message", "");
                if (!detail.empty()) message = std::string(detail);
            }
            done(ErrorReply(std::move(message)));
            return;
        }
        if (!parsed) {
            done(ErrorReply("DevTools returned unparseable JSON"));
            return;
        }
        agent::DevToolsReply reply;
        reply.ok = true;
        reply.result = std::move(*parsed);
        done(std::move(reply));
    }

  private:
    std::weak_ptr<State> state_;

    IMPLEMENT_REFCOUNTING(Observer);
};

namespace {

void ExpirePending(std::weak_ptr<DevToolsBridge::State> weak_state, int browser_id,
                   int message_id) {
    std::shared_ptr<DevToolsBridge::State> state = weak_state.lock();
    if (state == nullptr) return;
    agent::DevToolsCallback done = state->Take(browser_id, message_id);
    if (done) done(ErrorReply("The page did not answer within 30 seconds."));
}

}  // namespace

DevToolsBridge::DevToolsBridge() : state_(std::make_shared<State>()) {}

DevToolsBridge::~DevToolsBridge() { Shutdown(); }

void DevToolsBridge::Call(CefRefPtr<CefBrowser> browser, std::string_view method,
                          json::Value params, agent::DevToolsCallback done) {
    CEF_REQUIRE_UI_THREAD();
    if (state_->shut_down) {
        done(ErrorReply("The browser is shutting down."));
        return;
    }
    if (browser == nullptr || browser->GetHost() == nullptr) {
        done(ErrorReply("That tab has no live page yet. Activate it first."));
        return;
    }
    CefRefPtr<CefDictionaryValue> dictionary;
    if (params.IsObject() && !params.object_val.empty()) {
        CefRefPtr<CefValue> value = CefParseJSON(json::Serialize(params), JSON_PARSER_RFC);
        if (value == nullptr || value->GetType() != VTYPE_DICTIONARY) {
            done(ErrorReply("Could not encode DevTools parameters."));
            return;
        }
        dictionary = value->GetDictionary();
    }

    const int browser_id = browser->GetIdentifier();
    State::BrowserEntry& entry = state_->browsers[browser_id];
    if (entry.registration == nullptr) {
        entry.registration =
            browser->GetHost()->AddDevToolsMessageObserver(new State::Observer(state_));
    }
    const int message_id = state_->next_message_id++;
    entry.pending.emplace(message_id, std::move(done));
    const int sent =
        browser->GetHost()->ExecuteDevToolsMethod(message_id, std::string(method), dictionary);
    if (sent == 0) {
        agent::DevToolsCallback failed = state_->Take(browser_id, message_id);
        if (failed) failed(ErrorReply("DevTools rejected " + std::string(method) + "."));
        return;
    }
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(
                           &ExpirePending, std::weak_ptr<State>(state_), browser_id, message_id)),
                       kTimeoutMs);
}

void DevToolsBridge::ForgetBrowser(CefRefPtr<CefBrowser> browser) {
    if (browser == nullptr) return;
    auto it = state_->browsers.find(browser->GetIdentifier());
    if (it == state_->browsers.end()) return;
    std::map<int, agent::DevToolsCallback> pending = std::move(it->second.pending);
    state_->browsers.erase(it);
    for (auto& [id, done] : pending) {
        if (done) done(ErrorReply("The tab closed."));
    }
}

void DevToolsBridge::Shutdown() {
    if (state_->shut_down) return;
    state_->shut_down = true;
    std::map<int, State::BrowserEntry> browsers = std::move(state_->browsers);
    state_->browsers.clear();
    for (auto& [browser_id, entry] : browsers) {
        for (auto& [id, done] : entry.pending) {
            if (done) done(ErrorReply("The browser is shutting down."));
        }
    }
}

}  // namespace island
