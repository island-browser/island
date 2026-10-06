#!/usr/bin/env python3
"""Minimal ACP agent for the AgentSession tests.

Speaks newline-delimited JSON-RPC on stdio: answers initialize and
session/new, and for each session/prompt streams an echo of the last text
block, asks for one permission, and ends the turn with the outcome.
A prompt of "crash" exits with status 3 after writing to stderr.
"""
import json
import sys


def send(message):
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def read():
    line = sys.stdin.readline()
    if not line:
        sys.exit(0)
    return json.loads(line)


def update(session_id, payload):
    send({"jsonrpc": "2.0", "method": "session/update",
          "params": {"sessionId": session_id, "update": payload}})


def main():
    print("fake agent booting (not JSON-RPC)", flush=True)
    while True:
        message = read()
        method = message.get("method")
        if method == "initialize":
            send({"jsonrpc": "2.0", "id": message["id"], "result": {
                "protocolVersion": 1,
                "agentCapabilities": {"mcpCapabilities": {"http": True}},
                "agentInfo": {"name": "fake", "title": "Fake Agent"}}})
        elif method == "session/new":
            servers = message["params"].get("mcpServers", [])
            sys.stderr.write("mcp servers: %d\n" % len(servers))
            send({"jsonrpc": "2.0", "id": message["id"], "result": {"sessionId": "s1"}})
        elif method == "session/prompt":
            blocks = message["params"]["prompt"]
            text = blocks[-1]["text"]
            if text == "crash":
                sys.stderr.write("boom: simulated failure\n")
                sys.stderr.flush()
                sys.exit(3)
            update("s1", {"sessionUpdate": "agent_message_chunk",
                          "content": {"type": "text", "text": "echo: " + text}})
            if len(blocks) > 1:
                update("s1", {"sessionUpdate": "agent_message_chunk",
                              "content": {"type": "text", "text": " | ctx: " + blocks[0]["text"]}})
            update("s1", {"sessionUpdate": "tool_call", "toolCallId": "t1",
                          "title": "browser_list_tabs", "kind": "fetch", "status": "pending"})
            send({"jsonrpc": "2.0", "id": "perm-1", "method": "session/request_permission",
                  "params": {"sessionId": "s1", "toolCall": {"toolCallId": "t1"},
                             "options": [{"optionId": "yes", "name": "Allow", "kind": "allow_once"},
                                         {"optionId": "no", "name": "Deny", "kind": "reject_once"}]}})
            answer = read()
            while answer.get("id") != "perm-1":
                answer = read()
            outcome = answer["result"]["outcome"]
            chosen = outcome.get("optionId", outcome["outcome"])
            update("s1", {"sessionUpdate": "tool_call_update", "toolCallId": "t1",
                          "status": "completed" if chosen == "yes" else "failed"})
            send({"jsonrpc": "2.0", "id": message["id"], "result": {
                "stopReason": "cancelled" if chosen == "cancelled" else "end_turn"}})
        elif method == "session/cancel":
            pass


if __name__ == "__main__":
    main()
