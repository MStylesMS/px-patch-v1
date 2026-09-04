# px-patch-v1

ESP32 patch / ventilation-tile firmware — Paradox TFD control room.

## Status (2026-09-04)

Firmware **0.02** on the live Patch32Prop (`192.168.8.52`). I/O only: report
`{Chains}` / `{AllTilesPresent}`, drive fans. A/B solutions stay in `control.js`.

Build: `idf.py build` (ESP-IDF 6.0.3, target esp32). OTA:

```powershell
.\scripts\ota_upload.ps1 -HostAddress 192.168.8.52 -Legacy   # archive POST /ota
.\scripts\ota_upload.ps1 -HostAddress 192.168.8.52           # new /api/ota/upload
```

Default STA is `Paradox-TFD-1`; broker `192.168.8.132`.

The ESP is I/O only: report `{Chains}` / `{AllTilesPresent}`, drive fans.
A/B solutions stay in `control.js`.

## License

Dual-licensed:

- **AGPL-3.0** for open source use — see [LICENSE](LICENSE).
- **Commercial license required** for proprietary or revenue-generating use that does not comply with AGPL-3.0 — see [COMMERCIAL.md](COMMERCIAL.md).

Copyright © 2026 Mark Stevens.

## Local UI preview

```powershell
.\scripts\serve_webui.ps1
```

- http://127.0.0.1:8093/index.html — Live (tile panel + cables)
- http://127.0.0.1:8093/config.html — Config (scan + jack map)
- http://127.0.0.1:8093/monitor.html — Monitor (16 ports)
- http://127.0.0.1:8093/connection.html — Connect
- http://127.0.0.1:8093/samples.html — dummy-data scenarios

`localhost` enables demo mocks automatically. `?demo=1` / `?demo=0` override.
`?scenario=nominal|empty|partial|solved-a|solved-b|daisy|tiles-missing|fans-on`

Port **8093** so it does not collide with px-wifi-v1 (8090), px-fuse-v1 (8091),
or px-valve-v1 (8092).

## Firmware

Pin ESP-IDF 6.0.x, `idf.py set-target esp32`, `EXTRA_COMPONENT_DIRS` →
`../px-components`. See [docs/console-chrome.md](docs/console-chrome.md) and
[rooms/tfd/docs/ESP32-PATCH-PLAN.md](../../rooms/tfd/docs/ESP32-PATCH-PLAN.md).
