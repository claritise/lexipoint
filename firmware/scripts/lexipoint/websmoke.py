#!/usr/bin/env python3
"""Smoke-test the Lexirise web surface of a running reader (P1 gate, settings.md §1a-2).

    python3 scripts/lexipoint/websmoke.py 192.168.1.5        # device in "join network" mode
    python3 scripts/lexipoint/websmoke.py 192.168.4.1        # device hotspot

Read-only on purpose: it never saves settings, and it only ever names paths that don't exist, so a
broken guard shows up as a wrong answer, never as a deleted or overwritten file. It checks that
  - the page, the nav script and the API answer;
  - the API returns the key masked only;
  - a foreign Origin and a DNS-rebinding Host are refused;
  - the file manager (list, download, rename, move, delete) and WebDAV refuse hidden paths, also by
    their FAT short name (/LEXIRI~1 is /.lexirise) and with SdFat-ignored leading spaces.
"""

from __future__ import annotations

import http.client
import json
import re
import sys
import urllib.parse

TIMEOUT_S = 10.0
EVIL_ORIGIN = "http://evil.example"
EVIL_HOST = "evil.example"
PROBE = "/.lexirise/websmoke-does-not-exist"  # never exists: a failing guard can't touch real files
SHORT_DIR = "/LEXIRI~1"  # the FAT 8.3 alias of /.lexirise
SHORT_PROBE = SHORT_DIR + "/websmoke-does-not-exist"
MASKED_KEY = re.compile(r"^lx_•{8}[A-Za-z0-9_-]{3}$")


def request(host: str, method: str, path: str, body: str | None = None, headers: dict | None = None,
            port: int = 80) -> tuple[int, str]:
    conn = http.client.HTTPConnection(host, port, timeout=TIMEOUT_S)
    try:
        conn.request(method, path, body=body, headers=headers or {})
        res = conn.getresponse()
        return res.status, res.read().decode("utf-8", "replace")
    finally:
        conn.close()


def form(fields: dict) -> tuple[str, dict]:
    return urllib.parse.urlencode(fields), {"Content-Type": "application/x-www-form-urlencoded"}


def checks(host: str, port: int = 80):
    """Yields (name, passed, detail) for each check."""
    status, body = request(host, "GET", "/lexirise", port=port)
    yield "page loads", status == 200 and "Lexirise" in body, f"status {status}"
    status, body = request(host, "GET", "/lexirise/nav.js", port=port)
    yield "nav script loads", status == 200 and "/lexirise" in body, f"status {status}"

    status, body = request(host, "GET", "/api/lexirise", port=port)
    state = {}
    try:
        state = json.loads(body) if status == 200 else {}
    except ValueError:
        pass
    yield "API answers", status == 200 and "status" in state, f"status {status}"
    key = state.get("key", "")
    masked = (key == "" and not state.get("hasKey")) or bool(MASKED_KEY.match(key))
    yield "key is masked", masked, "key field is not in the masked form" if not masked else "ok"

    status, _ = request(host, "GET", "/api/lexirise", headers={"Origin": EVIL_ORIGIN}, port=port)
    yield "foreign Origin refused (GET)", status == 403, f"status {status}"
    status, _ = request(host, "POST", "/api/lexirise", body="{}",
                        headers={"Origin": EVIL_ORIGIN, "Content-Type": "application/json"}, port=port)
    yield "foreign Origin refused (POST)", status == 403, f"status {status}"
    status, _ = request(host, "GET", "/api/lexirise", headers={"Host": EVIL_HOST}, port=port)
    yield "rebinding Host refused", status == 403, f"status {status}"

    quoted = urllib.parse.quote(PROBE)
    status, _ = request(host, "GET", f"/api/files?path={urllib.parse.quote('/.lexirise')}", port=port)
    yield "hidden folder not listed", status == 403, f"status {status}"
    status, _ = request(host, "GET", f"/download?path={quoted}", port=port)
    yield "hidden download refused", status == 403, f"status {status}"
    body_, hdrs = form({"path": PROBE, "name": "renamed"})
    status, _ = request(host, "POST", "/rename", body=body_, headers=hdrs, port=port)
    yield "hidden rename refused", status == 403, f"status {status}"
    body_, hdrs = form({"path": "/websmoke-does-not-exist", "dest": "/.lexirise"})
    status, _ = request(host, "POST", "/move", body=body_, headers=hdrs, port=port)
    yield "move into hidden folder refused", status == 403, f"status {status}"
    body_, hdrs = form({"path": PROBE})
    status, text = request(host, "POST", "/delete", body=body_, headers=hdrs, port=port)
    yield "hidden delete refused", "system file" in text, f"status {status}: {text[:80]}"

    # SdFat skips leading spaces: "/ .lexirise" is /.lexirise ("+" also decodes to a space).
    status, _ = request(host, "GET", "/download?path=/%20.lexirise/websmoke-does-not-exist", port=port)
    yield "leading-space download refused", status == 403, f"status {status}"
    status, _ = request(host, "GET", "/download?path=/+.lexirise/websmoke-does-not-exist", port=port)
    yield "plus-space download refused", status == 403, f"status {status}"
    status, _ = request(host, "PROPFIND", "/%20.lexirise/", headers={"Depth": "1"}, port=port)
    yield "leading-space PROPFIND refused", status == 403, f"status {status}"

    # FAT short name of /.lexirise: must be refused like the long name (a 404 means the alias got in).
    status, _ = request(host, "GET", f"/download?path={urllib.parse.quote(SHORT_PROBE)}", port=port)
    yield "short-name download refused", status == 403, f"status {status}"
    status, _ = request(host, "GET", f"/api/files?path={urllib.parse.quote(SHORT_DIR)}", port=port)
    yield "short-name folder not listed", status == 403, f"status {status}"

    status, text = request(host, "PROPFIND", "/.lexirise/", headers={"Depth": "1"}, port=port)
    yield "WebDAV PROPFIND refused", status == 403 and "config.ini" not in text, f"status {status}"
    status, text = request(host, "GET", "/.lexirise/config.ini", port=port)
    yield "WebDAV GET refused", status != 200 and "api_key" not in text, f"status {status}"
    status, text = request(host, "PROPFIND", SHORT_DIR + "/", headers={"Depth": "1"}, port=port)
    yield "WebDAV short-name PROPFIND refused", status == 403 and "config.ini" not in text, f"status {status}"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    target = sys.argv[1]
    host, _, port = target.partition(":")
    failed = 0
    for name, passed, detail in checks(host, int(port) if port else 80):
        print(f"{'PASS' if passed else 'FAIL'}  {name}" + ("" if passed else f"  ({detail})"))
        failed += 0 if passed else 1
    print("websmoke OK" if not failed else f"websmoke FAILED: {failed} check(s)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
