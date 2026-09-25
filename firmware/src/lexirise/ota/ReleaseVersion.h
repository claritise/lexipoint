#pragma once

// Lexipoint's release versions (firmware-base.md §6): `<base>-lexi.<n>`, e.g. `1.6.5-lexi.2`, so the
// OTA updater offers Lexipoint's next release and never CrossPoint's (whose `1.6.6` would uninstall Lexipoint).
// Pure; tests: test/lexirise_ota.

#include <optional>
#include <string_view>

namespace lexipoint::ota {

struct ReleaseVersion {
  int major = 0;
  int minor = 0;
  int patch = 0;
  int lexi = 0;     // Lexipoint's release on that CrossPoint version; 0: not a Lexipoint version
  bool rc = false;  // a release candidate: "-rc" ("-lexi.2-rc+abc1234"), or CrossPoint's "1.6.5rc"
};

// "1.6.5-lexi.2", with an optional leading "v" and anything after the numbers ("-x4pro" on a dev build,
// "-rc+abc1234" on a release candidate). nullopt when it doesn't start with three numbers.
std::optional<ReleaseVersion> parseReleaseVersion(std::string_view text);

// Whether `latest` (a release tag) is newer than the running firmware (`current`, CROSSPOINT_VERSION):
// only a Lexipoint version (`-lexi.<n>`) is ever offered, compared by CrossPoint version then `n`; on a
// release candidate the release it leads to counts as newer.
bool isNewerRelease(std::string_view latest, std::string_view current);

}  // namespace lexipoint::ota
