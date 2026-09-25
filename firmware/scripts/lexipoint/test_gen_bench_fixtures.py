"""The card bench's fixtures are the binding reference's own data (gen_bench_fixtures.py)."""

import unittest

import gen_bench_fixtures

DOCS = gen_bench_fixtures.DOCS / "reference/card-reference.html"  # in the same checkout: never skipped


class FixturesMatchTheReference(unittest.TestCase):
    def test_committed_fixtures_are_regenerated_from_the_reference(self):
        import re
        squash = lambda s: re.sub(r"\s+", "", s)
        self.assertEqual(squash(gen_bench_fixtures.generate(DOCS.read_text())),
                         squash(gen_bench_fixtures.OUT.read_text()))


if __name__ == "__main__":
    unittest.main()
