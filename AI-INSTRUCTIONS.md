# px-patch-v1 — AI Instructions

TFD control-room patch / ventilation firmware for Paradox escape rooms.

## Status

Firmware **0.05** on Patch32Prop (`.52`). I/O only: scan 16 ports, report
`{Chains}` / `{AllTilesPresent}`, drive fans. Config may store up to 8 named
target-chain strings for the Live overlay; do **not** match or solve on the ESP.

## Firmware

- Target: classic **esp32**, IDF **6.0.3**, flash **4MB**, version from `version.txt`.
- Artifact: `build/px-patch-v1.bin` (OTA payload).
- Default STA: `Paradox-TFD-1` / `tfd1-jr6t`; broker `192.168.8.132` (baked so
  first boot after OTA rejoins the trailer LAN; SoftAP remains as recovery).
- OTA: browser `update.html`, or
  `.\scripts\ota_upload.ps1 -HostAddress 192.168.8.52` (new),
  `.\scripts\ota_upload.ps1 -HostAddress 192.168.8.52 -Legacy` (old patch32 `POST /ota`).
- SoftAP SSID form: `Paradox-PXPatchV1-XXXX`.
- Default mDNS hostname: **`patch.local`** (`networkName`; change via Connect or `POST /api/device/name`).
- **I/O only.** Do not put A/B solutions on the ESP.

Local UI preview without flash:

```powershell
.\scripts\serve_webui.ps1
```

→ http://127.0.0.1:8093/index.html (wifi-v1 = 8090, fuse = 8091, valve = 8092).

Scenario mocks: http://127.0.0.1:8093/samples.html

Admin UI chrome: [docs/console-chrome.md](docs/console-chrome.md) (must work on
phone / tablet / desktop — see Responsive section). Plan:
[rooms/tfd/docs/ESP32-PATCH-PLAN.md](../../../rooms/tfd/docs/ESP32-PATCH-PLAN.md).

## MQTT (legacy Prop)

| Topic | Payload |
|-------|---------|
| `/Paradox/TFD/Patch/Prop/Commands` | `fansOn` / `fansOff` / `reportState` |
| `/Paradox/TFD/Patch/Prop/Events` | `{Chains}`, `{AllTilesPresent}` |
| `/Paradox/Props` | heartbeat id `Patch32Prop` |

Broker is **`.132`**. Accept `command` as an alias of `Command`; publish PascalCase.

## Jack map

Archive firmware has **no** row/column table — ports are MCP 0–15 in scan
order. The Live graphic uses a provisional 2×3 tile layout (TL / BL / TR / BR
per tile). Remap on Config after comparing to the physical panel.

## Other conventions

- Do not edit `props/esp32/archive/patch32` or `tfd-old` runtime.
- Version bump default `+0.01`.
- Path-relative assets + `lib_http_proxy` when serving embedded UI.

## Suite standards

Public suite brief + contracts: [../../../apps/PxH/docs/standards/](../../../apps/PxH/docs/standards/).
