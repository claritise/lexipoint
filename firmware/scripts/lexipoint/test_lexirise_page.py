"""The /lexirise page shows the same rows as the device's settings screen (settings.md §1; P13). The device
decides: web::stateJson sends `shows` (settings_screen::visibleRows), and each data-when names one of its keys.
The offline dictionaries and the language names have none, so they always show."""

import os
import re
import unittest
from html.parser import HTMLParser

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
PAGE = os.path.join(ROOT, "src", "lexirise", "web", "LexirisePage.html")
WEB_API = os.path.join(ROOT, "src", "lexirise", "web", "WebApi.cpp")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class Rows(HTMLParser):
    """Each .setting-row: its data-when, its name, and the data-field / data-toggle keys inside it; a
    data-when on the control itself (a language's name stays, its toggle hides) counts for that key."""

    def __init__(self):
        super().__init__()
        self.rows = []
        self._depth = 0
        self._in_name = False

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        classes = (a.get("class") or "").split()
        if tag == "div" and "setting-row" in classes:
            self.rows.append({"when": a.get("data-when"), "name": "", "keys": {}})
            self._depth = 1
            return
        if not self._depth:
            return
        if tag == "div":
            self._depth += 1
        for k in ("data-field", "data-toggle"):
            if k in a:
                self.rows[-1]["keys"][a[k]] = a.get("data-when", self.rows[-1]["when"])
        self._in_name = "setting-name" in classes

    def handle_endtag(self, tag):
        if self._depth and tag == "div":
            self._depth -= 1
        self._in_name = False

    def handle_data(self, data):
        if self._depth and self._in_name and not self.rows[-1]["name"]:
            self.rows[-1]["name"] = data.strip()


def parsed():
    p = Rows()
    p.feed(read(PAGE))
    return p.rows


def when_by_key():
    return {key: when for row in parsed() for key, when in row["keys"].items()}


def served_keys():
    table = re.search(r"constexpr PageRow kPageRows\[\] = \{(.*?)\n\};", read(WEB_API), re.S)
    assert table, "web::kPageRows not found in WebApi.cpp (renamed? update this test)"
    return re.findall(r'\{"(\w+)", settings_screen::Row::\w+\}', table.group(1))


class VisibilityRules(unittest.TestCase):
    def test_each_row_follows_its_own_device_row(self):
        self.assertEqual(
            when_by_key(),
            {
                "enabled": None,  # the Lexirise lookups switch itself
                "ja.enabled": "jaLookups",
                "ja.reading": "jaReading",
                "ja.stardict": None,  # the offline dictionaries: always
                "zh.enabled": "zhLookups",
                "zh.stardict": None,
                "defaultLanguage": "defaultLanguage",
                "tags": "tags",
                "wifiIdleMin": "wifiIdle",
                "baseUrl": None,  # under Advanced, as before
            },
        )

    def test_every_data_when_is_a_key_the_device_sends(self):
        served = served_keys()
        self.assertEqual(len(served), len(set(served)))
        used = {w for w in when_by_key().values() if w}
        self.assertEqual(used, set(served))
        self.assertIn("!s.shows[row.dataset.when]", read(PAGE))

    def test_each_dictionary_keeps_its_language_name(self):
        # Two bare "Offline dictionary" rows would not say which is which (the device has group headings).
        names = [row["name"] for row in parsed() if row["when"] is None]
        for language in ("Japanese", "Chinese (Simplified)"):
            self.assertEqual(names[names.index(language) + 1], "Offline dictionary", language)


if __name__ == "__main__":
    unittest.main()
