# Lexipoint

E-reader firmware for the **Xteink X4 Pro** that looks words up as you read. Long-press a word in a Japanese
or Simplified Chinese book: a card shows its reading, meaning and frequency from
[Lexirise](https://lexirise.app), and one tap saves it to your Lexirise account at the level you choose.
Offline, the StarDict dictionaries on the SD card answer instead.

**Device:** the Xteink X4 Pro is the only supported device. Other touchscreen e-readers the FreeInk SDK
supports may follow; devices without touch won't (`docs/v0.1/standalone-repo.md`, D20).

**Status:** v0.1 in development. No release yet.

## Use it

[`docs/user-guide.md`](docs/user-guide.md): installing, the font Japanese and Chinese need, your Lexirise key,
the lookup card, and the settings.

## Build it

```sh
git clone --recursive https://github.com/claritise/lexipoint
cd lexipoint/firmware
pio run -e x4pro                                    # the dev build
pio run -e x4pro -t upload                          # flash it
cmake -S test -B build/test && cmake --build build/test -j && ctest --test-dir build/test -j
```

[`firmware/docs/contributing/`](firmware/docs/contributing/README.md) covers the toolchain and the code the
firmware is built on. The checks every change passes are in
[`firmware/docs/contributing/development-workflow.md`](firmware/docs/contributing/development-workflow.md).

## What's here

| Folder | |
|---|---|
| `firmware/` | The firmware (PlatformIO). Lexipoint's own code is in `src/lexirise/`, `test/lexirise_*/` and `scripts/lexipoint/`; the rest is the base it's built on |
| `docs/` | Design, decisions and the build ledger (`docs/v0.1/00-overview.md`, `docs/v0.1/01-build-order.md`) |
| `tools/` | Tools that run on a computer (the manga converter, `docs/v0.2/manga.md`) |

## Credits

Lexipoint is built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) (MIT,
© 2025 Dave Allie) and the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) (MIT, © 2026 FreeInk).
See [`NOTICE`](NOTICE). CrossPoint's license is in [`firmware/LICENSE`](firmware/LICENSE).
