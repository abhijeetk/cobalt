#!/usr/bin/env python3
"""Standalone server for the copied WebKit FairPlay fps-hls layout test.

Borrowed test assets in this directory come from WebKit:
https://github.com/WebKit/WebKit/tree/main/LayoutTests/http/tests/media/fairplay
https://github.com/WebKit/WebKit/blob/main/LayoutTests/media/video-test.js
"""

from __future__ import annotations

import argparse
import mimetypes
import os
from pathlib import Path
import re
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from subprocess import run
import sys
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parent
CKC_SCRIPT = ROOT / "media" / "fairplay" / "resources" / "index.py"
KEYSERVER_ROOT = CKC_SCRIPT.parent
AUTOINSTALL_DISABLE_ENV = "DISABLE_WEBKITCOREPY_AUTOINSTALLER"
RANGE_RE = re.compile(r"bytes=(\d*)-(\d*)$")


class FairPlayTestHandler(BaseHTTPRequestHandler):
    server_version = "FairPlayFpsHlsTest/1.0"

    def do_GET(self) -> None:
        self._serve_static(send_body=True)

    def do_HEAD(self) -> None:
        self._serve_static(send_body=False)

    def do_POST(self) -> None:
        if self._request_path() != "/media/fairplay/resources/index.py":
            self.send_error(HTTPStatus.NOT_FOUND, "Unknown POST endpoint")
            return

        content_length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(content_length)

        env = os.environ.copy()
        env[AUTOINSTALL_DISABLE_ENV] = "1"
        env["PYTHONPATH"] = str(KEYSERVER_ROOT) + os.pathsep + env.get("PYTHONPATH", "")
        env["PATH"] = str(Path(sys.executable).parent) + os.pathsep + env.get("PATH", "")

        completed = run(
            [sys.executable, str(CKC_SCRIPT)],
            input=body,
            stdout=-1,
            stderr=-1,
            cwd=str(KEYSERVER_ROOT),
            env=env,
            timeout=30,
        )

        if completed.stderr:
            sys.stderr.buffer.write(completed.stderr)
            sys.stderr.flush()

        status, headers, response_body = self._parse_cgi_response(completed.stdout)
        if completed.returncode and status == HTTPStatus.OK:
            status = HTTPStatus.INTERNAL_SERVER_ERROR

        self.send_response(status)
        for name, value in headers:
            if name.lower() not in {"status", "content-length", "connection"}:
                self.send_header(name, value)
        self.send_header("Content-Length", str(len(response_body)))
        self.end_headers()
        self.wfile.write(response_body)

    def _serve_static(self, send_body: bool) -> None:
        request_path = self._request_path()
        if request_path == "/":
            self.send_response(HTTPStatus.FOUND)
            self.send_header("Location", "/media/fairplay/fps-hls.html")
            self.end_headers()
            return

        file_path = self._file_path_for_request(request_path)
        if not file_path or not file_path.is_file():
            self.send_error(HTTPStatus.NOT_FOUND, "File not found")
            return

        file_size = file_path.stat().st_size
        start, end, partial = self._range_for(file_size)
        length = end - start + 1

        self.send_response(HTTPStatus.PARTIAL_CONTENT if partial else HTTPStatus.OK)
        self.send_header("Content-Type", self._content_type(file_path))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if partial:
            self.send_header("Content-Range", f"bytes {start}-{end}/{file_size}")
        self.end_headers()

        if not send_body:
            return

        with file_path.open("rb") as file:
            file.seek(start)
            remaining = length
            while remaining:
                chunk = file.read(min(1024 * 256, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)

    def _request_path(self) -> str:
        return unquote(urlsplit(self.path).path)

    def _file_path_for_request(self, request_path: str) -> Path | None:
        relative = request_path.lstrip("/")
        normalized = os.path.normpath(relative)
        if normalized.startswith("..") or os.path.isabs(normalized):
            return None
        candidate = (ROOT / normalized).resolve()
        try:
            candidate.relative_to(ROOT)
        except ValueError:
            return None
        return candidate

    def _range_for(self, file_size: int) -> tuple[int, int, bool]:
        header = self.headers.get("Range")
        if not header:
            return 0, file_size - 1, False

        match = RANGE_RE.match(header.strip())
        if not match:
            return 0, file_size - 1, False

        start_text, end_text = match.groups()
        if start_text:
            start = int(start_text)
            end = int(end_text) if end_text else file_size - 1
        else:
            suffix_length = int(end_text)
            start = max(file_size - suffix_length, 0)
            end = file_size - 1

        if start >= file_size or end < start:
            self.send_error(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
            return 0, file_size - 1, False

        return start, min(end, file_size - 1), True

    @staticmethod
    def _content_type(file_path: Path) -> str:
        if file_path.suffix == ".m3u8":
            return "application/vnd.apple.mpegurl"
        if file_path.suffix == ".ts":
            return "video/mp2t"
        if file_path.suffix == ".der":
            return "application/octet-stream"
        return mimetypes.guess_type(str(file_path))[0] or "application/octet-stream"

    @staticmethod
    def _parse_cgi_response(output: bytes) -> tuple[int, list[tuple[str, str]], bytes]:
        normalized = output.replace(b"\r\n", b"\n")
        header_blob, separator, body = normalized.partition(b"\n\n")
        if not separator:
            return HTTPStatus.OK, [("Content-Type", "application/octet-stream")], output

        status = HTTPStatus.OK
        headers: list[tuple[str, str]] = []
        for raw_line in header_blob.split(b"\n"):
            line = raw_line.decode("iso-8859-1")
            if not line or ":" not in line:
                continue
            name, value = line.split(":", 1)
            value = value.strip()
            if name.lower() == "status":
                status = int(value.split(" ", 1)[0])
            else:
                headers.append((name, value))

        if not any(name.lower() == "content-type" for name, _ in headers):
            headers.append(("Content-Type", "application/octet-stream"))
        return status, headers, body


def main() -> int:
    parser = argparse.ArgumentParser(description="Serve the standalone FairPlay fps-hls test resource")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", default=8000, type=int)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), FairPlayTestHandler)
    print(f"Serving FairPlay fps-hls test at http://{args.host}:{args.port}/media/fairplay/fps-hls.html")
    print(f"Python executable: {sys.executable}")
    print(f"{AUTOINSTALL_DISABLE_ENV}=1 is set for CKC subprocesses")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
