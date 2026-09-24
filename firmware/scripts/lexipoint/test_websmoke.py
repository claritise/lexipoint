"""websmoke.py against a local fake device: all checks pass on a well-behaved one, and each guard,
broken on its own, makes its check fail. Run: python3 -m unittest test_websmoke"""

import http.server
import json
import os
import sys
import threading
import unittest
import urllib.parse

sys.path.insert(0, os.path.dirname(__file__))
import websmoke  # noqa: E402

MASKED = "lx_" + "•" * 8 + "abc"


class FakeDevice(http.server.BaseHTTPRequestHandler):
    broken: set = set()  # which guards are "missing"

    def log_message(self, *args):  # quiet
        pass

    def _send(self, code, body="", ctype="text/plain"):
        data = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _api_allowed(self):
        origin = self.headers.get("Origin", "")
        host = self.headers.get("Host", "")
        if "origin" not in self.broken and origin and origin != f"http://{host}":
            return False
        if "host" not in self.broken and host.split(":")[0] == websmoke.EVIL_HOST:
            return False
        return True

    def _hidden(self, *paths):
        return "files" not in self.broken and any(
            seg.startswith(".") for p in paths for seg in p.split("/") if seg)

    def _form(self):
        length = int(self.headers.get("Content-Length", 0))
        return urllib.parse.parse_qs(self.rfile.read(length).decode())

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        if url.path == "/lexirise":
            return self._send(200, "<title>Lexirise</title>", "text/html")
        if url.path == "/lexirise/nav.js":
            return self._send(200, "link.href = '/lexirise'", "application/javascript")
        if url.path == "/api/lexirise":
            if not self._api_allowed():
                return self._send(403, '{"error":"cross-origin"}')
            key = "lx_FULLSECRETKEY0000abc" if "mask" in self.broken else MASKED
            return self._send(200, json.dumps({"hasKey": True, "key": key, "status": {"state": "connected"}}))
        if url.path in ("/api/files", "/download"):
            return self._send(403 if self._hidden(q.get("path", [""])[0]) else 404)
        if url.path.startswith("/."):
            return self._send(200 if "dav" in self.broken else 403, "api_key=x" if "dav" in self.broken else "")
        return self._send(404)

    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        if url.path.startswith("/api/lexirise"):
            self._form()
            return self._send(200 if self._api_allowed() else 403)
        f = self._form()
        if url.path == "/rename":
            return self._send(403 if self._hidden(f["path"][0]) else 404)
        if url.path == "/move":
            return self._send(403 if self._hidden(f["path"][0], f["dest"][0]) else 404)
        if url.path == "/delete":
            hidden = self._hidden(f["path"][0])
            return self._send(500, f"{f['path'][0]} (system file); " if hidden else "not found")
        return self._send(404)

    def do_PROPFIND(self):
        if "dav" in self.broken:
            return self._send(207, "<d:href>/.lexirise/config.ini</d:href>")
        return self._send(403)


class WebSmoke(unittest.TestCase):
    def setUp(self):
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeDevice)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        FakeDevice.broken = set()

    def results(self):
        return {name: passed for name, passed, _ in websmoke.checks("127.0.0.1", self.port)}

    def test_healthy_device_passes_everything(self):
        results = self.results()
        self.assertTrue(all(results.values()), [n for n, ok in results.items() if not ok])

    def test_each_broken_guard_is_caught(self):
        expectations = {
            "mask": {"key is masked"},
            "origin": {"foreign Origin refused (GET)", "foreign Origin refused (POST)"},
            "host": {"rebinding Host refused"},
            "files": {"hidden folder not listed", "hidden download refused", "hidden rename refused",
                      "move into hidden folder refused", "hidden delete refused"},
            "dav": {"WebDAV PROPFIND refused", "WebDAV GET refused"},
        }
        for guard, should_fail in expectations.items():
            FakeDevice.broken = {guard}
            failed = {n for n, ok in self.results().items() if not ok}
            self.assertEqual(failed, should_fail, guard)


if __name__ == "__main__":
    unittest.main()
