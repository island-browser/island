# Agent and tools settings

The Agent & tools section sets up the sidebar agent and the MCP endpoint, and lists your research reports.

This page describes **Trusted sites**. The other parts of the section are still planned.

## Trusted sites

**Settings > Agent & tools > Trusted sites** lists the sites you trust the agent on. For a trusted site, the agent acts without asking on the consequential actions described in [The agent panel](../agent/agent-panel.md#confirmations): submit buttons, words such as Buy or Delete, Enter in a form, scripts, navigation to another site in your tab, and closing your tab.

* To trust a site, choose **Trust this site** on a confirmation card. The site is added here.
* To stop trusting a site, click **Remove** beside it. The change takes effect at once.
* A trusted site covers its subdomains. Trusting `example.com` also trusts `shop.example.com`, but not `example.com.evil.net`.
* The list stores bare host names, without `www.`. Entries that are the same site are listed once.

Trust never covers card fields or an action the agent cannot classify. Those always ask. Password and sign-in code fields are always refused.

When the list is empty, the section says "No trusted sites yet. Choose “Trust this site” when the agent asks."

## What this page will cover

* Choosing an agent and setting a custom command
* Open agent
* Copying the MCP config
* The Reports list
