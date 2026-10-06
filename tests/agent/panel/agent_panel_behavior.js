// Behavioral check of src/main/pages/agent.html in headless Chromium.
//
// Optional (needs Node and Playwright, which the C++ build does not):
//   NODE_PATH=$(npm root -g) node tests/agent/panel/agent_panel_behavior.js
//
// Covers the page <-> native contract (console messages out, islandRender in),
// keyboard handling, the provider switcher, and that agent text can never
// inject HTML or script.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const html = fs.readFileSync(process.argv[2] || path.join(__dirname, '../../../src/main/pages/agent.html'), 'utf8');
const assert = (c, m) => { if (!c) { console.log('FAIL ' + m); process.exitCode = 1; } else console.log('ok   ' + m); };
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 380, height: 700 } });
  const sent = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('\u0001island:')) sent.push(JSON.parse(t.slice(8))); });
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

  // Provider switcher: the header title names the provider and opens a menu.
  const providers = [
    { id: 'claude', name: 'Claude Code', available: true, command: 'npx -y @agentclientprotocol/claude-agent-acp', install: 'Install Node.js, which provides npx', install_command: '', executable: 'npx', docs: 'https://github.com/agentclientprotocol/claude-agent-acp' },
    { id: 'opencode', name: 'OpenCode', available: false, command: 'opencode acp', install: 'npm i -g opencode-ai', install_command: 'npm i -g opencode-ai', executable: 'opencode', docs: 'https://opencode.ai/docs/acp/' },
    { id: 'gemini', name: 'Gemini CLI <b>x</b>', available: true, command: 'gemini --acp', install: 'npm i -g @google/gemini-cli', install_command: 'npm i -g @google/gemini-cli', executable: 'gemini', docs: '' },
    { id: 'custom', name: 'Custom command', available: false, command: '', install: 'Enter a command', install_command: '', executable: '', docs: '' } ];
  await render({ provider: 'claude', providers, env_override: false, session: { state: 'idle', status: 'Not running', items: [], busy: false } });
  assert((await page.textContent('#agent-name')) === 'Claude Code', 'header shows the provider name');
  assert((await page.getAttribute('#provider-button', 'aria-expanded')) === 'false', 'menu starts closed');
  await page.click('#provider-button');
  assert(await page.isVisible('#provider-menu'), 'clicking the title opens the menu');
  const rows = await page.$$eval('#provider-menu [role=menuitemradio]', r => r.map(x => [x.dataset.id, x.getAttribute('aria-checked'), x.textContent]));
  assert(rows.length === 3 && rows[0][0] === 'claude' && rows[0][1] === 'true', 'lists providers with the current one checked (blank custom hidden)');
  assert(rows[1][2].indexOf('Not found \u2014 npm i -g opencode-ai') >= 0, 'missing provider shows its install hint');
  assert(!(await page.evaluate(() => !!document.querySelector('#provider-menu b'))), 'provider names are text, not HTML');
  assert(await page.$eval('[data-id=opencode] .avail', n => n.getAttribute('data-available')) === 'false', 'availability dot');
  await page.click('[data-id=gemini]');
  assert(sent.some(m => m.type === 'set_provider' && m.id === 'gemini'), 'clicking a provider sends set_provider');
  assert(await page.isHidden('#provider-menu'), 'choosing closes the menu');

  // Keyboard: arrows open and move, Enter picks, Esc closes and restores focus.
  await page.focus('#provider-button');
  await page.keyboard.press('ArrowDown');
  assert(await page.evaluate(() => document.activeElement.dataset.id === 'claude'), 'ArrowDown opens on the current provider');
  await page.keyboard.press('ArrowDown');
  assert(await page.evaluate(() => document.activeElement.dataset.id === 'opencode'), 'ArrowDown moves');
  await page.keyboard.press('ArrowUp');
  await page.keyboard.press('ArrowUp');
  assert(await page.evaluate(() => document.activeElement.dataset.id === 'settings'), 'ArrowUp wraps to Custom...');
  await page.keyboard.press('Home');
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('Enter');
  assert(sent.some(m => m.type === 'set_provider' && m.id === 'opencode'), 'Enter picks the focused provider');
  await page.focus('#provider-button');
  await page.keyboard.press('Enter');
  assert(await page.isVisible('#provider-menu'), 'Enter on the title opens the menu');
  await page.keyboard.press('Escape');
  assert(await page.isHidden('#provider-menu'), 'Esc closes the menu');
  assert(await page.evaluate(() => document.activeElement.id === 'provider-button'), 'Esc returns focus to the title');
  assert(!sent.some(m => m.type === 'close_panel'), 'Esc in the menu does not hide the panel');
  await page.focus('#provider-button');
  await page.keyboard.press(' ');
  await page.keyboard.press('End');
  await page.keyboard.press(' ');
  assert(sent.some(m => m.type === 'open_settings'), 'Space on Custom... opens Settings');
  await page.click('#provider-button');
  await page.mouse.click(200, 600);
  assert(await page.isHidden('#provider-menu'), 'clicking outside closes the menu');

  // Empty state: the provider name, and the install hint with a copy button
  // when the provider was not found.
  await render({ provider: 'opencode' });
  assert((await page.textContent('.hero p')).indexOf('OpenCode can read pages') === 0, 'empty state names the provider');
  assert((await page.textContent('.missing .copy-row code')) === 'npm i -g opencode-ai', 'empty state shows the install command');
  await page.click('.missing .copy-row .btn');
  assert(/Copied|Select/.test(await page.textContent('.missing .copy-row .btn')), 'copy button reacts');
  await page.click('.missing .links .btn');
  assert(sent.some(m => m.type === 'open_url' && m.url === 'https://opencode.ai/docs/acp/'), 'install guide opens the docs');
  await render({ provider: 'claude' });
  assert(!(await page.$('.missing')), 'no install notice when the provider is available');
  await render({ provider: 'custom' });
  await page.click('.missing .btn');
  assert(sent.filter(m => m.type === 'open_settings').length === 2, 'blank custom command points to Settings');
  // ISLAND_AGENT_COMMAND: the header names the override and the menu says so.
  await render({ provider: 'opencode', env_override: true, env_command: 'my-agent --acp' });
  assert((await page.textContent('#agent-name')) === 'my-agent', 'env override shows its command');
  assert(!(await page.$('.missing')), 'env override hides the install notice');
  await page.click('#provider-button');
  assert((await page.textContent('#provider-menu')).indexOf('ISLAND_AGENT_COMMAND') >= 0, 'menu notes the env override');
  await page.keyboard.press('Escape');

  // A failed agent offers restart and the switcher; only custom has a field.
  await render({ provider: 'gemini', env_override: false, session: { state: 'failed', status: 'Stopped', items: [], busy: false, error: 'The agent exited (code 127).' } });
  assert(!(await page.$('#command')), 'no command field for a preset');
  await page.click('.setup .btn.primary');
  assert(sent.some(m => m.type === 'start' && !('command' in m)), 'restart sends start');
  await render({ provider: 'custom', providers: providers.map(p => p.id === 'custom' ? Object.assign({}, p, { command: 'my-agent --acp' }) : p), session: { state: 'failed', status: 'Stopped', items: [], busy: false, error: 'boom', command: 'my-agent --acp' } });
  assert((await page.inputValue('#command')) === 'my-agent --acp', 'custom shows its command');
  await page.fill('#command', 'other-agent');
  await page.press('#command', 'Enter');
  assert(sent.some(m => m.type === 'start' && m.command === 'other-agent'), 'custom restart sends the command');
  await browser.close();
})();
