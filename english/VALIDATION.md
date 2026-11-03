# English package checks — 1 November 2026

This file records software checks of the English build package. It does not report a new physical
assembly or a new hardware test of the translated copy.

## Programs and scripts

- Both ESP32-S3 programs compile with **esp32:esp32 3.3.11** and **GFX Library for
  Arduino 1.6.7**, OPI PSRAM, 16 MB flash and the 3 MB application / 9.9 MB FATFS partition scheme.
- Executable C++ tokens are compared with the French originals, allowing the intended updates to
  file paths in help text. The serial commands, speech, screen words and French safeguard remain
  in their original language.
- Python, shell and JSON/manifest files pass syntax checks.
- Mocked transport checks exercise the `head`, `torso`, `tete` and `torse` normal-flash arguments,
  plus the two English board arguments for the UNO Q wrapper. These checks do not contact a robot
  or remote computer and do not upload firmware.

## CAD and files

- The English head/torso and limb generators were run successfully.
- All **23** print STLs pass the supplied mesh checks: valid closed meshes with positive volume,
  the expected component count, and the lowest point within **0.01 mm** of the print bed.
  The torso body's detachable print supports account for its additional mesh component.
- Local Markdown links resolve within the package (the guide itself is on
  Instructables).
- `illustrations/preview_en_torso.png` and `preview_en_clock.png` are previews with English words, drawn on a computer
  by the firmware's own drawing code. With the French words, the same drawing matched the supplied French timer screen
  exactly and the clock face to within 1/255 per colour channel. The firmware in this package still shows French.
- The package is checked for private household files, machine-specific paths and external symlinks.
  `FILE_MAP.json` intentionally records the old French paths alongside their English equivalents.

## Checks not performed on this copy

The Fusion scripts were checked as source files and their dependencies are supplied, but the
English assembly was not constructed interactively inside Fusion. Optional Blender rendering,
Mac service installation, live voice conversation and a new physical assembly were not performed
for this translation. Use the component measurements and test-fitting instructions in the guide.

## Guide review

- All 33 source excerpts in the guide match the supplied programs, ignoring comment wording and
  whitespace. The household-profile block is an example for the reader to create.
- Command blocks pass shell syntax checks. Relative links and referenced supplied paths resolve;
  the personal files are deliberately absent and have creation instructions.
- The Step 12 WAV-transfer example was exercised against a simulated serial port: file bytes and
  checksum matched, and unsupported stereo input was rejected before opening a port.
- Fusion and board documentation, Ollama's pinned release and the Piper HTTP installation
  instructions were checked against their publishers' pages. The complete Mac installation was
  not repeated in a fresh environment.

Before publication, the author still needs to supply the public download link and choose the
code/CAD licence mentioned at the end of the guide.
