"""The card's words are all translatable (popup-ui.md §4): every CardStrings field has its STR_LEXI_CARD_*
key in english.yaml, with the same English, and its line in cardStringsFromI18n()."""

from __future__ import annotations

import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def read(path: str) -> str:
    with open(os.path.join(REPO, path), encoding="utf-8") as f:
        return f.read()


def card_strings() -> list[tuple[str, str]]:
    """(key, English) for every CardStrings field, array elements one by one."""
    model = read("src/lexirise/card/CardModel.h")
    start = model.index("struct CardStrings {")
    block = model[start:model.index("\n};", start)]
    out = []
    for line in block.splitlines():
        m = re.match(r'\s*const char\* (\w+)(\[[^\]]+\])? = (.*);', line)
        if not m:
            continue
        name, array, value = m.groups()
        values = re.findall(r'"((?:[^"\\]|\\.)*)"', re.sub(r";?\s*//.*$", "", value))
        key = "STR_LEXI_CARD_" + re.sub(r"([A-Z])", r"_\1", name).upper()
        for i, v in enumerate(values):
            text = bytes(v, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
            out.append((f"{key}_{i}" if array else key, text))
    return out


class CardStringsAreTranslatable(unittest.TestCase):
    def test_every_field_has_its_key_and_line(self):
        fields = card_strings()
        self.assertGreater(len(fields), 40)
        yaml = read("lib/I18n/translations/english.yaml")
        cpp = read("src/lexirise/card/CardStringsI18n.cpp")
        for key, english in fields:
            m = re.search(rf'^{key}: "((?:[^"\\]|\\.)*)"$', yaml, re.M)
            self.assertIsNotNone(m, f"{key} missing from english.yaml")
            self.assertEqual(m.group(1).replace('\\"', '"').replace("\\\\", "\\"), english, key)
            self.assertIn(f"tr({key});", cpp, f"{key} not set in cardStringsFromI18n()")


# The reading session's summary (C1, C7, V6): SummaryFormats' English defaults (host tests) against english.yaml.
SUMMARY_KEYS = {"counts": "STR_LEXI_SESSION_COUNTS", "wordsJa": "STR_LEXI_SESSION_WORDS_JA",
                "wordJa": "STR_LEXI_SESSION_WORDS_JA_ONE", "wordsZh": "STR_LEXI_SESSION_WORDS_ZH",
                "wordZh": "STR_LEXI_SESSION_WORDS_ZH_ONE"}


class SummaryStringsMatchTheYaml(unittest.TestCase):
    def test_every_format_has_its_key_and_line(self):
        header = read("src/lexirise/session/ReadingSession.h")
        start = header.index("struct SummaryFormats {")
        block = header[start:header.index("\n};", start)]
        found = {}
        for line in block.splitlines():
            m = re.match(r'\s*const char\* (\w+) = "((?:[^"\\]|\\.)*)";', line)
            if m:
                found[m.group(1)] = bytes(m.group(2), "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
        self.assertEqual(set(found), set(SUMMARY_KEYS))
        yaml = read("lib/I18n/translations/english.yaml")
        cpp = read("src/lexirise/session/HomeSummary.cpp")
        for field, key in SUMMARY_KEYS.items():
            m = re.search(rf'^{key}: "((?:[^"\\]|\\.)*)"$', yaml, re.M)
            self.assertIsNotNone(m, f"{key} missing from english.yaml")
            self.assertEqual(m.group(1), found[field], key)
            self.assertIn(f"formats.{field} = tr({key});", cpp, f"{key} not set in takeHomeSummary()")


if __name__ == "__main__":
    unittest.main()
