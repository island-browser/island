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

TEST(ActionGateTest, BookmarkMutationsAlwaysAskEvenForTrustedSites) {
    GateInput input = Call("browser_add_favorite");
    input.bookmark_title = "Island";
    input.bookmark_url = "https://shop.example.com/";
    input.favorite_scope = "space 12 favorites";
    GateDecision decision = Classify(input, kTrusted);
    EXPECT_EQ(decision.action, ActionClass::kConfirm);
    EXPECT_EQ(decision.rule, GateRule::kBookmarkMutation);
    EXPECT_FALSE(decision.trusted_allowed);
    EXPECT_TRUE(decision.task_grant_allowed);
    EXPECT_EQ(decision.summary, "Add \"Island\" on shop.example.com to space 12 favorites");
}

TEST(ActionGateTest, PasswordFieldsAreBlockedEvenWhenTrusted) {
    for (auto facts : {Input("password"), Input("text", "current-password"),
                       Input("text", "new-password"), Input("text", "one-time-code")}) {
        for (const auto* trusted : {&kNone, &kTrusted}) {
            GateDecision d = Classify(Call("page_type", facts), *trusted);
            EXPECT_EQ(d.action, ActionClass::kBlocked);
            EXPECT_EQ(d.rule, GateRule::kPassword);
            EXPECT_FALSE(d.trusted_allowed);
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

// --- Fix round 1 -------------------------------------------------------------------------------

// I1: the target of browser_navigate is read the way a browser reads it, so a backslash cannot
// hide the real host behind a fake one.
TEST(ActionGateTest, BackslashInTargetDoesNotHideForeignHost) {
    GateInput in = Call("browser_navigate");
    in.target_url = "https://evil.net\\@shop.example.com/";  // loads evil.net
    GateDecision d = Classify(in, kNone);
    EXPECT_EQ(d.action, ActionClass::kConfirm);
    EXPECT_EQ(d.rule, GateRule::kForeignNavigation);
    EXPECT_EQ(d.summary, "Open evil.net in your tab");
    in.target_url = "https://evil.net/?u=https://shop.example.com/";
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kForeignNavigation);
    in.target_url = "https://shop.example.com\\@evil.net/";  // loads shop.example.com
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
}

TEST(ActionGateTest, HostOfReadsTheAuthorityLikeABrowser) {
    EXPECT_EQ(HostOf("https://evil.net\\@shop.example.com/"), "evil.net");
    EXPECT_EQ(HostOf("https://shop.example.com\\@evil.net/"), "shop.example.com");
    EXPECT_EQ(HostOf("https://shop.example.com\t.evil.net/"), "shop.example.com.evil.net");
    EXPECT_EQ(HostOf("https://evil.net/?u=https://shop.example.com/"), "evil.net");
    EXPECT_EQ(HostOf("evil.net/?u=https://shop.example.com/"), "");
    EXPECT_EQ(HostOf("https://shop.example.com:443@evil.net/"), "evil.net");
}

// I1 (empty hosts): an unknown page or target never counts as the same site.
TEST(ActionGateTest, EmptyHostsCountAsForeignNavigation) {
    GateInput in = Call("browser_navigate");
    in.page_url = "about:blank";
    in.target_url = "evil.net";  // no scheme: the tool layer resolves it to https://evil.net
    GateDecision d = Classify(in, kNone);
    EXPECT_EQ(d.action, ActionClass::kConfirm);
    EXPECT_EQ(d.rule, GateRule::kForeignNavigation);
    in.target_url = "https://shop.example.com/";
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kConfirm);
}

// I2: `cc-num` is a card field name, as §2.1 lists it.
TEST(ActionGateTest, CcNumNameIsACardField) {
    GateDecision d = Classify(Call("page_type", Input("text", {}, "cc-num")), kNone);
    EXPECT_EQ(d.action, ActionClass::kConfirm);
    EXPECT_EQ(d.rule, GateRule::kCardField);
    EXPECT_FALSE(d.trusted_allowed);
    ElementFacts by_id = Input("text");
    by_id.id = "CC-NUM";
    EXPECT_EQ(Classify(Call("page_type", by_id), kTrusted).rule, GateRule::kCardField);
}

// I3: rules 1 and 2 apply only to the tools §2.1 names.
TEST(ActionGateTest, PasswordAndCardRulesApplyOnlyToTyping) {
    EXPECT_EQ(Classify(Call("page_click", Input("password")), kNone).action, ActionClass::kFree);
    ElementFacts card = Input("text", "cc-number");
    EXPECT_EQ(Classify(Call("page_click", card), kNone).action, ActionClass::kFree);
}

TEST(ActionGateTest, ClickOnProductCardIsFree) {
    ElementFacts card;
    card.tag = "div";
    card.id = "product-card";
    card.text = "Open";
    card.page_origin = "https://shop.example.com";
    EXPECT_EQ(Classify(Call("page_click", card), kTrusted).action, ActionClass::kFree);
    EXPECT_EQ(Classify(Call("page_click", card), kNone).action, ActionClass::kFree);
}

// I4: typing or pressing a key without element facts asks, on every site.
TEST(ActionGateTest, UnclassifiableTypingAndKeysAskEvenWhenTrusted) {
    GateDecision typed = Classify(Call("page_type"), kTrusted);
    EXPECT_EQ(typed.action, ActionClass::kConfirm);
    EXPECT_EQ(typed.rule, GateRule::kUnclassified);
    EXPECT_FALSE(typed.trusted_allowed);
    GateInput key = Call("page_press_key");
    key.key = "a";
    EXPECT_EQ(Classify(key, kTrusted).action, ActionClass::kConfirm);
    EXPECT_EQ(Classify(key, kTrusted).rule, GateRule::kUnclassified);
}

// I5: non-ASCII spaces and punctuation end a word; Cyrillic letters do not.
TEST(ActionGateTest, PunctuationAndSpacesEndWords) {
    EXPECT_TRUE(IsConsequentialText("Buy\xC2\xA0now"));      // NBSP
    EXPECT_TRUE(IsConsequentialText("Sign\xC2\xA0up"));      // NBSP
    EXPECT_TRUE(IsConsequentialText("Delete\xE2\x80\xA6"));  // U+2026
    EXPECT_TRUE(
        IsConsequentialText("\xC2\xAB\xD0\x9A\xD1\x83\xD0\xBF\xD0\xB8\xD1\x82\xD1\x8C"
                            "\xC2\xBB"));                    // «Купить»
    EXPECT_TRUE(IsConsequentialText("Pay\xE2\x80\x94now"));  // U+2014
    EXPECT_FALSE(IsConsequentialText("Покупить"));  // "купить" inside a longer Cyrillic word
    EXPECT_FALSE(IsConsequentialText("Payé"));      // a Latin letter after the word
}

// M1: rule 6 compares origins, not hosts.
TEST(ActionGateTest, NavigationComparesOrigins) {
    GateInput in = Call("browser_navigate");
    in.target_url = "http://shop.example.com:8080/";
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kForeignNavigation);
    in.target_url = "https://shop.example.com:443/x";  // explicit default port: same origin
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
    in.target_url = "http://shop.example.com/";
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kForeignNavigation);
}

// M2: a summary is one line, and an element without text is named by its role.
TEST(ActionGateTest, SummaryIsOneLine) {
    GateDecision d = Classify(Call("page_click", Button("Place\norder\n\nnow")), kNone);
    EXPECT_EQ(d.summary, "Click \"Place order now\" on shop.example.com");
    ElementFacts blank = Button("", true);
    EXPECT_EQ(Classify(Call("page_click", blank), kNone).summary,
              "Click an element on shop.example.com");
}

// M3: an IPv6 host keeps its brackets and loses its port.
TEST(ActionGateTest, IPv6HostDropsThePort) {
    EXPECT_EQ(HostOf("https://[::1]:8080/"), "[::1]");
    EXPECT_EQ(HostOf("https://[::1]/"), "[::1]");
}

// M6: the trusted variants of rules 3-7 and the untrusted variant of rule 2.
TEST(ActionGateTest, TrustedSiteFreesEveryConsequentialRule) {
    EXPECT_EQ(Classify(Call("page_click", Button("Go", true, true)), kTrusted).action,
              ActionClass::kFree);
    GateInput enter = Call("page_press_key", Input("text"));
    enter.key = "Enter";
    EXPECT_EQ(Classify(enter, kTrusted).action, ActionClass::kFree);
    GateInput nav = Call("browser_navigate");
    nav.target_url = "https://bank.other.org/";
    EXPECT_EQ(Classify(nav, kTrusted).action, ActionClass::kFree);
    EXPECT_EQ(Classify(Call("browser_close_tab"), kTrusted).action, ActionClass::kFree);
}

TEST(ActionGateTest, CardRuleAsksOnUntrustedSitesToo) {
    EXPECT_EQ(Classify(Call("page_type", Input("text", "cc-number")), kNone).rule,
              GateRule::kCardField);
}

// Fix round 2 (N1): page_type with submit presses Enter, so it is judged like page_press_key Enter.
TEST(ActionGateTest, TypeWithSubmitInAFormAsksLikeEnter) {
    GateInput in = Call("page_type", Input("text"));
    in.submit = true;
    GateDecision decision = Classify(in, kNone);
    EXPECT_EQ(decision.action, ActionClass::kConfirm);
    EXPECT_EQ(decision.rule, GateRule::kEnterInForm);
    EXPECT_EQ(decision.summary, Classify(in, kNone).summary);
    GateInput enter = Call("page_press_key", Input("text"));
    enter.key = "Enter";
    EXPECT_EQ(decision.summary, Classify(enter, kNone).summary);
    // Outside a form, Enter is free, and so is typing with submit.
    ElementFacts loose = Input("text");
    loose.in_form = false;
    in.facts = loose;
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
}

TEST(ActionGateTest, TypeWithSubmitOnTrustedSiteIsFree) {
    GateInput in = Call("page_type", Input("text"));
    in.submit = true;
    EXPECT_EQ(Classify(in, kTrusted).action, ActionClass::kFree);
    in.submit = false;
    EXPECT_EQ(Classify(in, kTrusted).action, ActionClass::kFree);
}

TEST(ActionGateTest, TypeWithSubmitIntoPasswordIsStillBlocked) {
    GateInput in = Call("page_type", Input("password"));
    in.submit = true;
    EXPECT_EQ(Classify(in, kTrusted).action, ActionClass::kBlocked);
    EXPECT_EQ(Classify(in, kTrusted).rule, GateRule::kPassword);
    EXPECT_FALSE(Classify(in, kTrusted).trusted_allowed);
}

// M8: a password field never offers "Trust this site".
TEST(ActionGateTest, PasswordBlockOffersNoTrust) {
    EXPECT_FALSE(Classify(Call("page_type", Input("password")), kTrusted).trusted_allowed);
}

}  // namespace
}  // namespace island::agent
