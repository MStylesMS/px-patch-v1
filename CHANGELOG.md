# Changelog

All notable changes to px-patch-v1 are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Version numbers correspond to the contents of `version.txt`.

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
