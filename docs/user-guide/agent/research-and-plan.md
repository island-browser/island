# Research and the Plan tab

Research mode turns a question into a plan you can read, change and approve before the agent opens a single page. This page shows how to run a research, steer its plan and read the report.

## Start a research

1. Open a new tab. On the [start page](../search/start-page.md), choose **Research**.
2. Type your question and press Enter.
3. Island opens a new space named after your question. If a space with that name exists, Island reuses it.
4. The space opens with a **Plan** tab, pinned first.

You need an agent configured first. See [The agent panel](agent-panel.md).

## Read the plan

The Plan tab shows the agent's plan as steps, branches, a join and a result. Nothing runs yet; the header says **Waiting for approval**.

* Switch between **Graph view** and **List view** with the view control. The graph can zoom in and out, and **Tidy up the graph** re-lays it out.
* Click a step to open the **Inspector**. It shows the step, what it **Runs after**, and its status.
* Branches have their own colors, so you can tell the lines of work apart.

{% hint style="info" %}
A plan the agent shares without Island's branching details opens in **List view**, with steps named p0, p1 and so on.
{% endhint %}

## Edit the plan before you approve it

Select a step, then use the Inspector:

* **Skip this step** leaves it out. A skipped step shows "Skipped by you". **Restore this step** puts it back.
* **Instruction for this step** lets you write guidance the agent follows for that step.
* **Branch from here** (or **Add a branch after** a step) adds a new branch. A plan can branch up to **3 levels** deep.
* **Ask agent to expand** asks the agent to break that step into more steps.

## Approve and run

At the top of the Plan tab, choose one:

* **Run plan** runs every branch.
* **Run one branch only** runs the selected branch (the button names it, such as **Run A only**).
* **Cancel research** stops before anything runs.

As the agent works, the pages it opens appear as indented child tabs under the Plan tab, grouped by branch (for example under a "Branch A" label). Select a step in the plan to see its page. Pages open active, so you can watch.

## Retry a failed step

If a step fails, select it and click **Retry this step** in the Inspector. You can retry while the run goes on or after it ends. You cannot retry while the plan waits for approval, or after you cancelled it before approving.

## Stop or keep a research

* To stop it, click **Stop research**.
* If you close the Plan tab while research runs, Island asks whether to stop or **Keep running**. Esc keeps it running.
* Deleting the space that holds the Plan tab stops the research.

## Read the report

When the agent finishes, the final answer opens as a **Report** tab. It is grouped with the research in the sidebar. A report cites every claim.

In the report you can:

* **Copy Markdown**.
* **Save as .md…**.
* **Continue in agent panel** to ask follow-ups.

Island also saves each report in **Settings > Agent & tools > Reports**, and the [command palette](../search/command-palette.md) finds it as a REPORT row.

## Things to know

* Plan and Report tabs are not restored after a restart. Their pages come back as ordinary tabs, and the report stays in your Reports list.
* The agent cannot approve or change the Plan tab. Its navigation and page tools refuse that tab.
* A turn that proposes a plan and stops shows "Waiting for approval", not an error.
