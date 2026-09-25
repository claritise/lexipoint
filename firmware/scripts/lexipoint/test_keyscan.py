"""keyscan.py: the key-leak scan (01-build-order.md, uniform gate 5)."""

import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import keyscan  # noqa: E402


# Built at run time, so this file itself holds no key-shaped string for the scan to find.
REAL_LOOKING = "lx" + "_a8F3kq92LmZx0PqR7tYv"


class KeyScan(unittest.TestCase):
    def test_synthetic_test_keys_pass(self):
        lines = [
            'test/a.cpp:1:const std::string kKey = "lx_TESTKEYtestkey0123456789";',
            'test/b.cpp:2:  client.configure(kBase, "lx_OTHERkeyOTHERkey000000");',
            'scripts/t.py:3:key = "lx_FULLSECRETKEY0000abc"',
            'docs/x.md:4:api_key=lx_YOUR_KEY_HERE',
        ]
        self.assertEqual(keyscan.leaks(lines), [])

    def test_a_real_looking_key_is_caught(self):
        line = 'src/x.cpp:9:constexpr const char* k = "' + REAL_LOOKING + '";'
        self.assertEqual(keyscan.leaks([line]), [line])

    def test_one_real_key_among_synthetic_ones_is_caught(self):
        line = 'x:1:"lx_TESTKEYtestkey01" "' + REAL_LOOKING + '"'
        self.assertEqual(keyscan.leaks([line]), [line])

    def test_keys_with_underscores_and_dashes_are_caught(self):
        for key in ("lx" + "_ab12-cd34ef56", "lx" + "_ab_cd12EF34gh"):
            line = f'x:1:"{key}"'
            self.assertEqual(keyscan.leaks([line]), [line], key)

    def test_the_prefix_matches_the_firmware(self):
        repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
        with open(os.path.join(repo, "src/lexirise/LexiriseConfig.h"), encoding="utf-8") as f:
            self.assertIn(f'kApiKeyPrefix = "{keyscan.KEY_PREFIX}";', f.read())

    def test_it_scans_the_whole_repo_from_a_subfolder(self):
        here = os.path.dirname(os.path.abspath(__file__))
        repo = os.path.join(here, "..", "..")
        from_root = keyscan.key_shaped_lines(repo)
        self.assertEqual(keyscan.key_shaped_lines(here), from_root)
        self.assertTrue(any(line.startswith("test/") for line in from_root))  # outside this folder

    def test_the_repo_is_clean(self):
        repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
        out = subprocess.run([sys.executable, os.path.abspath(keyscan.__file__)], cwd=repo, capture_output=True,
                             text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)


if __name__ == "__main__":
    unittest.main()
