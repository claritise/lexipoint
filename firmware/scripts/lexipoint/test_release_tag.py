"""release_tag.py: Lexipoint's release tags (firmware-base.md §6), as publish_release.py checks them."""

import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import release_tag  # noqa: E402

REPO = release_tag.REPO


class ReleaseTag(unittest.TestCase):
    def test_expected_tags(self):
        self.assertEqual(release_tag.expected_tag("1.6.5", "2", False), "1.6.5-lexi.2")
        self.assertEqual(release_tag.expected_tag("1.6.5", "2", True), "1.6.5-lexi.2-rc")

    def test_a_good_release_passes(self):
        self.assertEqual(release_tag.check("1.6.5-lexi.2", "1.6.5", "2", False, latest="1.6.5-lexi.1"), [])
        self.assertEqual(release_tag.check("1.6.5-lexi.1", "1.6.5", "1", False), [])  # the first release
        self.assertEqual(release_tag.check("1.6.5-lexi.2-rc", "1.6.5", "2", True, latest="1.6.5-lexi.1"), [])
        self.assertEqual(release_tag.check("1.7.0-lexi.1", "1.7.0", "1", False, latest="1.6.5-lexi.4"), [])

    def test_a_leading_v_is_refused(self):
        self.assertTrue(release_tag.check("v1.6.5-lexi.2", "1.6.5", "2", False))

    def test_a_tag_not_newer_than_the_latest_is_refused(self):
        # A release number reset to 1 on the same [crosspoint] version.
        self.assertTrue(release_tag.check("1.6.5-lexi.1", "1.6.5", "1", False, latest="1.6.5-lexi.3"))
        self.assertTrue(release_tag.check("1.6.5-lexi.3", "1.6.5", "3", False, latest="1.6.5-lexi.3"))

    def test_bad_configuration_is_refused(self):
        self.assertTrue(release_tag.check("1.7.0rc-lexi.1", "1.7.0rc", "1", False))  # not X.Y.Z
        self.assertTrue(release_tag.check("1.6.5-lexi.0", "1.6.5", "0", False))      # lexi.0 is never offered
        self.assertTrue(release_tag.check("1.6.5-lexi.x", "1.6.5", "x", False))

    def test_the_prerelease_flag_must_match_the_tag(self):
        self.assertTrue(release_tag.check("1.6.5-lexi.2", "1.6.5", "2", True))
        self.assertTrue(release_tag.check("1.6.5-lexi.2-rc", "1.6.5", "2", False))

    def test_tags_fit_the_updaters_buffers(self):
        self.assertEqual(release_tag.MAX_TAG_LENGTH, 27)
        self.assertLessEqual(len("1.6.5-lexi.12-rc"), release_tag.MAX_TAG_LENGTH)
        self.assertTrue(release_tag.check("1000.1000.1000-lexi.100000-rc", "1000.1000.1000", "100000", True))

    def test_the_previous_release_is_the_newest_other_one(self):
        tags = ["1.6.5-lexi.3", "1.6.5-lexi.2", "1.6.5-lexi.1"]
        self.assertEqual(release_tag.previous_release(tags, "1.6.5-lexi.3"), "1.6.5-lexi.2")  # just published
        self.assertEqual(release_tag.previous_release(tags, "1.6.5-lexi.4"), "1.6.5-lexi.3")
        self.assertEqual(release_tag.previous_release(["", "1.6.5-lexi.1"], "1.6.5-lexi.1"), "")
        self.assertEqual(release_tag.previous_release([], "1.6.5-lexi.1"), "")
        # By version, not by list order (gh lists by date): an older tag made later doesn't hide lexi.3.
        self.assertEqual(release_tag.previous_release(["1.6.5-lexi.1", "1.6.5-lexi.3"], "1.6.5-lexi.2"), "1.6.5-lexi.3")
        self.assertEqual(release_tag.previous_release(["nightly", "1.6.5-lexi.1"], "1.6.5-lexi.2"), "1.6.5-lexi.1")

    def test_the_command_reads_the_releases_list(self):
        import io
        import unittest.mock
        version, release = release_tag.configured()
        tag = release_tag.expected_tag(version, release, False)
        newer = f"{version}-lexi.{int(release) + 1}"
        with unittest.mock.patch("sys.stdin", io.StringIO(f"{tag}\n{newer}\n")):
            self.assertEqual(release_tag.main(["check", tag, "--releases", "-"]), 1)  # an older tag than one out
        with unittest.mock.patch("sys.stdin", io.StringIO(f"{tag}\n")):
            self.assertEqual(release_tag.main(["check", tag, "--releases", "-"]), 0)

    def test_a_release_number_past_the_firmwares_limit_is_refused(self):
        self.assertTrue(release_tag.check("1.6.5-lexi.1000001", "1.6.5", "1000001", False))
        with open(os.path.join(REPO, "src/lexirise/ota/ReleaseVersion.cpp"), encoding="utf-8") as f:
            self.assertIn(f"kMaxVersionComponent = {release_tag.MAX_NUMBER};", f.read())

    def test_the_repo_is_releasable_as_configured(self):
        version, release = release_tag.configured()
        self.assertEqual(release_tag.check(release_tag.expected_tag(version, release, False), version, release, False),
                         [])
        self.assertEqual(release_tag.check(release_tag.expected_tag(version, release, True), version, release, True),
                         [])

    def test_the_marker_matches_the_firmware(self):
        with open(os.path.join(REPO, "src/lexirise/ota/ReleaseVersion.cpp"), encoding="utf-8") as f:
            src = f.read()
        self.assertIn(f'kLexiMarker = "{release_tag.LEXI_MARKER}"', src)
        with open(os.path.join(REPO, "platformio.ini"), encoding="utf-8") as f:
            ini = f.read()
        self.assertEqual(len(re.findall(re.escape(release_tag.LEXI_MARKER) + r"\$\{lexirise\.release\}", ini)), 3)
        with open(os.path.join(REPO, "lib/JsonParser/ReleaseJsonParser.h"), encoding="utf-8") as f:
            self.assertIn("char tagName[32];", f.read())
        with open(os.path.join(REPO, "src/network/OtaUpdater.cpp"), encoding="utf-8") as f:
            self.assertIn(f"char assetName[{release_tag.ASSET_NAME_BYTES}]", f.read())


if __name__ == "__main__":
    unittest.main()
