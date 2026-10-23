# The agent panel

The agent panel puts an AI agent beside your page. The agent speaks the Agent Client Protocol (ACP). Island does not bundle a model: it starts an agent program on your computer and talks to it.

## Open and close the panel

* Press ⌘J (Ctrl+J), or click the **Agent** button at the bottom of the sidebar.
* To hide the panel, click the close button in its header, or press ⌘J again.

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
3. Read the reply. The panel renders Markdown and offers a Copy button on code.
4. To stop a running answer, click **Stop** or press Esc.

The agent can read the page and act in the browser through Island's tools: it can open and switch tabs, navigate, and read, click and type on pages. It cannot touch the Plan tab: only you can approve a plan.

## Share the current tab

The **context** button above the box is on by default. While it is on, the agent sees the current tab. Click it to turn sharing off.

## Start over

Click **New chat** in the header to clear the conversation and begin a new one.

## Ask from the start page

Choose **Ask Agent** on the [start page](../search/start-page.md) and press Enter. The panel opens with your question.

## The address-field agent dot

In the Native look, a dot at the right of the address field shows agent state. It is blue while the agent works and orange while it waits for your answer. Click it to open the panel.

## Related

* [Research and the Plan tab](research-and-plan.md)
* [The MCP endpoint](mcp-endpoint.md)
