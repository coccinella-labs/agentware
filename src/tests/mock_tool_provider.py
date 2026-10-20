#!/usr/bin/env python3
"""Mock Ollama-compatible provider for the tool-call loop golden tests.

Serves canned assistant replies from a JSON list supplied via the
MOCK_RESPONSES environment variable (path to a JSON file containing a
list of strings). Replies are returned in order, cycling once exhausted,
so a multi-turn loop can be scripted deterministically.

Small-model-style failures (prose around JSON, trailing commas, two
calls, wrong tool name) can be included in the list verbatim; the server
does not validate or rewrite them.

Usage:
    MOCK_RESPONSES=/tmp/replies.json python3 mock_tool_provider.py [port]
"""
import http.server
import json
import os
import sys


def load_responses():
    path = os.environ.get("MOCK_RESPONSES")
    if path and os.path.exists(path):
        with open(path) as f:
            data = json.load(f)
        if isinstance(data, list):
            return data
    # Default: a single plain answer.
    return ["This is a mock response."]


RESPONSES = load_responses()
INDEX = {"i": 0}


class Handler(http.server.BaseHTTPRequestHandler):
    def _headers(self):
        self.send_response(200)
        self.send_header("Content-type", "application/json")
        self.end_headers()

    def do_GET(self):
        if self.path in ("/", "/health"):
            self.send_response(200)
            self.send_header("Content-type", "text/plain")
            self.end_headers()
            self.wfile.write(b"healthy\n")
        elif self.path == "/api/tags":
            self._headers()
            self.wfile.write(json.dumps({"models": [{"name": "mock"}]}).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        self.rfile.read(n)
        idx = INDEX["i"] % len(RESPONSES)
        content = RESPONSES[idx]
        INDEX["i"] += 1
        reply = {
            "model": "mock",
            "created_at": "2023-09-23T00:00:00Z",
            "message": {"role": "assistant", "content": content},
            "done": True,
        }
        self._headers()
        self.wfile.write(json.dumps(reply).encode())

    def log_message(self, *args):
        pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 11434
    httpd = http.server.HTTPServer(("0.0.0.0", port), Handler)
    print(f"mock tool provider on {port} with {len(RESPONSES)} canned replies")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
