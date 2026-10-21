#!/usr/bin/env python3
"""Mock Ollama-compatible provider for the tool-call loop golden tests.

Serves canned assistant replies from a JSON list supplied via the
MOCK_RESPONSES environment variable (path to a JSON file containing a
list of strings). Replies are returned in order, cycling once exhausted,
so a multi-turn loop can be scripted deterministically.

Small-model-style failures (prose around JSON, trailing commas, two
calls, wrong tool name) can be included in the list verbatim; the server
does not validate or rewrite them.

The server binds the LOOPBACK interface only and, unless a port is given
on the command line, takes an OS-assigned port and writes it to the file
named by MOCK_PROVIDER_PORT_FILE. The harness reads that file to learn
the port, so two runs can never collide on a fixed port.

The server is threaded and has a deep accept backlog. A single wedged
client connection must never be able to stall every later request: that
failure mode is indistinguishable, from the outside, from "the provider
was never reachable", and it used to turn the macOS CI job into a wall
of 30 s connect timeouts with nothing to look at.

Usage:
    MOCK_RESPONSES=/tmp/replies.json \
    MOCK_PROVIDER_PORT_FILE=/tmp/port \
        python3 mock_tool_provider.py [port]
"""
import http.server
import json
import os
import sys
import threading


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
LOCK = threading.Lock()


class Handler(http.server.BaseHTTPRequestHandler):
    def _headers(self, body):
        self.send_response(200)
        self.send_header("Content-type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()

    def do_GET(self):
        if self.path in ("/", "/health"):
            body = b"healthy\n"
            self.send_response(200)
            self.send_header("Content-type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/api/tags":
            body = json.dumps({"models": [{"name": "mock"}]}).encode()
            self._headers(body)
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()

    def do_POST(self):
        # Log the request so a failing case shows whether it ever
        # arrived. Without this, "the provider was unreachable" and "the
        # provider was reached but never answered" look the same.
        print(f"mock provider: POST {self.path}", flush=True)
        n = int(self.headers.get("Content-Length", 0))
        self.rfile.read(n)
        with LOCK:
            idx = INDEX["i"] % len(RESPONSES)
            content = RESPONSES[idx]
            INDEX["i"] += 1
        reply = {
            "model": "mock",
            "created_at": "2023-09-23T00:00:00Z",
            "message": {"role": "assistant", "content": content},
            "done": True,
        }
        self._body = json.dumps(reply).encode()
        self._headers(self._body)
        self.wfile.write(self._body)

    def log_message(self, *args):
        pass


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    # A deep backlog: a burst of connections is queued rather than
    # dropped, so a slow client cannot turn into a connect timeout for
    # the next one.
    request_queue_size = 128


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    # Loopback only. The provider is a test fixture and has no reason to
    # be reachable from another host.
    httpd = Server(("127.0.0.1", port), Handler)
    actual = httpd.server_address[1]
    # Record this process's own PID. The harness checks every recorded
    # PID at the end of the suite and fails if any provider outlived its
    # case. The provider writes the record itself, so the check does not
    # depend on the harness keeping track of the PID: a provider started
    # in a subshell, where the harness would lose it, is still recorded.
    pid_file = os.environ.get("MOCK_PROVIDER_PID_FILE", "")
    if pid_file:
        with open(pid_file, "a") as f:
            f.write(str(os.getpid()) + "\n")
            f.flush()
    port_file = os.environ.get("MOCK_PROVIDER_PORT_FILE", "")
    if port_file:
        # Write atomically so the harness never reads a half-written file.
        tmp = port_file + ".tmp"
        with open(tmp, "w") as f:
            f.write(str(actual) + "\n")
        os.replace(tmp, port_file)
    print(
        f"mock tool provider on port {actual} with {len(RESPONSES)} canned replies",
        flush=True,
    )
    httpd.serve_forever()


if __name__ == "__main__":
    main()
