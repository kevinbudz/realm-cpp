#!/usr/bin/env python3
"""Serve the web build (build-web/) for local testing.

Usage: python3 web/serve.py [port]   (default port 8080)
Open http://localhost:8080/realm_client.html?server=127.0.0.1&port=2051
"""
import functools
import http.server
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "build-web"


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".wasm": "application/wasm", ".js": "text/javascript", ".data": "application/octet-stream"}

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    if not ROOT.is_dir():
        sys.exit(f"{ROOT} does not exist: run cmake --build build-web first")
    handler = functools.partial(Handler, directory=str(ROOT))
    with http.server.ThreadingHTTPServer(("", port), handler) as server:
        print(f"Serving {ROOT} on http://localhost:{port}/realm_client.html")
        server.serve_forever()


if __name__ == "__main__":
    main()
