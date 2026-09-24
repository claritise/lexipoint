"""The card bench's fixtures are the binding reference's own data (gen_bench_fixtures.py)."""

import pathlib
import unittest

import gen_bench_fixtures

DOCS = pathlib.Path.home() / "Projects/lexipoint/docs/v0.1/reference/card-reference.html"


@unittest.skipUnless(DOCS.exists(), "the docs repo isn't checked out next to the firmware")
class FixturesMatchTheReference(unittest.TestCase):
    def test_committed_fixtures_are_regenerated_from_the_reference(self):
        import re
        squash = lambda s: re.sub(r"\s+", "", s)
        self.assertEqual(squash(gen_bench_fixtures.generate(DOCS.read_text())),
                         squash(gen_bench_fixtures.OUT.read_text()))


if __name__ == "__main__":
    unittest.main()
