// Runs the gate's page helpers against hand-built DOM stand-ins, because the C++ tests only ever
// see canned script replies. It also runs the page-side counter normalizer (kLiveCountersJs) on
// the same pairs as LiveCountersNormalizeOnlyTimesAndATrailingCounter in browser_tools_test.cpp.
// Usage: node facts_probe.cjs <path to browser_tools.cc>
//
// Written as CommonJS with ES2015 syntax only, and the version check is the first statement, so
// it parses and stops cleanly on old Node. The launcher (run_facts_probe.cmake) checks the version
// before it runs this file; a direct run on Node below 18 exits 77 with the skip marker.
if (Number(process.versions.node.split('.')[0]) < 18) {
  console.log('FACTS_PROBE_SKIP: Node 18 or newer is needed (found ' + process.versions.node + ').');
  process.exit(77);
}

const fs = require('fs');
const assert = require('assert').strict;

const source = fs.readFileSync(process.argv[2], 'utf8');
const match = /kFactsJs = R"JS\(([\s\S]*?)\)JS";/.exec(source);
assert.ok(match, 'kFactsJs not found');
const liveMatch = /kLiveCountersJs = R"JS\(([\s\S]*?)\)JS";/.exec(source);
assert.ok(liveMatch, 'kLiveCountersJs not found');
const marksBlock = /kCurrencyMarks\[\] = \{([\s\S]*?)\};/.exec(source);
assert.ok(marksBlock, 'kCurrencyMarks not found');
// Literals in the table; the comment lines hold no quotes.
const currencyMarks = Array.from(marksBlock[1].matchAll(/"((?:[^"\\]|\\.)*)"/g), (m) => m[1]);
assert.ok(currencyMarks.length > 0, 'currency table is empty');

// The page-side rule, with the table the C++ builds into the script.
const live = new Function('__islandCurrencyMarks', liveMatch[1] + ' return {liveCounters, liveEqual};')(currencyMarks);
const liveCounters = live.liveCounters;
const liveEqual = live.liveEqual;

function load(document) {
  const body = `${match[1]}; return {active: __islandActive, within: __islandWithin, facts: __islandFacts, activeFacts: __islandActiveFacts};`;
  return new Function('document', 'location', body)(document, { origin: 'https://a.test' });
}

function el(tag, props) {
  props = props || {};
  const attrs = props.attrs || {};
  const base = {
    tagName: tag.toUpperCase(), type: '', name: '', id: '', innerText: '', value: '',
    shadowRoot: null, contentDocument: null, form: null, parentNode: null,
    getAttribute: (k) => (attrs[k] !== undefined ? attrs[k] : null),
    closest: (sel) => (sel === 'form' && props.inForm ? {} : null),
  };
  return Object.assign(base, props);
}
const doc = (active, extra) => Object.assign({ activeElement: active, body: el('body') }, extra || {});

// The same pairs as kLiveCases in browser_tools_test.cpp: `match` is whether the labels count as the
// same after a card, in the page copy and in C++.
const livePairs = [
  ['Resend in 0:42', 'Resend in 0:41', true],
  ['Pay now (expires 4:59)', 'Pay now (expires 4:58)', true],
  ['Like (12)', 'Like (13)', true],
  ['Checkout (3)', 'Checkout (12)', true],
  ['Pay $5', 'Pay $500', false],
  ['Transfer 10 USD', 'Transfer 9000 USD', false],
  ['Buy 1', 'Buy 10', false],
  ['Pay $5 (1)', 'Pay $5 (2)', false],
  ['Pay 5 (1)', 'Pay 5 (2)', false],
  ['Köp 29:90', 'Köp 299:90', false],
  ['Betala 29:90 kr', 'Betala 299:90 kr', false],
  ['Pay 5 SEK (1)', 'Pay 5 SEK (2)', false],
  ['Оплатить 500 руб (1)', 'Оплатить 500 руб (2)', false],
  ['Pay 5 USDT (1)', 'Pay 5 USDT (2)', false],
  ['Pay ＄5 (1)', 'Pay ＄5 (2)', false],
  ['Pay ₿ (5)', 'Pay ₿ (500)', false],
  ['Order # (#)', 'Order 1:00 (5)', false],
  ['Pay ５ (1)', 'Pay ５ (2)', false],
  ['Pay ５0:42', 'Pay ５9:42', false],
  ['Pay 0:42５', 'Pay 9:42５', false],
  ['Pay 0:42:5５', 'Pay 9:42:5５', false],
  ['ادفع ٥٠ (1)', 'ادفع ٥٠ (2)', false],
  ['Pay ￦ (5)', 'Pay ￦ (500)', false],
  ['Pay ﹩ (5)', 'Pay ﹩ (500)', false],
  ['Pay ﷼ (5)', 'Pay ﷼ (500)', false],
  ['Pay ₨ (5)', 'Pay ₨ (500)', false],
  ['Pay 5 грн (1)', 'Pay 5 грн (2)', false],
  ['Pay 5 Rs (1)', 'Pay 5 Rs (2)', false],
  ['Pay 5 Rp (1)', 'Pay 5 Rp (2)', false],
  ['Pay 5 RM (1)', 'Pay 5 RM (2)', false],
  ['Try again in 0:42', 'Try again in 0:41', true],
];
for (const [before, after, match] of livePairs) {
  assert.equal(liveEqual(before, after), match, before + ' -> ' + after);
  assert.equal(liveEqual(after, before), match, after + ' -> ' + before);
}
// Normalized forms, as NormalizeLiveCounters returns them in C++.
assert.equal(liveCounters('0:42'), '#');
assert.equal(liveCounters('Timer 0:42 (3)'), 'Timer # (3)');
assert.equal(liveCounters('Checkout (12)  '), 'Checkout (#)  ');
assert.equal(liveCounters('Pay 5 (1)'), 'Pay 5 (1)');
assert.equal(liveCounters('299:90'), '299:90');
assert.equal(liveCounters('Pay 5 kr'), 'Pay 5 kr');
assert.equal(liveCounters('Usdx 0:42'), 'Usdx #');
assert.equal(liveCounters('Okra 0:42'), 'Okra #');
assert.equal(liveCounters('ادفع ٥٠ (1)'), 'ادفع ٥٠ (1)');
assert.equal(liveCounters('1:02:034'), '#:034');
assert.equal(liveCounters('1:00:2:00'), '#:#');
assert.equal(liveCounters('Pay ５0:42'), 'Pay ５0:42');
assert.equal(liveCounters('Pay 0:42５'), 'Pay 0:42５');
assert.equal(liveCounters('Pay 0:42:5５'), 'Pay 0:42:5５');

// A plain focused input.
let input = el('input', { type: 'text', inForm: true });
let h = load(doc(input));
assert.equal(h.activeFacts().tag, 'input');
assert.equal(h.activeFacts().in_form, true);

// Nothing focused: the body's facts, not null (page_press_key PageDown must stay free).
h = load(doc(null));
assert.equal(h.activeFacts().tag, 'body');
assert.equal(h.activeFacts().in_form, false);

// A password field inside a same-origin frame is what page_type would hit.
const password = el('input', { type: 'password' });
const frame = el('iframe', { contentDocument: doc(password) });
h = load(doc(frame));
assert.equal(h.activeFacts().type, 'password');
assert.equal(h.active().el, password);
assert.ok(h.within(frame, password) === false, 'no parent chain was built yet');
password.parentNode = { parentNode: null, defaultView: { frameElement: frame } };
assert.ok(h.within(frame, password), 'within follows the frame element out of a frame');

// A cross-origin frame (contentDocument is null) hides its focus: no facts.
h = load(doc(el('iframe')));
assert.equal(h.activeFacts(), null);
assert.equal(h.active().opaque, true);

// An open shadow root: the focused element inside it, not the host.
const secret = el('input', { type: 'password' });
const host = el('login-form', { shadowRoot: { activeElement: secret } });
secret.parentNode = { parentNode: null, host };
h = load(doc(host));
assert.equal(h.activeFacts().type, 'password');
assert.ok(h.within(host, secret));

// A custom element without a visible shadow root may hide a closed one: no facts.
h = load(doc(el('login-form')));
assert.equal(h.activeFacts(), null);

// A wrapper that merely contains the focused password field is not the focused element.
const wrapper = el('div');
const inner = el('input', { type: 'password' });
inner.parentNode = wrapper;
h = load(doc(inner));
assert.equal(h.activeFacts().type, 'password');
assert.ok(h.within(wrapper, inner));
assert.ok(!h.within(inner, wrapper));

console.log('facts probe: ok');
