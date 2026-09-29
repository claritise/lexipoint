"""v0.2 V7b: the home screen's Sync Vocabulary sits just above Settings with its signed-off strings, and it's the only
place WiFi is joined for the vocab mirror (the page analysis, the idle pages and the card's probe never join:
host-tested in test/lexirise_net ServiceTest and test/lexirise_vocab ManualSyncTest). Source checks. (Until v0.2 V8
it also checked the row compiled out without Lexirise; V8 removed that build: test_lxctl LexiriseIsAlwaysBuilt.)"""

import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def read(path: str) -> str:
    with open(os.path.join(REPO, path), encoding="utf-8") as f:
        return f.read()


class HomeSyncRow(unittest.TestCase):
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
