#!/usr/bin/env python3
"""Serve the web build (build-web/) for local testing.

Usage: python3 web/serve.py [port] [--capture-dir DIR]
  port           default 8080
  --capture-dir  where POST /capture?name=NAME writes NAME.png (default ./web-captures, relative to the cwd)
Open http://localhost:8080/realm_client.html?server=127.0.0.1&port=2051

POST /capture?name=NAME stores the request body (a PNG) as CAPTURE_DIR/NAME.png. NAME is reduced to letters,
digits, '.', '_' and '-' (other characters become '_'); a trailing '.png' is optional. Replies 201 with JSON
{"path": ..., "bytes": ...}. The shell POSTs a benchmark's &capture=1 frame here.
"""
import argparse
import functools
import http.server
import json
import os
import pathlib
import re
import sys
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parent.parent / "build-web"
MAX_CAPTURE_BYTES = 64 * 1024 * 1024
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
UNSAFE_NAME_CHARACTERS = re.compile(r"[^A-Za-z0-9._-]")


def capture_file_name(name):
    """The stored file name for a capture name, or ValueError when nothing usable is left."""
    stem = UNSAFE_NAME_CHARACTERS.sub("_", name).lstrip(".")
    if stem.lower().endswith(".png"):
        stem = stem[:-4]
    stem = stem[:120].strip("._")
    if not stem:
        raise ValueError("capture name is empty after sanitizing")
    return stem + ".png"


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".wasm": "application/wasm", ".js": "text/javascript", ".data": "application/octet-stream"}
    capture_dir = pathlib.Path("web-captures")

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def reply(self, status, payload):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        if url.path != "/capture":
            self.reply(404, {"error": "unknown POST path"})
            return
        names = urllib.parse.parse_qs(url.query).get("name")
        if not names:
            self.reply(400, {"error": "name query parameter required"})
            return
        try:
            file_name = capture_file_name(names[0])
        except ValueError as error:
            self.reply(400, {"error": str(error)})
            return
        length = self.headers.get("Content-Length", "")
        if not length.isdigit():
            self.reply(411, {"error": "Content-Length required"})
            return
        size = int(length)
        if size <= 0 or size > MAX_CAPTURE_BYTES:
            self.reply(413, {"error": "capture size outside 1 byte to 64 MiB"})
            return
        body = self.rfile.read(size)
        if len(body) != size or not body.startswith(PNG_SIGNATURE):
            self.reply(415, {"error": "body is not a complete PNG"})
            return
        self.capture_dir.mkdir(parents=True, exist_ok=True)
        target = self.capture_dir / file_name
        partial = self.capture_dir / (file_name + ".part")
        partial.write_bytes(body)
        os.replace(partial, target)
        path = str(target.resolve())
        print(f"capture: wrote {path} ({size} bytes)", flush=True)
        self.reply(201, {"path": path, "bytes": size})


def main():
    parser = argparse.ArgumentParser(description="Serve the web build and accept benchmark captures.")
    parser.add_argument("port", nargs="?", type=int, default=8080)
    parser.add_argument("--capture-dir", default="web-captures",
                        help="directory for POST /capture PNGs (default ./web-captures)")
    args = parser.parse_args()
    if not ROOT.is_dir():
        sys.exit(f"{ROOT} does not exist: run cmake --build build-web first")
    capture_dir = pathlib.Path(args.capture_dir).resolve()
    handler = functools.partial(type("CaptureHandler", (Handler,), {"capture_dir": capture_dir}),
                                directory=str(ROOT))
    with http.server.ThreadingHTTPServer(("", args.port), handler) as server:
        print(f"Serving {ROOT} on http://localhost:{args.port}/realm_client.html", flush=True)
        print(f"Captures: POST http://localhost:{args.port}/capture?name=NAME -> {capture_dir}", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
