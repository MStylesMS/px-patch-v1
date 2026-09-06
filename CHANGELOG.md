# Changelog

All notable changes to px-patch-v1 are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Version numbers correspond to the contents of `version.txt`.

## [0.11] - 2026-09-05

### Added

- Device config now stores the jack map as MCP port → `R#C#` (`#00 = R1C1`)
  plus `gridRows` / `gridCols`. Default is the 2023 Crafty Fox panel. Apply +
  Save persists it on the ESP; Live drawing stays the existing 4×6 diagram.

## [0.10] - 2026-09-05

### Changed

- Default jack map assigns MCP 0–F column-major (down each R1–R4 column,
  then the next column to the right), matching how the panel was wired.
  Same 16 sockets; only the port numbers moved.

## [0.09] - 2026-09-05

### Added

- Config **Learn jack map**: keep one end in a home node (R1C1), click the
  physical node when a 2-port chain appears. Live jack labels are now R#C#
  (6 columns across the three plates), not MCP hex.

## [0.08] - 2026-09-05

### Fixed

- Scan now requires a HIGH→LOW edge (double-read) so a pin that is slow to
  rise after being driven is not treated as connected to every later pin.
  That ghost star was published as `0123456` and drawn as six Live segments
  for a single jumper.
- Live / Monitor poll 400 ms (was 2 s).

## [0.07] - 2026-09-05

### Fixed

- MCP23S17 init now forces BANK=0 (write IOCON at 0x05 and 0x0A), enables SEQOP,
  keeps pull-ups on undriven pins, and scans at 1 MHz. Empty `{Chains}` / idle
  Live board with a real jumper was the expander left in a leftover BANK after
  OTA (this board has no RST).

## [0.05] - 2026-09-04

### Fixed

- Dropdown text is black on white so the closed control and the native option
  list stay readable.

## [0.04] - 2026-09-04

### Fixed

- Dropdown (`select`) text is dark navy on a light field so options stay
  readable against the Windows native popup.

## [0.03] - 2026-09-04

### Added

- Config stores up to 8 named target-chain sets (display-only Live overlays;
  the prop still does not match or solve). Defaults seed Crafty Fox A/B.

### Fixed

- Live board now derives cable groups from `{Chains}` so the 2×3 panel renders
  on-device (firmware `/api/state` has no `groups` array).
- Patch Panel overflow so the full tile graphic stays in view.

## [0.02] - 2026-09-04

### Added

- First on-device firmware: MCP23S17 HSPI scan, tile GPIO, fan relay, legacy
  Prop MQTT (`Command`/`command`, `{Chains}`, `{AllTilesPresent}`), Signal Glass
  HTTP console.
- Factory Connect defaults for Crafty Fox: STA `Paradox-TFD-1`, broker
  `192.168.8.132`, SoftAP `Paradox-PXPatchV1-XXXX` as recovery.
- Fixes vs archive: `reportState` tile polarity, tile debounce reset, unique
  3+ daisy encoding. I/O only — no A/B solutions on the ESP.

## [0.01] - 2026-09-03

### Added

- Signal Glass prop console (Live / Config / Monitor / Connect / OTA) copied from
  px-valve-v1 / px-wifi-v1 and specialized for the patch / vent panel.
- Live 2×3 silver-tile graphic with drooping patch cables (target dashed amber,
  current red / solved green). Jack map is provisional — firmware has no
  row/column table; remap on Config.
- Dummy-data scenarios on port **8093**. No firmware yet.
