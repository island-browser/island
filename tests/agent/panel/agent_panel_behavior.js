// Behavioral check of src/main/agent_panel.html in headless Chromium.
//
// Optional (needs Node and Playwright, which the C++ build does not):
//   NODE_PATH=$(npm root -g) node tests/agent/panel/agent_panel_behavior.js
//
// Covers the page <-> native contract (console messages out, islandRender in),
// keyboard handling, and that agent text can never inject HTML or script.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const html = fs.readFileSync(process.argv[2] || path.join(__dirname, '../../../src/main/agent_panel.html'), 'utf8');
const assert = (c, m) => { if (!c) { console.log('FAIL ' + m); process.exitCode = 1; } else console.log('ok   ' + m); };
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 380, height: 700 } });
  const sent = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('\u0001island-agent:')) sent.push(JSON.parse(t.slice(14))); });
  await page.setContent(html);
  const render = s => page.evaluate(x => window.islandRender(x), s);
  await render({ session: { state: 'ready', status: 'Ready', items: [], busy: false }, context: { title: 'Docs', url: 'https://d.test/' } });
  assert(sent[0] && sent[0].type === 'ready', 'announces ready');
  await page.fill('#input', 'hello there');
  await page.press('#input', 'Enter');
  assert(sent.some(m => m.type === 'send' && m.text === 'hello there' && m.include_context === true), 'Enter sends with context');
  assert((await page.inputValue('#input')) === '', 'composer clears');
  await page.click('#context');
  await page.fill('#input', 'no ctx');
  await page.click('#send');
  assert(sent.some(m => m.type === 'send' && m.text === 'no ctx' && m.include_context === false), 'context toggle off');
  await page.fill('#input', 'line1');
  await page.press('#input', 'Shift+Enter');
  assert(!sent.some(m => m.text === 'line1'), 'Shift+Enter does not send');
  await render({ session: { state: 'prompting', busy: true, items: [
    { kind: 'agent', text: 'x <img src=x onerror="window.pwned=1"> [click](javascript:alert(1)) **b**' },
    { kind: 'permission', request_id: 9, text: 'page_click', options: [{ id: 'y', name: 'Allow', kind: 'allow_once' }], resolution: '' } ] } });
  assert(!(await page.evaluate(() => !!document.querySelector('.agent img'))), 'agent HTML is not injected');
  assert(!(await page.evaluate(() => window.pwned)), 'no script ran');
  assert(!(await page.evaluate(() => !!document.querySelector('a[href^="javascript"]'))), 'javascript: links are not linked');
  await page.click('.card.ask .btn');
  assert(sent.some(m => m.type === 'permission' && m.request_id === 9 && m.option_id === 'y'), 'permission click');
  await page.focus('#input');
  await page.press('#input', 'Escape');
  assert(sent.some(m => m.type === 'cancel'), 'Esc cancels while busy');
  await page.click('#send');
  assert(sent.filter(m => m.type === 'cancel').length === 2, 'stop button cancels while busy');
  await render({ session: { state: 'ready', busy: false, items: [{ kind: 'agent', text: 'see https://example.com/a.' }] } });
  await page.click('.agent a');
  assert(sent.some(m => m.type === 'open_url' && m.url === 'https://example.com/a'), 'links open in a tab (trailing dot trimmed)');
  await page.click('#new-chat');
  assert(sent.some(m => m.type === 'new_chat'), 'new chat');
  await browser.close();
})();
