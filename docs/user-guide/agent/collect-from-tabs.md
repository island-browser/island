# Collect from tabs

Collect from tabs asks the agent to read several of your tabs and answer with one table. You get the table as a report you can export as CSV, JSON or Markdown.

## Start a collection

1. Open the command palette (⌘K, Ctrl+K) and choose **Collect from tabs…**. The Collect sheet opens in the agent panel.
2. Type what to collect, for example "product name, price and delivery date".
3. Check the tabs to read. The list shows the web tabs of the current space, with each tab's title and address. Pinned tabs start unchecked. You can read at most 30 tabs in one collection.
4. Click **Collect**. The button stays dimmed until you have typed something and checked at least one tab.

The collection becomes a task in the [Tasks strip](agent-panel.md#the-tasks-strip). It waits behind any task that is already running, and you can move or remove it while it is queued.

Island sends the agent the tabs' titles and addresses, and the agent reads each page. The page text is web content: the agent treats it as data, not as instructions. A collection can use the same confirmations as any other agent work. For example, the agent asks before it opens another site in one of your tabs.

## Where the table goes

When the agent answers with a table, Island saves it as a **collected table** in your Reports list and opens it in a Report tab. The command palette shows it with the label TABLE, and the Report tab says "Collected table".

If the answer has no table, nothing is saved as a collected table. The agent's answer still appears in the conversation.

When you click **Collect**, Island rechecks each choice against its address. A tab that closed is dropped. A tab that moved to another position is matched by its address, and a tab whose address changed is not included.

## Export the table

Open the report, click **Export**, and choose a format:

* **CSV (.csv):** UTF-8 with a byte order mark, so spreadsheets read accented letters correctly, and CRLF line ends. A cell that starts with `=`, `+`, `-`, `@`, a tab or a carriage return gets a leading apostrophe, so a spreadsheet shows it as text and does not run it.
* **JSON (.json):** an array of objects keyed by the column headers. A repeated header gets a number, so `Source`, `Source 2` and so on are kept.
* **Markdown (.md):** the whole report, not only the table.

CSV and JSON are disabled when the report has no table. Hover over the item to see "No table in this report".

Links in the table are exported as their addresses. When a link's text differs from its address, the column gets a second column with the text.

{% hint style="info" %}
A CSV file is text. Check the values before you use them in a spreadsheet. The apostrophe protects against running formulas, not against wrong answers.
{% endhint %}

## Related

* [The agent panel](agent-panel.md)
* [Research and the Plan tab](research-and-plan.md)
