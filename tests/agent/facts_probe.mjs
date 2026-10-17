// Runs the gate's page helpers (kFactsJs in src/agent/browser_tools.cc) against hand-built DOM
// stand-ins, because the C++ tests only ever see canned script replies.
// Usage: node facts_probe.mjs <path to browser_tools.cc>
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(process.argv[2], 'utf8');
const match = /kFactsJs = R"JS\(([\s\S]*?)\)JS";/.exec(source);
assert.ok(match, 'kFactsJs not found');

function load(document) {
  const body = `${match[1]}; return {active: __islandActive, within: __islandWithin, facts: __islandFacts, activeFacts: __islandActiveFacts};`;
  return new Function('document', 'location', body)(document, { origin: 'https://a.test' });
}

function el(tag, props = {}) {
  const attrs = props.attrs || {};
  return {
    tagName: tag.toUpperCase(), type: '', name: '', id: '', innerText: '', value: '',
    shadowRoot: null, contentDocument: null, form: null, parentNode: null,
    getAttribute: (k) => attrs[k] ?? null,
    closest: (sel) => (sel === 'form' && props.inForm ? {} : null),
    ...props,
  };
}
const doc = (active, extra = {}) => ({ activeElement: active, body: el('body'), ...extra });

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
