# Canonical version constants for the .cpfont binary format and font manifest.
#
# These are the single source of truth for the build tooling. The CI workflow
# (release-fonts.yml) and both Python scripts (fontconvert_sdcard.py,
# generate-font-manifest.py) read from here.
#
# The firmware's SdCardFont.h carries its own copy of CPFONT_VERSION, bumped
# by hand when the firmware supports a new format. (Lexipoint removed the
# device's font download, which carried the manifest version, in v0.2 V8.)

# .cpfont binary format version. Bump when the on-disk struct layout changes.
CPFONT_VERSION = 4

# JSON manifest schema version. Bump when the manifest shape changes.
FONTS_MANIFEST_VERSION = 1
