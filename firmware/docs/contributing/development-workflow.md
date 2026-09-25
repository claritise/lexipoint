# Development Workflow

Lexipoint's workflow is in the repo's `docs/v0.1/01-build-order.md` ("How to run"): each phase on a
`lexi/<phase-id>` branch, reviewed until clean, then merged into `main` together with its ledger row.

## Local checks

From `firmware/`:

```sh
./bin/clang-format-fix
pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run -e x4pro
pio run -e x4pro-gh_release
pio run -e x4pro-lexirise-off
cmake -S test -B build/test && cmake --build build/test -j && ctest --test-dir build/test -j
python3 -m unittest discover -s scripts/lexipoint -p 'test_*.py'
python3 scripts/lexipoint/keyscan.py
```

CI (`.github/workflows/ci.yml` at the repo root) runs the same. Use clang-format 21+ locally to match CI.
If `clang-format` is missing or too old locally, see [Getting Started](./getting-started.md).
