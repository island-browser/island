# The agent panel

The agent panel puts an AI agent beside your page. The agent speaks the Agent Client Protocol (ACP). Island does not bundle a model: it starts an agent program on your computer and talks to it.

## What you see

* **The header** shows the agent's name and a status pill. The pill reads **Waiting for approval** in amber while a card needs you, **Working** while the agent runs, and **Idle** when it is ready for your next message. An error or a stopped agent shows its own short text, such as **Not running** or **Stopped**.
* **Your messages** appear as tinted bubbles. The agent's replies are plain text.
* **Each tool call** is one line: a check mark when it finished, a cross when it failed, and a small spinner while it runs. The line names the tool and the site it used.
* **The box at the bottom** is one card. Type in the field; the chip on the left shows the tab the agent can read (click it to leave the tab out of this message), and the round arrow on the right sends.

## Open and close the panel

* Press ⌘J (Ctrl+J), or click the **Agent** button at the bottom of the sidebar.
* To hide the panel, click the close button in its header, or press ⌘J again.
* Closing the panel does not stop a task that is running. The agent keeps working, and a card that is waiting for you stays until you answer it.

## Choose an agent

1. Click the agent name in the panel header ("Choose an agent").
2. Pick one from the menu.

| Agent | Needs |
| --- | --- |
| **Claude Code** (default) | A Claude login or `ANTHROPIC_API_KEY`. Runs through `npx`. |
| **Codex** | A ChatGPT login or `OPENAI_API_KEY`. Runs through `npx`. |
| **OpenCode** | `npm i -g opencode-ai` |
| **Gemini CLI** | `npm i -g @google/gemini-cli`, and a Google sign-in or `GEMINI_API_KEY` |
| **Qwen Code** | `npm i -g @qwen-code/qwen-code` |
| **Goose** | The `goose` program installed |

The menu shows which agents are installed. For one that is missing, it shows an install guide and the command to run.

To use another ACP agent, set a custom command in **Settings > Agent & tools > Agent in the sidebar**. See [Agent and tools](../settings/agent-and-tools.md).

{% hint style="info" %}
`npx` ships with Node.js. Install Node.js first if you want the default agent.
{% endhint %}

## Ask something

1. Click the box that says "Ask anything, or tell it what to do".
2. Type, then press Enter to send.
3. Read the reply. The panel renders Markdown, including code blocks.
4. To stop a running answer, click **Stop** or press Esc.

If the agent is busy, what you send joins the queue. It appears under the conversation as "Queued · #1" and starts when the task ahead of it ends.

The agent can read the page and act in the browser through Island's tools: it can open and switch tabs, navigate, and read, click and type on pages. It cannot touch the Plan tab: only you can approve a plan. Before it does something consequential, it asks you first (see [Confirmations](#confirmations)).

## Confirmations

Some actions can change a page or your tabs in ways you cannot undo easily. Before the agent takes one, a card appears above the box with the title **Allow this action?**, the site, and what the agent wants to do.

The card offers these answers:

* **Allow once** lets this one action run.
* **Allow for this task** lets the same kind of action run on this site until the current task ends. A new task starts with no grants.
* **Trust this site** lets the agent act on this site without asking, from now on. You can undo it in Settings (see [Trusted sites](#trusted-sites)).
* **Cancel** declines the action. Pressing Esc also declines the oldest card.

If you do not answer within 2 minutes, the action does not run. The card does not take focus, so typing keeps going where you left it.

The agent asks before:

* a click on a submit button, or on a button or link whose words include one of a list of words such as Buy, Order, Pay, Send, Post, Publish, Delete, Remove, Subscribe or Book (English and Russian);
* pressing Enter in a form, or typing with a submit;
* running a script on a page;
* opening another website in one of your tabs. Tabs the agent opened itself are free to navigate;
* closing one of your tabs. Tabs the agent opened itself are free to close;
* typing into a card field (a card number, a security code or an expiry date). This asks on every site, and a card field never offers **Allow for this task** or **Trust this site**.

The agent never types into a password or sign-in code field. Island refuses that action and tells the agent so.

When a card is no longer valid, it goes away by itself, and the action does not run. That happens when the tab the card is for closes, moves to another place, or opens another site, and when you press Stop. A card stays when you switch tabs or spaces, as long as its tab is still there. The one exception is a card that names no tab during ordinary chat: it is for the tab you are viewing, so switching away removes it. During a research, a card with no tab is for the agent's own page, so it stays while you look elsewhere. The agent is told to ask again.

The address-field agent dot and the indicator on the sidebar Agent entry turn orange while a card waits for you.

## Trusted sites

Trusting a site means you accept the agent acting on it for the consequential actions above, without asking. Trust covers the site and its subdomains: trusting `example.com` also covers `shop.example.com`, but not `example.com.evil.net`.

Trust never covers card fields or actions the agent cannot classify; those always ask. Password and sign-in code fields are always refused.

To review or remove trusted sites, open **Settings > Agent & tools > Trusted sites**. Each host has a Remove button. Removing a host takes effect at once.

## The Tasks strip

Once the agent has work to do (a message, an Ask, a research or a collection), a **Tasks** strip appears at the top of the panel. It stays until you clear the finished tasks. Each row shows a task's title and its state:

* **Queued** tasks wait their turn, numbered from the front of the queue. Use the arrow buttons to move a queued task up or down, or the ✕ button to remove it.
* **Running** shows the task's progress when the agent reports a plan, for example "3/7".
* **Needs you** means the task is waiting for an answer on a card, or for your approval of a research plan. The next task does not start until you answer.
* **Done**, **Failed** and **Stopped** are finished tasks. **Clear finished** removes them from the strip.

To stop the running task, press its **Stop** button or Stop in the panel. Stop also withdraws its card, and the next queued task starts. The sidebar Agent entry shows plan progress such as `3/7`, or a count such as `2 queued`. In the Arc and Graphite looks, the button text includes `Agent · 3/7` or `Agent · 2 queued`. The command palette has **Agent tasks**, which opens the panel.

## Ask from a selection, a link or the page

You can ask the agent about what you are reading without typing the page into the box.

* **Selected text:** right-click the selection for **Ask agent about selection**. The panel opens with the selection shown as a quote above the box, and you type your question. Right-click for **Explain** to send "Explain this in plain words." with the selection at once.
* **A link:** right-click it for **Summarize linked page**. The link opens in a peek card for you, and the agent reads the page in its own background tab, which it closes afterwards.
* **The whole page:** the command palette has **Summarize page** and **Ask about page…**.

Each of these becomes a task in the Tasks strip. The page's title, address and selection are sent as text from the web, so the agent treats them as data, not as instructions.

The context chip above the box shows the page the agent is looking at. Click its ✕ to remove it. The chip and the quote are cleared when you change tab, open a new tab, start a new chat, or close the panel.

These items appear only on web pages, and only when an agent is set up.

## Collect from tabs

To gather the same information from several tabs into one table, use **Collect from tabs…** in the command palette. See [Collect from tabs](collect-from-tabs.md).

## Share the current tab

The **context** button above the box is on by default. While it is on, the agent sees the current tab. Click it to turn sharing off.

## Start over

Click **New chat** in the header to clear the conversation and begin a new one. A card that is still waiting is withdrawn, and the action does not run.

## Ask from the start page

Choose **Ask Agent** on the [start page](../search/start-page.md) and press Enter. The panel opens with your question.

## The address-field agent dot

In the Native look, a dot at the right of the address field shows agent state. It is blue while the agent works and orange while it waits for your answer. Click it to open the panel.

## Related

* [Collect from tabs](collect-from-tabs.md)
* [Research and the Plan tab](research-and-plan.md)
* [The MCP endpoint](mcp-endpoint.md)
* [Agent and tools settings](../settings/agent-and-tools.md)
