# px-patch-v1 — AI Instructions

TFD control-room patch / ventilation firmware for Paradox escape rooms.

## Status

Firmware **0.13** on Patch32Prop (`.52`). I/O scan + fans; GM `solve` publishes
suite `{event:"solved"}` (does **not** invent A/B chain matching on the ESP).
Config may store up to 8 named target-chain strings for the Live overlay.

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
- Do **not** put A/B chain solutions on the ESP; GM `solve` only publishes solved.

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
| `/Paradox/TFD/Patch/Prop/Commands` | `fansOn` / `fansOff` / `reportState` / GM `solve` / `reset` |
| `/Paradox/TFD/Patch/Prop/Events` | `{Chains}`, `{AllTilesPresent}`, `{event:"solved"}` |
| `/Paradox/Props` | heartbeat id `Patch32Prop` |

Broker is **`.132`**. Accept `command` as an alias of `Command`; publish PascalCase.
**I/O scan stays dumb** — GM solve only emits the suite solved event.

## Jack map

Config stores `gridRows`, `gridCols`, and `jacks: [{port,row,col}, …]`
(display-only; not used by the scan). Default is the 2023 Crafty Fox
panel (`#00 = R1C1`, `#01 = R3C3`, …). Edit on Config if a wire moves;
Apply + Save writes `/spiffs/config.json`. Live still draws the existing
4×6 / 2×3 tile graphic — ports outside R1–R4 / C1–C6 are kept but not
drawn. Archive firmware has no row/column table.

## Other conventions

- Do not edit `props/esp32/archive/patch32` or `tfd-old` runtime.
- Version bump default `+0.01`.
- Path-relative assets + `lib_http_proxy` when serving embedded UI.
- Hardware must not block boot: `hwOk` / `hwFault` + red `.hw-banner`.
  Shared contract: [../px-components/docs/hw-fault.md](../px-components/docs/hw-fault.md).

## Suite standards

Public suite brief + contracts: [../../../apps/PxH/docs/standards/](../../../apps/PxH/docs/standards/).
