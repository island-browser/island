#include "browser_tools.h"

#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <utility>

namespace island::agent {

namespace {

using json::Value;

// ---- schema helpers ----------------------------------------------------------

Value Prop(std::string_view type, std::string_view description) {
    return Value::MakeObject()
        .Set("type", Value::String(std::string(type)))
        .Set("description", Value::String(std::string(description)));
}

Value EnumProp(std::string_view description, std::initializer_list<std::string_view> values) {
    Value list = Value::MakeArray();
    for (std::string_view v : values) list.Push(Value::String(std::string(v)));
    return Prop("string", description).Set("enum", std::move(list));
}

Value TabProp() {
    return Prop("integer",
                "Tab index in the active space (from browser_list_tabs). Defaults to the "
                "active tab.")
        .Set("minimum", Value::Int(0));
}

Value Schema(Value properties, std::initializer_list<std::string_view> required = {}) {
    Value schema = Value::MakeObject()
                       .Set("type", Value::String("object"))
                       .Set("properties", std::move(properties));
    if (required.size() > 0) {
        Value list = Value::MakeArray();
        for (std::string_view r : required) list.Push(Value::String(std::string(r)));
        schema.Set("required", std::move(list));
    }
    schema.Set("additionalProperties", Value::Bool(false));
    return schema;
}

Value Props() { return Value::MakeObject(); }

// ---- argument helpers --------------------------------------------------------

struct TabArg {
    bool valid = true;
    std::optional<std::size_t> tab;
};

TabArg ReadTabArg(const Value& args, std::string_view key = "tab") {
    const Value* v = args.FindMember(key);
    if (v == nullptr || v->IsNull()) return {};
    const std::int64_t index = args.IntOr(key, -1);
    if (index < 0) return {.valid = false};
    return {.valid = true, .tab = static_cast<std::size_t>(index)};
}

std::optional<std::size_t> ReadIndex(const Value& args, std::string_view key) {
    const std::int64_t index = args.IntOr(key, -1);
    if (index < 0) return std::nullopt;
    return static_cast<std::size_t>(index);
}

ToolResult StatusResult(const HostStatus& status, std::string_view success) {
    if (!status.ok) return ToolResult::Error(status.error);
    return ToolResult::Text(std::string(success));
}

Value TabsJson(const std::vector<AgentTabInfo>& tabs) {
    Value list = Value::MakeArray();
    for (const AgentTabInfo& tab : tabs) {
        Value item = Value::MakeObject()
                         .Set("index", Value::Int(static_cast<std::int64_t>(tab.index)))
                         .Set("title", Value::String(tab.title))
                         .Set("url", Value::String(tab.url))
                         .Set("active", Value::Bool(tab.active))
                         .Set("loading", Value::Bool(tab.loading));
        if (tab.pinned) item.Set("pinned", Value::Bool(true));
        list.Push(std::move(item));
    }
    return list;
}

std::string HexColor(std::uint32_t argb) {
    static constexpr std::array<char, 16> kDigits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                     '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string out = "#";
    for (int shift = 20; shift >= 0; shift -= 4) {
        out += kDigits[(argb >> static_cast<unsigned>(shift)) & 0xFU];
    }
    return out;
}

// ---- page scripts --------------------------------------------------------------

// Every script returns a JSON string so the value crosses the DevTools
// boundary intact (returnByValue) and parses with the same JSON code.

std::string ReadPageScript(std::size_t max_chars) {
    return "(() => { const max = " + std::to_string(max_chars) +
           "; const body = document.body ? document.body.innerText : '';"
           " return JSON.stringify({title: document.title, url: location.href,"
           " length: body.length, truncated: body.length > max, text: body.slice(0, max)}); })()";
}

std::string SnapshotScript(std::size_t limit) {
    return "(() => { const limit = " + std::to_string(limit) +
           R"JS(;
  const selector = 'a[href],button,input:not([type=hidden]),textarea,select,summary,' +
    '[role=button],[role=link],[role=checkbox],[role=radio],[role=tab],[role=menuitem],' +
    '[role=option],[role=switch],[role=textbox],[role=combobox],[role=searchbox],' +
    '[contenteditable=""],[contenteditable=true],[onclick]';
  for (const old of document.querySelectorAll('[data-island-ref]')) old.removeAttribute('data-island-ref');
  const elements = [];
  let ref = 0;
  let total = 0;
  for (const el of document.querySelectorAll(selector)) {
    const rect = el.getBoundingClientRect();
    const style = getComputedStyle(el);
    if (rect.width === 0 || rect.height === 0 || style.visibility === 'hidden' || style.display === 'none') continue;
    total += 1;
    if (elements.length >= limit) continue;
    ref += 1;
    el.setAttribute('data-island-ref', String(ref));
    const secret = el.type === 'password';
    const raw = el.getAttribute('aria-label') || el.innerText || (secret ? '' : el.value) ||
      el.placeholder || el.title || el.alt || '';
    const label = String(raw).trim().replace(/\s+/g, ' ').slice(0, 120);
    const item = {ref, tag: el.tagName.toLowerCase()};
    const role = el.getAttribute('role');
    if (role) item.role = role;
    if (el.tagName === 'INPUT') item.type = el.type;
    if (label) item.label = label;
    if (el.href) item.href = String(el.href).slice(0, 300);
    if (el.name) item.name = el.name;
    if (el.disabled) item.disabled = true;
    if (el.checked) item.checked = true;
    item.in_viewport = rect.bottom > 0 && rect.top < innerHeight && rect.right > 0 && rect.left < innerWidth;
    elements.push(item);
  }
  return JSON.stringify({title: document.title, url: location.href, total, elements});
})())JS";
}

// Defines the gate's page helpers. __islandActive() finds the element that really has keyboard
// focus: it descends through open shadow roots and same-origin frames, and reports {opaque: true}
// when focus sits behind a cross-origin frame or a possibly closed shadow root, where the element
// cannot be seen. __islandFacts(el) describes an element for the gate; the field values of password
// inputs are never read. Facts come from the page's own main world, so a hostile page can describe
// itself as it likes; the gate relies on them only to classify, never to grant more than it asks.
constexpr std::string_view kFactsJs = R"JS(
  const __islandActive = () => {
    let el = document.activeElement || document.body || document.documentElement;
    for (let depth = 0; el && depth < 16; depth++) {
      const tag = el.tagName.toLowerCase();
      if (tag === 'iframe' || tag === 'frame') {
        const doc = el.contentDocument;
        if (!doc) return {el, opaque: true};
        const inner = doc.activeElement || doc.body || doc.documentElement;
        if (!inner) return {el, opaque: true};
        el = inner;
        continue;
      }
      if (el.shadowRoot) {
        if (!el.shadowRoot.activeElement) return {el, opaque: false};
        el = el.shadowRoot.activeElement;
        continue;
      }
      if (tag.indexOf('-') !== -1) return {el, opaque: true};
      return {el, opaque: false};
    }
    return {el, opaque: true};
  };
  const __islandWithin = (outer, inner) => {
    for (let n = inner, i = 0; n && i < 64; i++) {
      if (n === outer) return true;
      n = n.parentNode || n.host || (n.defaultView && n.defaultView.frameElement);
    }
    return false;
  };
  const __islandFacts = (el) => {
    const tag = el.tagName.toLowerCase();
    const type = String(el.type || '').toLowerCase();
    const clip = (v) => String(v || '').replace(/\s+/g, ' ').trim().slice(0, 80);
    const labelled = tag === 'input' && ['submit', 'button', 'reset', 'image'].includes(type);
    const submitter = (tag === 'button' && (type === 'submit' || type === '')) ||
      (tag === 'input' && (type === 'submit' || type === 'image'));
    return {tag, type, autocomplete: clip(el.getAttribute('autocomplete')), name: clip(el.name),
      id: clip(el.id), aria_label: clip(el.getAttribute('aria-label')),
      text: clip(labelled ? el.value : el.innerText),
      in_form: !!el.form || !!(el.closest && el.closest('form')),
      is_submit: submitter && !!el.form, page_origin: location.origin};
  };
  const __islandActiveFacts = () => {
    const active = __islandActive();
    return active.opaque || !active.el ? null : __islandFacts(active.el);
  };)JS";

std::string ClickTargetScript(const std::string& locator) {
    return "(() => { const el = " + locator + ";" + std::string(kFactsJs) +
           R"JS(
  if (!el) return JSON.stringify({ok: false, error: 'element not found; call page_snapshot for fresh refs'});
  el.scrollIntoView({block: 'center', inline: 'center'});
  const rect = el.getBoundingClientRect();
  const facts = __islandFacts(el);
  if (rect.width === 0 || rect.height === 0) return JSON.stringify({ok: true, synthetic: true, facts});
  return JSON.stringify({ok: true, x: Math.round(rect.left + rect.width / 2), y: Math.round(rect.top + rect.height / 2), facts});
})())JS";
}

// Currency marks for the live-label rule (NormalizeLiveCounters): a label that carries one is
// compared exactly. A mark of ASCII letters only is a whole token, any case ("SEK", "kr"); any
// other mark is a substring ("$", "руб", "р."). The page copy below reads this same table, so
// both sides agree on every entry (facts_probe.cjs runs the pairs in browser_tools_test.cpp).
constexpr std::string_view kCurrencyMarks[] = {
    "$",   "€",    "£",    "¥",   "¢",   "₽",   "₴",   "₸",   "₹",   "₩",   "₺",   "₪",   "₫",
    "₱",   "₦",    "₿",    "₡",   "₵",   "₼",   "₾",   "＄",  "￥",  "￡",  "￠",  "元",  "円",
    "zł",  "Zł",   "ZŁ",   "Kč",  "KČ",  "kč",  "руб", "Руб", "РУБ", "р.",  "Р.",  "лв",  "Лв",
    "ЛВ",  "грн",  "Грн",  "ГРН", "￦",  "﹩",  "﷼",   "₨",   "USD", "EUR", "GBP", "JPY", "RUB",
    "CNY", "RMB",  "CHF",  "CAD", "AUD", "NZD", "HKD", "SGD", "SEK", "NOK", "DKK", "ISK", "PLN",
    "CZK", "HUF",  "RON",  "UAH", "ZAR", "ILS", "Rs",  "THB", "INR", "KRW", "BRL", "MXN", "BTC",
    "ETH", "USDT", "USDC", "kr",  "kn",  "Ft",  "lei", "Rp",  "RM",
};

// The live-label rule in page JavaScript, for SyntheticClickScript. `liveEqual` mirrors
// LiveLabelsMatch. `liveCounters` mirrors NormalizeLiveCounters for a label that is not unsafe; it
// does not check the unsafe marks itself, because the page only calls liveEqual. The table is
// `__islandCurrencyMarks`, built from kCurrencyMarks by SyntheticClickScript.
constexpr std::string_view kLiveCountersJs = R"JS(
  const liveTokens = __islandCurrencyMarks.filter((m) => /^[A-Za-z]+$/.test(m));
  const liveSymbols = __islandCurrencyMarks.filter((m) => !/^[A-Za-z]+$/.test(m));
  const liveTokenRe = new RegExp('(^|[^A-Za-z])(' + liveTokens.join('|') + ')(?![A-Za-z])', 'i');
  const liveUnsafe = (s) => s.indexOf('#') !== -1 || liveSymbols.some((m) => s.indexOf(m) !== -1) ||
    liveTokenRe.test(s);
  const liveCounters = (text) => {
    let out = String(text);
    // Only an ASCII prefix without a digit counts, as in C++ (a non-ASCII digit fails closed).
    const counter = /^([\x00-\x2f\x3a-\x7f]*)\([0-9]+\)([ \t]*)$/.exec(out);
    if (counter) out = counter[1] + '(#)' + counter[2];
    // The time run may not touch a non-ASCII byte, before it or after it (NonAsciiFollows).
    return out.replace(/(^|[\x00-\x2f\x3a-\x7f])[0-9]{1,2}:[0-5][0-9](:[0-5][0-9])?(?![0-9\x80-￿])(?!:[0-9]*[\x80-￿])/g, (m, pre) => pre + '#');
  };
  const liveEqual = (a, b) => a === b || (!liveUnsafe(String(a)) && !liveUnsafe(String(b)) &&
    liveCounters(a) === liveCounters(b));
)JS";

// Clicks a box-less element with element.click(). The element is found again by its locator, so
// the script first checks that it is still the element the gate judged (`expected` is that
// element's facts as a JSON object) and still has no box.
std::string SyntheticClickScript(const std::string& locator, const std::string& expected) {
    std::string marks = "const __islandCurrencyMarks = [";
    for (std::string_view mark : kCurrencyMarks) {
        marks += JsStringLiteral(std::string(mark)) + ",";
    }
    marks += "];";
    return "(() => { const el = " + locator + "; const expected = " + expected + ";" + marks +
           std::string(kLiveCountersJs) + std::string(kFactsJs) + R"JS(
  if (!el) return JSON.stringify({ok: false, error: 'element not found; call page_snapshot for fresh refs'});
  const rect = el.getBoundingClientRect();
  const facts = __islandFacts(el);
  const liveKeys = ['text', 'aria_label'];
  const same = rect.width === 0 && rect.height === 0 &&
    Object.keys(expected).every((key) => liveKeys.includes(key)
      ? liveEqual(facts[key], expected[key])
      : facts[key] === expected[key]);
  if (!same) return JSON.stringify({ok: false, error: 'The page changed while waiting; take a fresh snapshot.'});
  el.click();
  return JSON.stringify({ok: true});
})())JS";
}

std::string FocusScript(const std::string& locator, bool clear) {
    return "(() => { const el = " + locator + "; const clear = " + (clear ? "true" : "false") +
           ";" + std::string(kFactsJs) + R"JS(
  if (!el) return JSON.stringify({ok: false, error: 'element not found; call page_snapshot for fresh refs'});
  el.scrollIntoView({block: 'center'});
  el.focus();
  if (clear) {
    if (typeof el.select === 'function') el.select();
    else if (el.isContentEditable) document.execCommand('selectAll');
  }
  const active = __islandActive();
  return JSON.stringify({ok: !!active.el && __islandWithin(el, active.el),
    facts: active.opaque || !active.el ? null : __islandFacts(active.el)});
})())JS";
}

// The focused element's facts, for page_type without a locator and page_press_key.
std::string ActiveElementScript() {
    return "(() => {" + std::string(kFactsJs) +
           R"JS(
  return JSON.stringify({ok: true, facts: __islandActiveFacts()});
})())JS";
}

std::string ScrollScript(std::string_view direction, std::int64_t amount) {
    std::string action;
    if (direction == "top") {
        action = "window.scrollTo(0, 0);";
    } else if (direction == "bottom") {
        action = "window.scrollTo(0, document.documentElement.scrollHeight);";
    } else {
        const std::string step =
            amount > 0 ? std::to_string(amount) : "Math.round(innerHeight * 0.8)";
        action = std::string("window.scrollBy(0, ") + (direction == "up" ? "-" : "") + step + ");";
    }
    return "(() => { " + action +
           " return JSON.stringify({scroll_y: Math.round(scrollY), viewport_height: innerHeight,"
           " page_height: document.documentElement.scrollHeight}); })()";
}

struct KeySpec {
    std::string_view key;
    std::string_view code;
    int key_code;
    std::string_view text;
};

constexpr std::array<KeySpec, 14> kKeys = {{
    {"Enter", "Enter", 13, "\r"},
    {"Tab", "Tab", 9, ""},
    {"Escape", "Escape", 27, ""},
    {"Backspace", "Backspace", 8, ""},
    {"Delete", "Delete", 46, ""},
    {"ArrowUp", "ArrowUp", 38, ""},
    {"ArrowDown", "ArrowDown", 40, ""},
    {"ArrowLeft", "ArrowLeft", 37, ""},
    {"ArrowRight", "ArrowRight", 39, ""},
    {"PageUp", "PageUp", 33, ""},
    {"PageDown", "PageDown", 34, ""},
    {"Home", "Home", 36, ""},
    {"End", "End", 35, ""},
    {"Space", "Space", 32, " "},
}};

const KeySpec* FindKey(std::string_view name) {
    for (const KeySpec& spec : kKeys) {
        if (spec.key == name) return &spec;
    }
    return nullptr;
}

Value KeyEventParams(const KeySpec& spec, bool down) {
    Value params = Value::MakeObject()
                       .Set("type", Value::String(down ? "keyDown" : "keyUp"))
                       .Set("key", Value::String(spec.key == "Space" ? " " : std::string(spec.key)))
                       .Set("code", Value::String(std::string(spec.code)))
                       .Set("windowsVirtualKeyCode", Value::Int(spec.key_code))
                       .Set("nativeVirtualKeyCode", Value::Int(spec.key_code));
    if (down && !spec.text.empty()) params.Set("text", Value::String(std::string(spec.text)));
    return params;
}

// Sends keyDown then keyUp for one key and reports the combined outcome.
void SendKey(AgentBrowserHost& host, std::optional<std::size_t> tab, const KeySpec& spec,
             std::function<void(bool ok, std::string error)> done) {
    host.DevToolsCall(
        tab, "Input.dispatchKeyEvent", KeyEventParams(spec, true),
        [&host, tab, spec, done = std::move(done)](DevToolsReply down) mutable {
            if (!down.ok) {
                done(false, down.error);
                return;
            }
            host.DevToolsCall(
                tab, "Input.dispatchKeyEvent", KeyEventParams(spec, false),
                [done = std::move(done)](DevToolsReply up) { done(up.ok, up.error); });
        });
}

Value MouseParams(std::string_view type, double x, double y) {
    Value params = Value::MakeObject()
                       .Set("type", Value::String(std::string(type)))
                       .Set("x", Value::Double(x))
                       .Set("y", Value::Double(y));
    if (type != "mouseMoved") {
        params.Set("button", Value::String("left")).Set("clickCount", Value::Int(1));
    }
    return params;
}

// Parses the JSON string a page script returned.
std::optional<Value> ParseScriptJson(const Value& value) {
    if (!value.IsString()) return std::nullopt;
    std::optional<Value> parsed = json::Parse(value.string_val);
    if (!parsed || !parsed->IsObject()) return std::nullopt;
    return parsed;
}

// The element facts a script reported, or nullopt when it reported none.
std::optional<ElementFacts> ParseFacts(const Value& script_result) {
    const Value* facts = script_result.FindMember("facts");
    if (facts == nullptr || !facts->IsObject()) return std::nullopt;
    ElementFacts out;
    out.tag = std::string(facts->StringOr("tag", ""));
    out.type = std::string(facts->StringOr("type", ""));
    out.autocomplete = std::string(facts->StringOr("autocomplete", ""));
    out.name = std::string(facts->StringOr("name", ""));
    out.id = std::string(facts->StringOr("id", ""));
    out.aria_label = std::string(facts->StringOr("aria_label", ""));
    out.text = std::string(facts->StringOr("text", ""));
    out.in_form = facts->BoolOr("in_form", false);
    out.is_submit = facts->BoolOr("is_submit", false);
    out.page_origin = std::string(facts->StringOr("page_origin", ""));
    return out;
}

// True when both describe the same element for the gate's purposes. Two missing facts (focus in
// an opaque frame) are the same; one missing and one present are not.
bool SameElement(const std::optional<ElementFacts>& a, const std::optional<ElementFacts>& b) {
    if (!a || !b) return !a && !b;
    return a->tag == b->tag && a->type == b->type && LiveLabelsMatch(a->text, b->text) &&
           a->autocomplete == b->autocomplete && a->name == b->name && a->id == b->id &&
           LiveLabelsMatch(a->aria_label, b->aria_label) && a->is_submit == b->is_submit &&
           a->page_origin == b->page_origin;
}

const char kPageChanged[] = "The page changed while waiting; take a fresh snapshot.";

// The facts as the JSON object SyntheticClickScript compares against.
std::string FactsLiteral(const ElementFacts& facts) {
    return json::Serialize(Value::MakeObject()
                               .Set("tag", Value::String(facts.tag))
                               .Set("type", Value::String(facts.type))
                               .Set("autocomplete", Value::String(facts.autocomplete))
                               .Set("name", Value::String(facts.name))
                               .Set("id", Value::String(facts.id))
                               .Set("aria_label", Value::String(facts.aria_label))
                               .Set("text", Value::String(facts.text))
                               .Set("in_form", Value::Bool(facts.in_form))
                               .Set("is_submit", Value::Bool(facts.is_submit)));
}

// A real left click at (x, y): move, press, release.
void SendClick(AgentBrowserHost& host, std::optional<std::size_t> tab, double x, double y,
               ToolCallback done) {
    auto shared_done = std::make_shared<ToolCallback>(std::move(done));
    host.DevToolsCall(
        tab, "Input.dispatchMouseEvent", MouseParams("mouseMoved", x, y),
        [&host, tab, x, y, shared_done](DevToolsReply moved) {
            if (!moved.ok) {
                (*shared_done)(ToolResult::Error("page_click failed: " + moved.error));
                return;
            }
            host.DevToolsCall(
                tab, "Input.dispatchMouseEvent", MouseParams("mousePressed", x, y),
                [&host, tab, x, y, shared_done](DevToolsReply pressed) {
                    if (!pressed.ok) {
                        (*shared_done)(ToolResult::Error("page_click failed: " + pressed.error));
                        return;
                    }
                    host.DevToolsCall(
                        tab, "Input.dispatchMouseEvent", MouseParams("mouseReleased", x, y),
                        [shared_done](DevToolsReply released) {
                            if (!released.ok) {
                                (*shared_done)(
                                    ToolResult::Error("page_click failed: " + released.error));
                                return;
                            }
                            (*shared_done)(ToolResult::Text("Clicked."));
                        });
                });
        });
}

}  // namespace

namespace {

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// A non-ASCII byte. A page may draw it as a digit ("５" is U+FF15), so it is never a boundary.
bool IsNonAscii(char c) { return static_cast<unsigned char>(c) >= 0x80; }

bool IsDigitLike(char c) { return IsDigit(c) || IsNonAscii(c); }

// Whether a non-ASCII byte continues the number that ends at `end`: directly, or after its ':' and
// the digits that follow it ("0:42５", "0:42:5５"). Plain ASCII after the run is left alone.
bool NonAsciiFollows(std::string_view text, std::size_t end) {
    if (end < text.size() && text[end] == ':') {
        ++end;
        while (end < text.size() && IsDigit(text[end])) ++end;
    }
    return end < text.size() && IsNonAscii(text[end]);
}

bool IsAsciiLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

char AsciiLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool IsTokenMark(std::string_view mark) {
    return !mark.empty() &&
           std::all_of(mark.begin(), mark.end(), [](char c) { return IsAsciiLetter(c); });
}

// A label the rule must not touch: it holds a currency mark, or a '#' (the placeholder's own
// character, so a literal '#' could otherwise collide with a normalized counter).
bool HasUnsafeMark(std::string_view text) {
    if (text.find('#') != std::string_view::npos) return true;
    for (std::string_view mark : kCurrencyMarks) {
        if (IsTokenMark(mark)) continue;
        if (text.find(mark) != std::string_view::npos) return true;
    }
    std::size_t i = 0;
    while (i < text.size()) {
        if (!IsAsciiLetter(text[i])) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && IsAsciiLetter(text[end])) ++end;
        for (std::string_view mark : kCurrencyMarks) {
            if (!IsTokenMark(mark) || mark.size() != end - i) continue;
            bool equal = true;
            for (std::size_t k = 0; k < mark.size(); ++k) {
                equal = equal && AsciiLower(text[i + k]) == AsciiLower(mark[k]);
            }
            if (equal) return true;
        }
        i = end;
    }
    return false;
}

// Replaces every time-like run (M:SS or M:SS:SS, minutes and seconds 00-59, one or two leading
// digits, whole digit runs only) with one '#'. A run that touches a non-ASCII byte is left as it
// is, so "５0:42" and "５9:42" never both become "５#".
std::string ReplaceTimeRuns(std::string_view text) {
    std::string out;
    std::size_t i = 0;
    const auto minutes_or_seconds_at = [&text](std::size_t at) {
        return at + 1 < text.size() && text[at] >= '0' && text[at] <= '5' &&
               IsDigit(text[at + 1]) && (at + 2 == text.size() || !IsDigitLike(text[at + 2]));
    };
    while (i < text.size()) {
        if (!IsDigit(text[i]) || (i > 0 && IsDigitLike(text[i - 1]))) {
            out.push_back(text[i++]);
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && IsDigit(text[j])) ++j;
        if (j - i <= 2 && j < text.size() && text[j] == ':' && minutes_or_seconds_at(j + 1)) {
            std::size_t end = j + 3;
            if (end < text.size() && text[end] == ':' && minutes_or_seconds_at(end + 1)) end += 3;
            if (!NonAsciiFollows(text, end)) {
                out.push_back('#');
                i = end;
                continue;
            }
        }
        out.append(text.substr(i, j - i));
        i = j;
    }
    return out;
}

// Replaces one trailing "(digits)" (trailing blanks allowed) with "(#)", but only when the text
// before it is pure ASCII with no digit at all (a non-ASCII digit or letter before it fails
// closed).
std::string ReplaceTrailingCounter(std::string text) {
    std::size_t close = text.size();
    while (close > 0 && (text[close - 1] == ' ' || text[close - 1] == '\t')) --close;
    if (close == 0 || text[close - 1] != ')') return text;
    std::size_t p = close - 1;
    while (p > 0 && IsDigit(text[p - 1])) --p;
    if (p == close - 1 || p == 0 || text[p - 1] != '(') return text;
    for (std::size_t k = 0; k < p; ++k) {
        const auto byte = static_cast<unsigned char>(text[k]);
        if (IsDigit(text[k]) || byte >= 0x80) return text;
    }
    return text.substr(0, p) + "#" + text.substr(close - 1);
}

}  // namespace

std::string NormalizeLiveCounters(std::string_view text) {
    if (HasUnsafeMark(text)) return std::string(text);
    return ReplaceTimeRuns(ReplaceTrailingCounter(std::string(text)));
}

bool LiveLabelsMatch(std::string_view a, std::string_view b) {
    if (a == b) return true;
    if (HasUnsafeMark(a) || HasUnsafeMark(b)) return false;
    return NormalizeLiveCounters(a) == NormalizeLiveCounters(b);
}

// ---- ToolResult ------------------------------------------------------------------

ToolResult ToolResult::Text(std::string text) {
    ToolResult result;
    result.content.push_back({.type = ToolContent::Type::kText, .text = std::move(text)});
    return result;
}

ToolResult ToolResult::Json(const json::Value& value) { return Text(json::Serialize(value)); }

ToolResult ToolResult::Error(std::string message) {
    ToolResult result = Text(std::move(message));
    result.is_error = true;
    return result;
}

json::Value ToolResult::ToJson() const {
    Value blocks = Value::MakeArray();
    for (const ToolContent& block : content) {
        if (block.type == ToolContent::Type::kImage) {
            blocks.Push(Value::MakeObject()
                            .Set("type", Value::String("image"))
                            .Set("data", Value::String(block.data))
                            .Set("mimeType", Value::String(block.mime_type)));
        } else {
            blocks.Push(Value::MakeObject()
                            .Set("type", Value::String("text"))
                            .Set("text", Value::String(block.text)));
        }
    }
    return Value::MakeObject()
        .Set("content", std::move(blocks))
        .Set("isError", Value::Bool(is_error));
}

// ---- helpers exposed for tests ---------------------------------------------------

std::string JsStringLiteral(std::string_view text) {
    // JSON string syntax is valid JS; additionally escape the line separators
    // that JS (pre-ES2019) and HTML-embedded contexts treat as terminators, and
    // '<' so a "</script>" can never close an enclosing tag.
    std::string serialized = json::Serialize(Value::String(std::string(text)));
    std::string out;
    out.reserve(serialized.size());
    for (std::size_t i = 0; i < serialized.size(); ++i) {
        const char c = serialized[i];
        if (c == '<') {
            out += "\\u003c";
        } else if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < serialized.size() &&
                   static_cast<unsigned char>(serialized[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(serialized[i + 2]) == 0xA8 ||
                    static_cast<unsigned char>(serialized[i + 2]) == 0xA9)) {
            out += static_cast<unsigned char>(serialized[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

std::string ElementLocatorScript(const json::Value& args) {
    const std::int64_t ref = args.IntOr("ref", -1);
    if (ref > 0) {
        return "document.querySelector('[data-island-ref=\"" + std::to_string(ref) + "\"]')";
    }
    const std::string_view selector = args.StringOr("selector", "");
    if (!selector.empty()) {
        // A malformed selector throws inside querySelector; treat it as "not found".
        return "(() => { try { return document.querySelector(" + JsStringLiteral(selector) +
               "); } catch (e) { return null; } })()";
    }
    return {};
}

// ---- BrowserToolbox ---------------------------------------------------------------

BrowserToolbox::BrowserToolbox(AgentBrowserHost& host) : host_(host) {
    auto add = [this](std::string name, std::string title, std::string description, Value schema,
                      bool read_only) {
        definitions_.push_back({.name = std::move(name),
                                .title = std::move(title),
                                .description = std::move(description),
                                .input_schema = std::move(schema),
                                .read_only = read_only});
    };

    // Window, space, and tab management.
    add("browser_list_tabs", "List tabs",
        "List the tabs of the active space with index, title, URL, and loading state. 'active' "
        "marks the tab that calls without an index act on. During a research that is the agent's "
        "own page, which need not be the tab the person sees.",
        Schema(Props()), true);
    add("browser_list_spaces", "List spaces",
        "List the browser's spaces (Arc-style tab groups) with index, name, color, and tab "
        "count.",
        Schema(Props()), true);
    add("browser_open_tab", "Open tab",
        "Open a new tab in the active space and make it active. Accepts a URL or search text.",
        Schema(Props().Set("url", Prop("string", "URL or search text to load."))), false);
    add("browser_navigate", "Navigate",
        "Load a URL (or search text) in a tab. Use page_read or page_snapshot afterwards to see "
        "the result.",
        Schema(
            Props().Set("url", Prop("string", "URL or search text to load.")).Set("tab", TabProp()),
            {"url"}),
        false);
    add("browser_activate_tab", "Activate tab", "Make a tab of the active space the visible tab.",
        Schema(Props().Set("tab", TabProp()), {"tab"}), false);
    add("browser_pin_tab", "Pin tab",
        "Pin a tab to the top of the active space's sidebar list (or unpin it with "
        "pinned=false). Pinned tabs persist across restarts.",
        Schema(Props()
                   .Set("tab", TabProp())
                   .Set("pinned", Prop("boolean", "true to pin (default), false to unpin."))),
        false);
    add("browser_close_tab", "Close tab", "Close a tab of the active space.",
        Schema(Props().Set("tab", TabProp()), {"tab"}), false);
    add("browser_back", "Go back", "Go back in the active tab's history.", Schema(Props()), false);
    add("browser_forward", "Go forward", "Go forward in the active tab's history.", Schema(Props()),
        false);
    add("browser_reload", "Reload", "Reload the active tab.", Schema(Props()), false);
    add("browser_switch_space", "Switch space", "Switch the window to another space.",
        Schema(Props().Set("space", Prop("integer", "Space index from browser_list_spaces.")
                                        .Set("minimum", Value::Int(0))),
               {"space"}),
        false);
    add("browser_new_space", "New space", "Create a new space and switch to it.",
        Schema(Props().Set("name", Prop("string", "Optional name for the space."))), false);
    add("browser_toggle_split", "Toggle split view",
        "Show the active tab side by side with its neighbour, or leave split view.",
        Schema(Props()), false);
    add("browser_toggle_sidebar", "Toggle sidebar",
        "Expand the sidebar or collapse it to the icon rail.", Schema(Props()), false);

    // Page inspection and interaction.
    add("page_read", "Read page",
        "Return the title, URL, and visible text of a tab's page (clipped to max_chars).",
        Schema(Props()
                   .Set("tab", TabProp())
                   .Set("max_chars", Prop("integer",
                                          "Maximum characters of text to return "
                                          "(default 20000).")
                                         .Set("minimum", Value::Int(1)))),
        true);
    add("page_snapshot", "Snapshot interactive elements",
        "List visible links, buttons, inputs, and other interactive elements, each tagged with "
        "a numeric ref for page_click and page_type. Refs are invalidated by the next snapshot "
        "or navigation.",
        Schema(Props()
                   .Set("tab", TabProp())
                   .Set("limit", Prop("integer", "Maximum elements to return (default 150).")
                                     .Set("minimum", Value::Int(1)))),
        true);
    add("page_click", "Click element",
        "Click an element by snapshot ref or CSS selector with a real mouse event.",
        Schema(Props()
                   .Set("ref", Prop("integer", "Element ref from page_snapshot."))
                   .Set("selector", Prop("string", "CSS selector (used when ref is absent)."))
                   .Set("tab", TabProp())),
        false);
    add("page_type", "Type text",
        "Focus an element (by ref or selector) and type text into it with real keyboard input. "
        "Optionally clear it first and press Enter afterwards.",
        Schema(Props()
                   .Set("ref", Prop("integer", "Element ref from page_snapshot."))
                   .Set("selector", Prop("string", "CSS selector (used when ref is absent)."))
                   .Set("text", Prop("string", "Text to type."))
                   .Set("clear", Prop("boolean", "Replace the current value (default true)."))
                   .Set("submit", Prop("boolean", "Press Enter after typing (default false)."))
                   .Set("tab", TabProp()),
               {"text"}),
        false);
    add("page_press_key", "Press key", "Press one key in the focused element of a tab.",
        Schema(Props()
                   .Set("key", EnumProp("Key to press.",
                                        {"Enter", "Tab", "Escape", "Backspace", "Delete", "ArrowUp",
                                         "ArrowDown", "ArrowLeft", "ArrowRight", "PageUp",
                                         "PageDown", "Home", "End", "Space"}))
                   .Set("tab", TabProp()),
               {"key"}),
        false);
    add("page_scroll", "Scroll page", "Scroll a tab's page.",
        Schema(Props()
                   .Set("direction", EnumProp("Scroll direction.", {"down", "up", "top", "bottom"}))
                   .Set("amount", Prop("integer",
                                       "Pixels for up/down (default: 80% of the "
                                       "viewport)."))
                   .Set("tab", TabProp()),
               {"direction"}),
        false);
    add("page_evaluate", "Evaluate JavaScript",
        "Evaluate a JavaScript expression in a tab and return its JSON-serializable value. "
        "Promises are awaited.",
        Schema(Props()
                   .Set("expression", Prop("string", "JavaScript expression to evaluate."))
                   .Set("tab", TabProp()),
               {"expression"}),
        false);
    add("page_screenshot", "Screenshot", "Capture a PNG screenshot of a tab's visible viewport.",
        Schema(Props().Set("tab", TabProp())), true);
}

bool BrowserToolbox::HasTool(std::string_view name) const {
    for (const ToolDefinition& def : definitions_) {
        if (def.name == name) return true;
    }
    return false;
}

void BrowserToolbox::Call(std::string_view name, const json::Value& raw_arguments,
                          ToolCallback done) {
    const Value arguments = raw_arguments.IsObject() ? raw_arguments : Value::MakeObject();
    if (!HasTool(name)) {
        done(ToolResult::Error("Unknown tool: " + std::string(name)));
        return;
    }
    const TabArg tab_arg = ReadTabArg(arguments);
    if (!tab_arg.valid) {
        done(ToolResult::Error("'tab' must be a non-negative integer."));
        return;
    }

    if (name == "browser_list_tabs") {
        std::vector<AgentTabInfo> tabs = host_.ListTabs();
        std::string space_name;
        for (const AgentSpaceInfo& space : host_.ListSpaces()) {
            if (space.active) space_name = space.name;
        }
        done(ToolResult::Json(Value::MakeObject()
                                  .Set("space", Value::String(space_name))
                                  .Set("tabs", TabsJson(tabs))));
        return;
    }
    if (name == "browser_list_spaces") {
        Value list = Value::MakeArray();
        for (const AgentSpaceInfo& space : host_.ListSpaces()) {
            list.Push(Value::MakeObject()
                          .Set("index", Value::Int(static_cast<std::int64_t>(space.index)))
                          .Set("name", Value::String(space.name))
                          .Set("color", Value::String(HexColor(space.color_argb)))
                          .Set("tab_count", Value::Int(static_cast<std::int64_t>(space.tab_count)))
                          .Set("active", Value::Bool(space.active)));
        }
        done(ToolResult::Json(Value::MakeObject().Set("spaces", std::move(list))));
        return;
    }
    if (name == "browser_open_tab") {
        done(StatusResult(host_.OpenTab(arguments.StringOr("url", "")), "Opened a new tab."));
        return;
    }
    if (name == "browser_navigate") {
        const std::string_view url = arguments.StringOr("url", "");
        if (url.empty()) {
            done(ToolResult::Error("'url' is required."));
            return;
        }
        const std::optional<AgentTabInfo> info = FindTab(tab_arg.tab);
        if (info && !info->refusal.empty()) {
            done(ToolResult::Error(info->refusal));
            return;
        }
        const std::optional<std::string> target = host_.ResolveNavigationTarget(url);
        const std::string requested(url);
        auto navigate = [this, tab = tab_arg.tab, requested](ToolCallback finish) {
            finish(
                StatusResult(host_.Navigate(tab, requested), "Navigation started: " + requested));
        };
        if (!info || !target) {
            // An unknown tab or an address Island refuses: the host reports it, nothing loads.
            navigate(std::move(done));
            return;
        }
        Gate({.tool = "browser_navigate",
              .page_url = info->url,
              .target_url = *target,
              .tab_opened_by_agent = info->opened_by_agent},
             tab_arg.tab, std::move(navigate), std::move(done));
        return;
    }
    if (name == "browser_activate_tab") {
        if (!tab_arg.tab) {
            done(ToolResult::Error("'tab' is required."));
            return;
        }
        done(StatusResult(host_.ActivateTab(*tab_arg.tab), "Tab activated."));
        return;
    }
    if (name == "browser_close_tab") {
        if (!tab_arg.tab) {
            done(ToolResult::Error("'tab' is required."));
            return;
        }
        const std::size_t index = *tab_arg.tab;
        auto close = [this, index](ToolCallback finish) {
            finish(StatusResult(host_.CloseTab(index), "Tab closed."));
        };
        const std::optional<AgentTabInfo> info = FindTab(index);
        if (!info) {
            close(std::move(done));
            return;
        }
        Gate({.tool = "browser_close_tab",
              .page_url = info->url,
              .tab_opened_by_agent = info->opened_by_agent},
             index, std::move(close), std::move(done));
        return;
    }
    if (name == "browser_pin_tab") {
        std::optional<std::size_t> tab = tab_arg.tab;
        if (!tab) {
            for (const AgentTabInfo& info : host_.ListTabs()) {
                if (info.active) tab = info.index;
            }
        }
        if (!tab) {
            done(ToolResult::Error("There is no tab to pin."));
            return;
        }
        const bool pinned = arguments.BoolOr("pinned", true);
        done(StatusResult(host_.PinTab(*tab, pinned), pinned ? "Tab pinned." : "Tab unpinned."));
        return;
    }
    if (name == "browser_switch_space") {
        const std::optional<std::size_t> space = ReadIndex(arguments, "space");
        if (!space) {
            done(ToolResult::Error("'space' must be a non-negative integer."));
            return;
        }
        done(StatusResult(host_.SwitchSpace(*space), "Space switched."));
        return;
    }
    if (name == "browser_new_space") {
        done(StatusResult(host_.NewSpace(arguments.StringOr("name", "")), "Space created."));
        return;
    }
    struct ActionTool {
        std::string_view name;
        AgentBrowserAction action;
        std::string_view message;
    };
    static constexpr std::array<ActionTool, 5> kActions = {{
        {"browser_back", AgentBrowserAction::kBack, "Went back."},
        {"browser_forward", AgentBrowserAction::kForward, "Went forward."},
        {"browser_reload", AgentBrowserAction::kReload, "Reloading."},
        {"browser_toggle_split", AgentBrowserAction::kToggleSplit, "Split view toggled."},
        {"browser_toggle_sidebar", AgentBrowserAction::kToggleSidebar, "Sidebar toggled."},
    }};
    for (const ActionTool& tool : kActions) {
        if (name == tool.name) {
            done(StatusResult(host_.RunAction(tool.action), tool.message));
            return;
        }
    }

    if (name == "page_read") return ReadPage(arguments, std::move(done));
    if (name == "page_snapshot") return Snapshot(arguments, std::move(done));
    if (name == "page_click") return Click(arguments, std::move(done));
    if (name == "page_type") return TypeText(arguments, std::move(done));
    if (name == "page_press_key") return PressKey(arguments, std::move(done));
    if (name == "page_scroll") return Scroll(arguments, std::move(done));
    if (name == "page_evaluate") return EvaluateTool(arguments, std::move(done));
    if (name == "page_screenshot") return Screenshot(arguments, std::move(done));
    done(ToolResult::Error("Unhandled tool: " + std::string(name)));
}

void BrowserToolbox::Evaluate(
    std::optional<std::size_t> tab, std::string expression,
    std::function<void(bool ok, json::Value value, std::string error)> done) {
    Value params = Value::MakeObject()
                       .Set("expression", Value::String(std::move(expression)))
                       .Set("returnByValue", Value::Bool(true))
                       .Set("awaitPromise", Value::Bool(true))
                       .Set("userGesture", Value::Bool(true));
    host_.DevToolsCall(
        tab, "Runtime.evaluate", std::move(params), [done = std::move(done)](DevToolsReply reply) {
            if (!reply.ok) {
                done(false, {}, reply.error.empty() ? "DevTools call failed." : reply.error);
                return;
            }
            if (const Value* details = reply.result.FindMember("exceptionDetails")) {
                std::string message(details->StringOr("text", "Uncaught exception"));
                if (const Value* exception = details->FindMember("exception")) {
                    const std::string_view description = exception->StringOr("description", "");
                    if (!description.empty()) message = std::string(description);
                }
                done(false, {}, message);
                return;
            }
            const Value* result = reply.result.FindMember("result");
            if (result == nullptr) {
                done(true, {}, {});
                return;
            }
            const Value* value = result->FindMember("value");
            done(true, value != nullptr ? *value : Value{}, {});
        });
}

void BrowserToolbox::ReadPage(const json::Value& args, ToolCallback done) {
    std::int64_t max_chars =
        args.IntOr("max_chars", static_cast<std::int64_t>(kDefaultMaxPageChars));
    if (max_chars <= 0) max_chars = static_cast<std::int64_t>(kDefaultMaxPageChars);
    if (max_chars > static_cast<std::int64_t>(kMaxPageChars)) {
        max_chars = static_cast<std::int64_t>(kMaxPageChars);
    }
    Evaluate(ReadTabArg(args).tab, ReadPageScript(static_cast<std::size_t>(max_chars)),
             [done = std::move(done)](bool ok, Value value, std::string error) {
                 if (!ok) {
                     done(ToolResult::Error("page_read failed: " + error));
                     return;
                 }
                 std::optional<Value> page = ParseScriptJson(value);
                 if (!page) {
                     done(ToolResult::Error("page_read returned no page data."));
                     return;
                 }
                 done(ToolResult::Json(*page));
             });
}

void BrowserToolbox::Snapshot(const json::Value& args, ToolCallback done) {
    std::int64_t limit = args.IntOr("limit", 150);
    if (limit <= 0) limit = 150;
    if (limit > 1000) limit = 1000;
    Evaluate(ReadTabArg(args).tab, SnapshotScript(static_cast<std::size_t>(limit)),
             [done = std::move(done)](bool ok, Value value, std::string error) {
                 if (!ok) {
                     done(ToolResult::Error("page_snapshot failed: " + error));
                     return;
                 }
                 std::optional<Value> snapshot = ParseScriptJson(value);
                 if (!snapshot) {
                     done(ToolResult::Error("page_snapshot returned no data."));
                     return;
                 }
                 done(ToolResult::Json(*snapshot));
             });
}

void BrowserToolbox::ResetTaskGrants() { task_grants_.clear(); }

std::optional<AgentTabInfo> BrowserToolbox::FindTab(std::optional<std::size_t> tab) {
    for (const AgentTabInfo& info : host_.ListTabs()) {
        if (tab ? info.index == *tab : info.active) return info;
    }
    return std::nullopt;
}

std::optional<ToolResult> BrowserToolbox::Refusal(std::optional<std::size_t> tab) {
    const std::optional<AgentTabInfo> info = FindTab(tab);
    if (info && !info->refusal.empty()) return ToolResult::Error(info->refusal);
    return std::nullopt;
}

GateDecision BrowserToolbox::Decide(GateInput input) {
    // The page's own origin, as the page reports it, beats the tab list's URL, which can lag a
    // navigation; an origin without a host (data:, sandboxed) matches no trusted site.
    if (input.facts && !input.facts->page_origin.empty()) input.page_url = input.facts->page_origin;
    return Classify(input, host_.TrustedSites());
}

// Belt-and-braces: SameElement runs first and compares every fact the gate reads, except in_form.
// A change in in_form can only make an approved confirmation free, never blocked or a new rule, so
// the failing branch here is not reachable through a test.
bool BrowserToolbox::StillApproved(GateInput approved, GateInput fresh) {
    const GateDecision was = Decide(std::move(approved));
    const GateDecision now = Decide(std::move(fresh));
    return now.action == ActionClass::kFree || (now.action == was.action && now.rule == was.rule);
}

void BrowserToolbox::Gate(GateInput input, std::optional<std::size_t> tab,
                          std::function<void(ToolCallback)> run, ToolCallback done,
                          std::function<void(ToolCallback)> after_wait) {
    const GateDecision decision = Decide(input);
    if (decision.action == ActionClass::kFree) {
        run(std::move(done));
        return;
    }
    if (decision.action == ActionClass::kBlocked) {
        done(ToolResult::Error("The agent cannot type into password fields."));
        return;
    }
    const std::tuple<std::string, GateRule, std::string> grant{decision.host, decision.rule,
                                                               decision.scope};
    if (decision.trusted_allowed && task_grants_.count(grant) > 0) {
        run(std::move(done));
        return;
    }
    ConfirmationRequest request;
    request.id = "c" + std::to_string(next_confirmation_++);
    request.tool = input.tool;
    request.summary = decision.summary;
    request.host = decision.host;
    request.tab = tab;
    request.trusted_allowed = decision.trusted_allowed;
    const bool grantable = decision.trusted_allowed;
    host_.RequestConfirmation(
        std::move(request),
        [this, grant, grantable, run = std::move(run), after_wait = std::move(after_wait),
         done = std::move(done)](ConfirmationAnswer answer) mutable {
            // The person may have used the page while the card was open, so the action re-reads
            // its target when it can.
            auto proceed = [&] {
                if (after_wait) {
                    after_wait(std::move(done));
                } else {
                    run(std::move(done));
                }
            };
            switch (answer) {
                case ConfirmationAnswer::kAllowOnce:
                case ConfirmationAnswer::kTrustSite:
                    proceed();
                    return;
                case ConfirmationAnswer::kAllowForTask:
                    if (grantable) task_grants_.insert(grant);
                    proceed();
                    return;
                case ConfirmationAnswer::kTimeout:
                    done(ToolResult::Error("No one confirmed this action in time."));
                    return;
                case ConfirmationAnswer::kWithdrawn:
                    done(
                        ToolResult::Error("The tab changed or the task stopped before the action "
                                          "was confirmed; ask again."));
                    return;
                case ConfirmationAnswer::kCancel:
                    break;
            }
            done(ToolResult::Error("The person declined this action."));
        });
}

void BrowserToolbox::ReadFocusedFacts(std::optional<std::size_t> tab,
                                      std::function<void(std::optional<ElementFacts>)> done) {
    Evaluate(tab, ActiveElementScript(),
             [done = std::move(done)](bool ok, Value value, std::string /*error*/) {
                 std::optional<Value> result = ok ? ParseScriptJson(value) : std::nullopt;
                 done(result ? ParseFacts(*result) : std::nullopt);
             });
}

void BrowserToolbox::Click(const json::Value& args, ToolCallback done) {
    const std::string locator = ElementLocatorScript(args);
    if (locator.empty()) {
        done(ToolResult::Error("page_click needs 'ref' or 'selector'."));
        return;
    }
    const std::optional<std::size_t> tab = ReadTabArg(args).tab;
    if (std::optional<ToolResult> refused = Refusal(tab)) {
        done(std::move(*refused));
        return;
    }
    // What one evaluation of the target reports: where to click, or that it has no box, plus the
    // facts of that same element.
    struct Located {
        std::string error;
        bool synthetic = false;
        double x = 0;
        double y = 0;
        std::optional<ElementFacts> facts;
    };
    auto locate = [this, tab, locator](std::function<void(Located)> got) {
        Evaluate(
            tab, ClickTargetScript(locator),
            [got = std::move(got)](bool ok, Value value, std::string error) {
                Located out;
                if (!ok) {
                    out.error = "page_click failed: " + error;
                    got(std::move(out));
                    return;
                }
                std::optional<Value> target = ParseScriptJson(value);
                if (!target) {
                    out.error = "page_click returned no data.";
                } else if (!target->BoolOr("ok", false)) {
                    out.error = std::string(target->StringOr("error", "Not found."));
                } else {
                    out.synthetic = target->BoolOr("synthetic", false);
                    const Value* xv = target->FindMember("x");
                    const Value* yv = target->FindMember("y");
                    if (!out.synthetic) {
                        if (xv == nullptr || yv == nullptr || !xv->IsNumber() || !yv->IsNumber()) {
                            out.error = "page_click could not locate the element center.";
                        } else {
                            out.x = xv->AsDouble();
                            out.y = yv->AsDouble();
                        }
                    }
                    out.facts = ParseFacts(*target);
                }
                got(std::move(out));
            });
    };
    // Performs the click the gate allowed: a real click at the point, or element.click() for an
    // element without a box, which is found again and compared with the one that was judged.
    auto act = [this, tab, locator](const Located& target, ToolCallback finish) {
        if (!target.synthetic) {
            SendClick(host_, tab, target.x, target.y, std::move(finish));
            return;
        }
        const std::string expected = target.facts ? FactsLiteral(*target.facts) : "{}";
        Evaluate(
            tab, SyntheticClickScript(locator, expected),
            [finish = std::move(finish)](bool ok, Value value, std::string err) {
                std::optional<Value> reply = ok ? ParseScriptJson(value) : std::nullopt;
                if (!ok) {
                    finish(ToolResult::Error("page_click failed: " + err));
                } else if (!reply || !reply->BoolOr("ok", false)) {
                    finish(ToolResult::Error(
                        reply ? std::string(reply->StringOr("error", kPageChanged))
                              : std::string(kPageChanged)));
                } else {
                    finish(ToolResult::Text("Clicked (element has no box; used element.click())."));
                }
            });
    };
    locate([this, tab, locate, act, done = std::move(done)](Located target) mutable {
        if (!target.error.empty()) {
            done(ToolResult::Error(target.error));
            return;
        }
        GateInput input{.tool = "page_click", .facts = target.facts};
        if (const std::optional<AgentTabInfo> info = FindTab(tab)) input.page_url = info->url;
        const GateInput approved = input;
        Gate(
            std::move(input), tab,
            [act, target](ToolCallback finish) { act(target, std::move(finish)); }, std::move(done),
            // After the card: find the element again, require the same one, click where it is now.
            [this, locate, act, approved](ToolCallback finish) {
                locate([this, act, approved, finish = std::move(finish)](Located fresh) mutable {
                    GateInput judged = approved;
                    judged.facts = fresh.facts;
                    if (!fresh.error.empty()) {
                        finish(ToolResult::Error(fresh.error));
                    } else if (!SameElement(approved.facts, fresh.facts) ||
                               !StillApproved(approved, judged)) {
                        finish(ToolResult::Error(kPageChanged));
                    } else {
                        act(fresh, std::move(finish));
                    }
                });
            });
    });
}

void BrowserToolbox::TypeText(const json::Value& args, ToolCallback done) {
    const Value* text_value = args.FindMember("text");
    if (text_value == nullptr || !text_value->IsString()) {
        done(ToolResult::Error("page_type needs 'text'."));
        return;
    }
    const std::string text = text_value->string_val;
    const bool submit = args.BoolOr("submit", false);
    const std::optional<std::size_t> tab = ReadTabArg(args).tab;
    const std::string locator = ElementLocatorScript(args);
    if (std::optional<ToolResult> refused = Refusal(tab)) {
        done(std::move(*refused));
        return;
    }

    auto type_into_focus = [this, tab, text, submit](ToolCallback finish) {
        host_.DevToolsCall(
            tab, "Input.insertText", Value::MakeObject().Set("text", Value::String(text)),
            [this, tab, submit, finish = std::move(finish)](DevToolsReply inserted) mutable {
                if (!inserted.ok) {
                    finish(ToolResult::Error("page_type failed: " + inserted.error));
                    return;
                }
                if (!submit) {
                    finish(ToolResult::Text("Typed."));
                    return;
                }
                SendKey(host_, tab, *FindKey("Enter"),
                        [finish = std::move(finish)](bool ok, std::string error) {
                            finish(ok ? ToolResult::Text("Typed and pressed Enter.")
                                      : ToolResult::Error("page_type failed: " + error));
                        });
            });
    };
    // Focuses the target (when there is one) and reports the facts of the element that really has
    // focus afterwards; without a target, of whatever has focus now.
    auto read_facts = [this, tab, locator, clear = args.BoolOr("clear", true)](
                          std::function<void(std::string, std::optional<ElementFacts>)> got) {
        if (locator.empty()) {
            ReadFocusedFacts(tab, [got = std::move(got)](std::optional<ElementFacts> facts) {
                got({}, std::move(facts));
            });
            return;
        }
        Evaluate(tab, FocusScript(locator, clear),
                 [got = std::move(got)](bool ok, Value value, std::string error) {
                     if (!ok) {
                         got("page_type failed: " + error, std::nullopt);
                         return;
                     }
                     std::optional<Value> focused = ParseScriptJson(value);
                     if (!focused) {
                         got("page_type returned no data.", std::nullopt);
                         return;
                     }
                     const std::string_view focus_error = focused->StringOr("error", "");
                     if (!focus_error.empty()) {
                         got(std::string(focus_error), std::nullopt);
                         return;
                     }
                     // The focus did not land on the target: typing now would reach whatever
                     // has focus, so the tool refuses instead of reporting "Typed.".
                     if (!focused->BoolOr("ok", false)) {
                         got("page_type could not focus that element; call page_snapshot for "
                             "fresh refs.",
                             std::nullopt);
                         return;
                     }
                     got({}, ParseFacts(*focused));
                 });
    };
    read_facts([this, tab, submit, type_into_focus, read_facts, done = std::move(done)](
                   std::string error, std::optional<ElementFacts> facts) mutable {
        if (!error.empty()) {
            done(ToolResult::Error(error));
            return;
        }
        GateInput input{.tool = "page_type", .facts = facts, .submit = submit};
        if (const std::optional<AgentTabInfo> info = FindTab(tab)) input.page_url = info->url;
        const GateInput approved = input;
        Gate(std::move(input), tab, type_into_focus, std::move(done),
             // After the card: focus the target again and type only if it is still what was judged.
             [this, read_facts, type_into_focus, approved](ToolCallback finish) {
                 read_facts([this, type_into_focus, approved, finish = std::move(finish)](
                                std::string err, std::optional<ElementFacts> fresh) mutable {
                     GateInput judged = approved;
                     judged.facts = fresh;
                     if (!err.empty()) {
                         finish(ToolResult::Error(err));
                     } else if (!SameElement(approved.facts, fresh) ||
                                !StillApproved(approved, judged)) {
                         finish(ToolResult::Error(kPageChanged));
                     } else {
                         type_into_focus(std::move(finish));
                     }
                 });
             });
    });
}

void BrowserToolbox::PressKey(const json::Value& args, ToolCallback done) {
    const KeySpec* spec = FindKey(args.StringOr("key", ""));
    if (spec == nullptr) {
        done(ToolResult::Error("Unsupported key."));
        return;
    }
    const std::string key(spec->key);
    const std::optional<std::size_t> tab = ReadTabArg(args).tab;
    if (std::optional<ToolResult> refused = Refusal(tab)) {
        done(std::move(*refused));
        return;
    }
    auto press = [this, tab, spec, key](ToolCallback finish) {
        SendKey(host_, tab, *spec, [key, finish = std::move(finish)](bool ok, std::string error) {
            finish(ok ? ToolResult::Text("Pressed " + key + ".")
                      : ToolResult::Error("page_press_key failed: " + error));
        });
    };
    ReadFocusedFacts(tab, [this, tab, key, press,
                           done = std::move(done)](std::optional<ElementFacts> facts) mutable {
        GateInput input{.tool = "page_press_key", .facts = facts, .key = key};
        if (const std::optional<AgentTabInfo> info = FindTab(tab)) input.page_url = info->url;
        const GateInput approved = input;
        Gate(std::move(input), tab, press, std::move(done),
             // After the card: press only if focus is still on what was judged.
             [this, tab, press, approved](ToolCallback finish) {
                 ReadFocusedFacts(tab, [this, press, approved, finish = std::move(finish)](
                                           std::optional<ElementFacts> fresh) mutable {
                     GateInput judged = approved;
                     judged.facts = fresh;
                     if (SameElement(approved.facts, fresh) && StillApproved(approved, judged)) {
                         press(std::move(finish));
                     } else {
                         finish(ToolResult::Error(kPageChanged));
                     }
                 });
             });
    });
}

void BrowserToolbox::Scroll(const json::Value& args, ToolCallback done) {
    const std::string_view direction = args.StringOr("direction", "");
    if (direction != "up" && direction != "down" && direction != "top" && direction != "bottom") {
        done(ToolResult::Error("'direction' must be one of up, down, top, bottom."));
        return;
    }
    Evaluate(ReadTabArg(args).tab, ScrollScript(direction, args.IntOr("amount", 0)),
             [done = std::move(done)](bool ok, Value value, std::string error) {
                 if (!ok) {
                     done(ToolResult::Error("page_scroll failed: " + error));
                     return;
                 }
                 std::optional<Value> position = ParseScriptJson(value);
                 done(position ? ToolResult::Json(*position) : ToolResult::Text("Scrolled."));
             });
}

void BrowserToolbox::EvaluateTool(const json::Value& args, ToolCallback done) {
    const std::string_view expression = args.StringOr("expression", "");
    if (expression.empty()) {
        done(ToolResult::Error("'expression' is required."));
        return;
    }
    const std::optional<std::size_t> tab = ReadTabArg(args).tab;
    if (std::optional<ToolResult> refused = Refusal(tab)) {
        done(std::move(*refused));
        return;
    }
    GateInput input{.tool = "page_evaluate"};
    if (const std::optional<AgentTabInfo> info = FindTab(tab)) input.page_url = info->url;
    Gate(
        std::move(input), tab,
        [this, tab, script = std::string(expression)](ToolCallback finish) {
            Evaluate(
                tab, script, [finish = std::move(finish)](bool ok, Value value, std::string error) {
                    if (!ok) {
                        finish(ToolResult::Error("Evaluation failed: " + error));
                        return;
                    }
                    finish(ToolResult::Json(Value::MakeObject().Set("value", std::move(value))));
                });
        },
        std::move(done));
}

void BrowserToolbox::Screenshot(const json::Value& args, ToolCallback done) {
    host_.DevToolsCall(ReadTabArg(args).tab, "Page.captureScreenshot",
                       Value::MakeObject().Set("format", Value::String("png")),
                       [done = std::move(done)](DevToolsReply reply) {
                           if (!reply.ok) {
                               done(ToolResult::Error("page_screenshot failed: " + reply.error));
                               return;
                           }
                           const std::string_view data = reply.result.StringOr("data", "");
                           if (data.empty()) {
                               done(ToolResult::Error("page_screenshot returned no image."));
                               return;
                           }
                           ToolResult result;
                           result.content.push_back({.type = ToolContent::Type::kImage,
                                                     .data = std::string(data),
                                                     .mime_type = "image/png"});
                           done(std::move(result));
                       });
}

}  // namespace island::agent
