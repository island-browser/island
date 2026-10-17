#include "action_gate.h"

#include <gtest/gtest.h>

namespace island::agent {
namespace {

ElementFacts Button(std::string text, bool submit = false, bool in_form = false) {
    ElementFacts f;
    f.tag = "button";
    f.text = std::move(text);
    f.is_submit = submit;
    f.in_form = in_form;
    f.page_origin = "https://shop.example.com";
    return f;
}

ElementFacts Input(std::string type, std::string autocomplete = {}, std::string name = {}) {
    ElementFacts f;
    f.tag = "input";
    f.type = std::move(type);
    f.autocomplete = std::move(autocomplete);
    f.name = std::move(name);
    f.in_form = true;
    f.page_origin = "https://shop.example.com";
    return f;
}

GateInput Call(std::string tool, std::optional<ElementFacts> facts = {}) {
    GateInput in;
    in.tool = std::move(tool);
    in.facts = std::move(facts);
    in.page_url = "https://shop.example.com/cart";
    return in;
}

const std::vector<std::string> kNone;
const std::vector<std::string> kTrusted = {"example.com"};

TEST(ActionGateTest, ReadingAndLinksAreFree) {
    for (const char* tool : {"page_read", "page_snapshot", "page_scroll", "page_screenshot",
                             "browser_open_tab", "browser_list_tabs"}) {
        EXPECT_EQ(Classify(Call(tool), kNone).action, ActionClass::kFree) << tool;
    }
    EXPECT_EQ(Classify(Call("page_click", Button("Next page")), kNone).action, ActionClass::kFree);
    EXPECT_EQ(Classify(Call("page_type", Input("search")), kNone).action, ActionClass::kFree);
}

TEST(ActionGateTest, PasswordFieldsAreBlockedEvenWhenTrusted) {
    for (auto facts : {Input("password"), Input("text", "current-password"),
                       Input("text", "new-password"), Input("text", "one-time-code")}) {
        for (const auto* trusted : {&kNone, &kTrusted}) {
            GateDecision d = Classify(Call("page_type", facts), *trusted);
            EXPECT_EQ(d.action, ActionClass::kBlocked);
            EXPECT_EQ(d.rule, GateRule::kPassword);
        }
    }
    EXPECT_EQ(Classify(Call("page_press_key", Input("password")), kTrusted).action,
              ActionClass::kBlocked);
}

TEST(ActionGateTest, CardFieldsAlwaysAsk) {
    for (auto facts :
         {Input("text", "cc-number"), Input("text", "cc-csc"), Input("text", {}, "cardNumber"),
          Input("tel", {}, "cvv"), Input("text", {}, "exp-date-expiry")}) {
        GateDecision d = Classify(Call("page_type", facts), kTrusted);
        EXPECT_EQ(d.action, ActionClass::kConfirm) << facts.name << facts.autocomplete;
        EXPECT_EQ(d.rule, GateRule::kCardField);
        EXPECT_FALSE(d.trusted_allowed);
    }
}

TEST(ActionGateTest, SubmitsAndConsequentialWordsAskUnlessTrusted) {
    EXPECT_EQ(Classify(Call("page_click", Button("Go", true, true)), kNone).rule,
              GateRule::kSubmit);
    GateDecision word = Classify(Call("page_click", Button("Place order")), kNone);
    EXPECT_EQ(word.action, ActionClass::kConfirm);
    EXPECT_EQ(word.rule, GateRule::kConsequentialWord);
    EXPECT_EQ(word.summary, "Click \"Place order\" on shop.example.com");
    EXPECT_EQ(Classify(Call("page_click", Button("Place order")), kTrusted).action,
              ActionClass::kFree);
}

TEST(ActionGateTest, EveryConsequentialWordAsks) {
    for (const char* w : {"submit",       "send",    "post",      "publish",       "buy",
                          "purchase",     "order",   "checkout",  "pay",           "place order",
                          "confirm",      "delete",  "remove",    "unsubscribe",   "transfer",
                          "book",         "reserve", "sign up",   "subscribe",     "отправить",
                          "опубликовать", "купить",  "оплатить",  "заказать",      "оформить",
                          "подтвердить",  "удалить", "перевести", "забронировать", "подписаться"}) {
        EXPECT_TRUE(IsConsequentialText(w)) << w;
    }
    EXPECT_FALSE(IsConsequentialText("Payload viewer"));  // whole words only
    EXPECT_FALSE(IsConsequentialText("Bookmarks"));
    EXPECT_TRUE(IsConsequentialText("  BUY NOW "));
}

TEST(ActionGateTest, EnterInAFormAsks) {
    GateInput in = Call("page_press_key", Input("text"));
    in.key = "Enter";
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kEnterInForm);
    in.key = "Tab";
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
}

TEST(ActionGateTest, ScriptsAskUnlessTrusted) {
    EXPECT_EQ(Classify(Call("page_evaluate"), kNone).rule, GateRule::kScript);
    EXPECT_EQ(Classify(Call("page_evaluate"), kTrusted).action, ActionClass::kFree);
}

TEST(ActionGateTest, ForeignNavigationAsksOnlyInPersonTabs) {
    GateInput in = Call("browser_navigate");
    in.target_url = "https://bank.other.org/";
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kForeignNavigation);
    in.tab_opened_by_agent = true;
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
    in.tab_opened_by_agent = false;
    in.target_url = "https://shop.example.com/other";
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);  // same origin
}

TEST(ActionGateTest, ClosingAPersonTabAsks) {
    GateInput in = Call("browser_close_tab");
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kClosePersonTab);
    in.tab_opened_by_agent = true;
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
}

TEST(ActionGateTest, MissingFactsAsk) {
    EXPECT_EQ(Classify(Call("page_click"), kNone).action, ActionClass::kConfirm);
    GateInput in = Call("page_press_key");
    in.key = "Enter";
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kConfirm);
}

TEST(ActionGateTest, LookAlikeHostsDoNotMatch) {
    EXPECT_TRUE(HostMatchesTrusted("example.com", kTrusted));
    EXPECT_TRUE(HostMatchesTrusted("shop.example.com", kTrusted));
    EXPECT_FALSE(HostMatchesTrusted("example.com.evil.net", kTrusted));
    EXPECT_FALSE(HostMatchesTrusted("evil-example.com", kTrusted));
    EXPECT_EQ(HostOf("https://WWW.Example.com.:8443/x"), "example.com");
    EXPECT_EQ(HostOf("not a url"), "");
}

}  // namespace
}  // namespace island::agent
