"""v0.2 V7b: the home screen's Sync Vocabulary compiles out without Lexirise, and it's the only place WiFi is joined
for the vocab mirror (the page analysis, the idle pages and the card's probe never join: host-tested in
test/lexirise_net ServiceTest and test/lexirise_vocab ManualSyncTest). Source checks; the x4pro-lexirise-off build
in the gate proves the #if blocks compile out."""

import os
import re
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def read(path: str) -> str:
    with open(os.path.join(REPO, path), encoding="utf-8") as f:
        return f.read()


def outside_lexirise(text: str, needle: str) -> list[int]:
    """Lines holding `needle` that aren't inside an `#if LEXIRISE` block (its #else half counts as outside)."""
    stack: list[bool] = []  # per open #if: whether its current branch is LEXIRISE-only
    bad = []
    for n, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if s.startswith("#if"):
            stack.append(bool(re.match(r"#if\s+LEXIRISE\b", s)))
        elif s.startswith("#else") and stack:
            stack[-1] = False
        elif s.startswith("#endif") and stack:
            stack.pop()
        elif needle in line and not any(stack):
            bad.append(n)
    return bad


class HomeSyncCompilesOut(unittest.TestCase):
    def test_every_mention_is_inside_if_lexirise(self):
        for path in ("src/activities/home/HomeActivity.h", "src/activities/home/HomeActivity.cpp",
                     "src/activities/ActivityManager.h", "src/activities/HomeMenuItem.h",
                     "src/activities/home/HomeMenuIndex.h"):
            text = read(path)
            for needle in ("VOCAB_SYNC", "vocabSync", "HomeSync", "STR_LEXI_SYNC"):
                self.assertEqual(outside_lexirise(text, needle), [], f"{needle} outside #if LEXIRISE in {path}")

    def test_the_row_sits_just_above_settings(self):
        text = read("src/activities/home/HomeActivity.cpp")
        self.assertIn("menuItems.insert(menuItems.end() - 1, tr(STR_LEXI_SYNC_VOCABULARY));", text)
        header = read("src/activities/home/HomeMenuIndex.h")
        # The index mapping puts it after File Transfer and before Settings.
        self.assertRegex(header, r"FILE_TRANSFER;[\s\S]*VOCAB_SYNC[\s\S]*SETTINGS_MENU")

    def test_the_strings_are_the_signed_off_ones(self):
        yaml = read("lib/I18n/translations/english.yaml")
        for key, text in (("STR_LEXI_SYNC_VOCABULARY", "Sync Vocabulary"),
                          ("STR_LEXI_SYNCING_VOCABULARY", "Syncing vocabulary..."),
                          ("STR_LEXI_VOCABULARY_UP_TO_DATE", "Vocabulary up to date"),
                          ("STR_LEXI_VOCABULARY_SYNCED", "Synced · %u words changed"),
                          ("STR_LEXI_VOCABULARY_SYNCED_ONE", "Synced · 1 word changed"),
                          ("STR_LEXI_SYNC_FAILED", "Sync failed"),
                          ("STR_LEXI_SYNC_NO_WIFI", "Sync failed · No Wi-Fi"),
                          ("STR_LEXI_SYNC_STOPPED", "Sync stopped")):
            self.assertIn(f'{key}: "{text}"', yaml)


class OnlyTheReadersSyncJoins(unittest.TestCase):
    def test_join_for_user_is_called_only_by_the_home_sync(self):
        callers = []
        for root, _, files in os.walk(os.path.join(REPO, "src")):
            for name in files:
                if not name.endswith((".cpp", ".h")):
                    continue
                path = os.path.relpath(os.path.join(root, name), REPO)
                if "joinForUser(" in read(path):
                    callers.append(path)
        self.assertEqual(sorted(callers), ["src/lexirise/LexiriseService.cpp", "src/lexirise/LexiriseService.h",
                                           "src/lexirise/vocab/HomeSync.cpp"])

    def test_the_mirror_and_page_calls_never_join(self):
        service = read("src/lexirise/LexiriseService.cpp")
        for call in ("vocabularyPage", "analyzePage"):
            body = service[service.index(f"LexiriseService::{call}("):]
            body = body[:body.index("}")]
            self.assertIn("/*mayJoin=*/false", body, call)


if __name__ == "__main__":
    unittest.main()
