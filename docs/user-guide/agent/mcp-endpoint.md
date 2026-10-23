# The MCP endpoint

**Planned.** This page is not written yet.

Island serves browser tools to external agents (Claude Code, Claude Desktop, Cursor, any MCP client) over MCP.

## What this page will cover

* What the endpoint is: `http://127.0.0.1:<port>/mcp`, local only, with a bearer token that changes on every launch
* Copying the ready-made config from Settings > Agent & tools > MCP endpoint
* Using the `island_mcp_bridge` stdio bridge for clients that need stdio
* The discovery file (`agent-endpoint.json`)
* Which tools external agents get, and which pages they cannot touch (the Plan tab)
* Security notes: local-only access and the token
