# Prop console chrome — px-patch-v1

Copied from [px-valve-v1/docs/console-chrome.md](../../px-valve-v1/docs/console-chrome.md) /
[px-wifi-v1/docs/console-chrome.md](../../px-wifi-v1/docs/console-chrome.md), then
specialized for the patch panel. Theme: **Signal Glass**.

## Pages

| Page | File | Job |
|------|------|-----|
| Live | `index.html` | 2×3 silver tiles, four jacks each, drooping cables. Target overlay (display-only A/B). Fans / tiles / reportState. |
| Config | `config.html` | Scan / debounce / settle / heartbeat / HTTP Debug. Jack map (row, col, corner → MCP port). No on-device A/B solutions. |
| Monitor | `monitor.html` | 16-port strip, parsed chains, tile GPIO vs published, fan relay, MCP addr 0. |
| Connect | `connection.html` | Wi-Fi, MQTT (default broker `.132`), mDNS/identity, OTA link |
| OTA | `update.html` | Firmware upload from Connect |
| Samples | `samples.html` | Dummy-data scenarios for UI review (not shipped later) |

Tab order: **Live, Config, Monitor, Connect**.

## Look

Same CSS tokens as px-wifi-v1 Signal Glass. Patch extras live at the bottom of
`styles.css` (tile board, cables, jack map).


## Responsive (required)

Must work on phone (~390px), tablet (~768px), and desktop. Shared chrome rules
live in `styles.css` and are documented in
[px-wifi-v1/docs/console-chrome.md](../../px-wifi-v1/docs/console-chrome.md):

- `max-width: 820px` � stack `.layout` **and** `.layout.live-layout`; wrap tabs
- `max-width: 520px` � phone padding / single-column metrics; prop-specific grids

Do not ship UI changes without checking those widths. Bootstrap is optional.

## Local UI iteration (no flash)

```powershell
# from px-patch-v1/
.\scripts\serve_webui.ps1
```

http://127.0.0.1:8093/index.html — demo mocks auto-on for localhost.

Scenario query: `?scenario=nominal|empty|partial|solved-a|solved-b|daisy|tiles-missing|fans-on`
