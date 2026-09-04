(function () {
    const KEY_BASE = "px.api.base";
    const KEY_DEMO = "px.demo.mode";
    const KEY_CFG = "px.patch.demo.config.v2";
    const KEY_OVERLAY = "px.patch.overlay";

    const SOLUTIONS = {
        A: "09,28,3E,4C",
        B: "1D,23,56,7A"
    };

    const TARGET_SET_COUNT = 8;
    const TARGET_LABELS = "ABCDEFGH";

    function defaultTargetSets() {
        return Array.from({ length: TARGET_SET_COUNT }, (_, i) => {
            const label = TARGET_LABELS.charAt(i);
            let chains = "";
            if (label === "A") {
                chains = SOLUTIONS.A;
            } else if (label === "B") {
                chains = SOLUTIONS.B;
            }
            return { label: label, chains: chains };
        });
    }

    function sanitizeTargetLabel(value) {
        return String(value == null ? "" : value).replace(/[^\x20-\x7E]/g, "").replace(/["\\]/g, "").slice(0, 16);
    }

    function sanitizeTargetChains(value) {
        return String(value == null ? "" : value).toUpperCase().replace(/[^0-9A-F,]/g, "").slice(0, 47);
    }

    function normalizeTargetSets(raw) {
        const src = Array.isArray(raw) ? raw : [];
        const defaults = defaultTargetSets();
        return defaults.map((d, i) => {
            const s = src[i] || {};
            const label = sanitizeTargetLabel(s.label != null ? s.label : d.label) || d.label;
            const chains = sanitizeTargetChains(s.chains != null ? s.chains : d.chains);
            return { label: label, chains: chains };
        });
    }

    const CORNERS = ["tl", "bl", "tr", "br"];
    const CORNER_LABEL = { tl: "TL", bl: "BL", tr: "TR", br: "BR" };

    function defaultJackMap() {
        const skip = {
            "0,0,br": true,
            "0,1,br": true,
            "0,2,br": true,
            "1,0,br": true,
            "1,1,bl": true,
            "1,1,br": true,
            "1,2,bl": true,
            "1,2,br": true
        };
        const slots = [];
        let port = 0;
        for (let row = 0; row < 2; row++) {
            for (let col = 0; col < 3; col++) {
                CORNERS.forEach((corner) => {
                    const key = row + "," + col + "," + corner;
                    const live = !skip[key] && port < 16;
                    slots.push({
                        row: row,
                        col: col,
                        corner: corner,
                        port: live ? port++ : null
                    });
                });
            }
        }
        return slots;
    }

    const DEFAULT_CONFIG = {
        scanPeriodMs: 110,
        debounceCount: 3,
        settleMs: 5,
        heartbeatInterval: 10000,
        debug: true,
        jackMap: defaultJackMap(),
        targetSets: defaultTargetSets()
    };

    function el(id) {
        return document.getElementById(id);
    }

    function nowIso() {
        return new Date().toISOString();
    }

    function hexdig(n) {
        return Number(n).toString(16).toUpperCase();
    }

    function isServedOverHttp() {
        return window.location.protocol === "http:" || window.location.protocol === "https:";
    }

    function getApiBase() {
        if (isServedOverHttp()) {
            return window.location.origin;
        }
        return localStorage.getItem(KEY_BASE) || "http://192.168.4.1";
    }

    function queryParam(name) {
        try {
            return new URLSearchParams(window.location.search).get(name);
        } catch {
            return null;
        }
    }

    function queryDemoOverride() {
        const q = queryParam("demo");
        if (q === "1" || q === "true") {
            return true;
        }
        if (q === "0" || q === "false") {
            return false;
        }
        return null;
    }

    function isLocalPreviewHost() {
        const h = window.location.hostname;
        return h === "127.0.0.1" || h === "localhost" || h === "[::1]";
    }

    function getDemoMode() {
        const q = queryDemoOverride();
        if (q !== null) {
            return q;
        }
        if (isLocalPreviewHost()) {
            return true;
        }
        return localStorage.getItem(KEY_DEMO) === "1";
    }

    const SCENARIOS = ["nominal", "empty", "partial", "solved-a", "solved-b", "daisy", "tiles-missing", "fans-on"];

    function getScenario() {
        const s = (queryParam("scenario") || "nominal").toLowerCase();
        return SCENARIOS.indexOf(s) >= 0 ? s : "nominal";
    }

    function normalizeApiPath(path) {
        return path.startsWith("/") ? path.slice(1) : path;
    }

    function resolveApiUrl(path) {
        const rel = normalizeApiPath(path);
        if (!isServedOverHttp()) {
            return getApiBase().replace(/\/$/, "") + "/" + rel;
        }
        try {
            return new URL(rel, document.baseURI || window.location.href).href;
        } catch {
            return "/" + rel;
        }
    }

    function mergeJackMap(saved) {
        const base = defaultJackMap();
        if (!Array.isArray(saved)) {
            return base;
        }
        return base.map((slot) => {
            const hit = saved.find((s) => s && s.row === slot.row && s.col === slot.col && s.corner === slot.corner) || {};
            const port = hit.port;
            const n = port === "" || port === null || port === undefined ? null : Number(port);
            return {
                row: slot.row,
                col: slot.col,
                corner: slot.corner,
                port: n === null || Number.isNaN(n) || n < 0 || n > 15 ? null : n
            };
        });
    }

    function loadDemoConfig() {
        let raw = {};
        try {
            raw = JSON.parse(localStorage.getItem(KEY_CFG) || "{}");
        } catch {
            raw = {};
        }
        return {
            scanPeriodMs: Number(raw.scanPeriodMs) || DEFAULT_CONFIG.scanPeriodMs,
            debounceCount: Number(raw.debounceCount) || DEFAULT_CONFIG.debounceCount,
            settleMs: Number(raw.settleMs) || DEFAULT_CONFIG.settleMs,
            heartbeatInterval: Number(raw.heartbeatInterval) || DEFAULT_CONFIG.heartbeatInterval,
            debug: raw.debug != null ? Boolean(raw.debug) : true,
            jackMap: mergeJackMap(raw.jackMap),
            targetSets: normalizeTargetSets(raw.targetSets)
        };
    }

    function saveDemoConfig(cfg) {
        demoConfig = {
            scanPeriodMs: Number(cfg.scanPeriodMs) || 110,
            debounceCount: Number(cfg.debounceCount) || 3,
            settleMs: Number(cfg.settleMs) || 5,
            heartbeatInterval: Number(cfg.heartbeatInterval) || 10000,
            debug: Boolean(cfg.debug),
            jackMap: mergeJackMap(cfg.jackMap),
            targetSets: normalizeTargetSets(cfg.targetSets)
        };
        localStorage.setItem(KEY_CFG, JSON.stringify(demoConfig));
    }

    let demoConfig = loadDemoConfig();

    function parseChains(str) {
        if (!str) {
            return [];
        }
        return String(str).toUpperCase().split(",").filter(Boolean).map((group) => {
            return group.split("").map((ch) => parseInt(ch, 16)).filter((n) => !Number.isNaN(n));
        }).filter((g) => g.length >= 2);
    }

    function formatChain(ports) {
        return ports.map(hexdig).join("");
    }

    function uniqueEncoding(groups) {
        const seen = {};
        return groups.map((g) => {
            const key = g.slice().sort((a, b) => a - b).join("-");
            if (seen[key]) {
                return null;
            }
            seen[key] = true;
            const ordered = g.slice().sort((a, b) => a - b);
            return formatChain(ordered);
        }).filter(Boolean).join(",");
    }

    function edgeKey(a, b) {
        return a < b ? a + "-" + b : b + "-" + a;
    }

    function edgeSet(groups) {
        const set = {};
        groups.forEach((g) => {
            const ordered = sortPortsVisual(g);
            for (let i = 0; i < ordered.length - 1; i++) {
                set[edgeKey(ordered[i], ordered[i + 1])] = true;
            }
            if (g.length === 2) {
                set[edgeKey(g[0], g[1])] = true;
            }
        });
        return set;
    }

    function slotForPort(port) {
        return demoConfig.jackMap.find((s) => s.port === port) || null;
    }

    function visualRank(port) {
        const s = slotForPort(port);
        if (!s) {
            return port * 10;
        }
        const cornerRank = { tl: 0, tr: 1, bl: 2, br: 3 };
        return s.row * 100 + s.col * 10 + (cornerRank[s.corner] || 0);
    }

    function sortPortsVisual(ports) {
        return ports.slice().sort((a, b) => visualRank(a) - visualRank(b));
    }

    function jackXY(slot, box) {
        const tw = box.tileW;
        const th = box.tileH;
        const x0 = box.originX + slot.col * (tw + box.gapX);
        const y0 = box.originY + slot.row * (th + box.gapY);
        const insetX = tw * 0.28;
        const insetY = th * 0.30;
        const local = {
            tl: [insetX, insetY],
            tr: [tw - insetX, insetY],
            bl: [insetX, th - insetY],
            br: [tw - insetX, th - insetY]
        };
        const p = local[slot.corner];
        return { x: x0 + p[0], y: y0 + p[1], slot: slot };
    }

    const BOARD = {
        w: 980,
        h: 720,
        originX: 36,
        originY: 40,
        tileW: 292,
        tileH: 292,
        gapX: 28,
        gapY: 28
    };

    function qPoint(p0, c, p1, t) {
        const u = 1 - t;
        return {
            x: u * u * p0.x + 2 * u * t * c.x + t * t * p1.x,
            y: u * u * p0.y + 2 * u * t * c.y + t * t * p1.y
        };
    }

    function approachJack(p0, c, p1, o) {
        let best = Infinity;
        let bestP = p0;
        for (let t = 0.06; t <= 0.94; t += 0.02) {
            const p = qPoint(p0, c, p1, t);
            const d = Math.hypot(p.x - o.x, p.y - o.y);
            if (d < best) {
                best = d;
                bestP = p;
            }
        }
        return { d: best, p: bestP, o: o };
    }

    function isSameJack(p, q) {
        if (!p || !q || !p.slot || !q.slot) {
            return p === q;
        }
        return p.slot.row === q.slot.row && p.slot.col === q.slot.col && p.slot.corner === q.slot.corner;
    }

    function cablePath(a, b, avoid) {
        const obstacles = avoid.filter((p) => !isSameJack(p, a) && !isSameJack(p, b));
        const GOOD = 60;

        function clearanceOf(p0, c, p1) {
            let best = Infinity;
            obstacles.forEach((o) => {
                if (isSameJack(o, p0) || isSameJack(o, p1)) {
                    return;
                }
                const hit = approachJack(p0, c, p1, o);
                if (hit.d < best) {
                    best = hit.d;
                }
            });
            return best;
        }

        function bestQuad(p0, p1) {
            const dx = p1.x - p0.x;
            const dy = p1.y - p0.y;
            const dist = Math.hypot(dx, dy) || 1;
            const mx = (p0.x + p1.x) / 2;
            const my = (p0.y + p1.y) / 2;
            const isVert = Math.abs(dx) < 48;
            const desiredDroop = Math.min(Math.max(18, dist * 0.16), 88);
            let best = { cx: mx, cy: my + 12, score: -1e9, clear: 0 };

            function consider(cx, cy) {
                cx = Math.max(24, Math.min(BOARD.w - 24, cx));
                cy = Math.max(24, Math.min(BOARD.h - 24, cy));
                const clear = clearanceOf(p0, { x: cx, y: cy }, p1);
                const droop = cy - my;
                const score = (clear >= GOOD ? 100000 : 0) +
                    (clear >= GOOD ? 0 : clear * 100) +
                    Math.min(Math.max(droop, 0), desiredDroop) * 0.35 -
                    Math.abs(cx - mx) * 0.12;
                if (score > best.score) {
                    best = { cx: cx, cy: cy, score: score, clear: clear };
                }
            }

            const sways = isVert ? [36, 56, 80, 108, 140] : [0, 28, 52, 80, 112, 148];
            const droops = isVert ? [0, 8, 18, 32, 48] : [-40, -24, -10, 0, 10, 22, 36, 52, 72, 96, 124, 156];
            sways.forEach((s) => {
                [-1, 1].forEach((dir) => {
                    if (s === 0 && dir < 0) {
                        return;
                    }
                    droops.forEach((d) => {
                        consider(mx + dir * s, my + d);
                    });
                });
            });
            return best;
        }

        function qPart(p0, q, p1, withMove) {
            const tail = "Q " + q.cx.toFixed(1) + " " + q.cy.toFixed(1) +
                " " + p1.x.toFixed(1) + " " + p1.y.toFixed(1);
            if (withMove) {
                return "M " + p0.x.toFixed(1) + " " + p0.y.toFixed(1) + " " + tail;
            }
            return tail;
        }

        const single = bestQuad(a, b);
        if (single.clear >= GOOD) {
            return qPart(a, single, b, true);
        }

        let bestVia = { clear: single.clear, score: single.clear, path: qPart(a, single, b, true), w: null };
        const ys = [280, 310, 340, 370, 400, 470, 500, 530];
        for (let wx = 140; wx <= 820; wx += 40) {
            for (let yi = 0; yi < ys.length; yi++) {
                const wy = ys[yi];
                const w = { x: wx, y: wy };
                if (Math.hypot(w.x - a.x, w.y - a.y) < 90 || Math.hypot(w.x - b.x, w.y - b.y) < 90) {
                    continue;
                }
                let jackHit = Infinity;
                obstacles.forEach((o) => {
                    const d = Math.hypot(o.x - w.x, o.y - w.y);
                    if (d < jackHit) {
                        jackHit = d;
                    }
                });
                if (jackHit < 48) {
                    continue;
                }
                const c = { x: (a.x + w.x) / 2, y: (a.y + w.y) / 2 + 12 };
                const c2 = { x: (w.x + b.x) / 2, y: (w.y + b.y) / 2 + 12 };
                const clear = Math.min(clearanceOf(a, c, w), clearanceOf(w, c2, b), jackHit);
                const corridor = (wy >= 290 && wy <= 410 && clear >= 52) ? 80 : 0;
                const score = clear + corridor;
                if (score > bestVia.score) {
                    bestVia = { clear: clear, score: score, w: w };
                }
            }
        }
        if (bestVia.w) {
            const q1 = bestQuad(a, bestVia.w);
            const q2 = bestQuad(bestVia.w, b);
            return qPart(a, q1, bestVia.w, true) + " " + qPart(bestVia.w, q2, b, false);
        }
        return bestVia.path;
    }

    function portPoints() {
        const pts = {};
        demoConfig.jackMap.forEach((slot) => {
            if (slot.port == null) {
                return;
            }
            pts[slot.port] = jackXY(slot, BOARD);
        });
        return pts;
    }

    function segmentsForGroups(groups, pts) {
        const segs = [];
        groups.forEach((g, gi) => {
            const ordered = sortPortsVisual(g).filter((p) => pts[p]);
            for (let i = 0; i < ordered.length - 1; i++) {
                segs.push({
                    a: ordered[i],
                    b: ordered[i + 1],
                    chain: gi,
                    group: ordered
                });
            }
        });
        return segs;
    }

    function scenarioState(name) {
        const now = Date.now();
        let chains = "1D,23,08";
        let tiles = true;
        let fans = false;
        if (name === "empty") {
            chains = "";
        } else if (name === "partial") {
            chains = "1D,23,56,04";
        } else if (name === "solved-a") {
            chains = SOLUTIONS.A;
        } else if (name === "solved-b" || name === "fans-on") {
            chains = SOLUTIONS.B;
            fans = name === "fans-on";
        } else if (name === "daisy") {
            chains = "14B,23,7A";
        } else if (name === "tiles-missing") {
            chains = "1D,23";
            tiles = false;
        }
        return buildState(chains, tiles, fans, name, now);
    }

    function buildState(chains, tilesPresent, fansOn, scenario, now) {
        const groups = parseChains(chains);
        const connected = {};
        groups.forEach((g) => g.forEach((p) => { connected[p] = true; }));
        const ports = [];
        for (let i = 0; i < 16; i++) {
            const slot = slotForPort(i);
            ports.push({
                id: i,
                hex: hexdig(i),
                connected: Boolean(connected[i]),
                level: connected[i] ? 0 : 1,
                tile: slot ? "R" + slot.row + "C" + slot.col : "—",
                corner: slot ? CORNER_LABEL[slot.corner] : "skip",
                chain: groups.find((g) => g.indexOf(i) >= 0) ? formatChain(groups.find((g) => g.indexOf(i) >= 0)) : ""
            });
        }
        return {
            ts: now || Date.now(),
            id: "Patch32Prop",
            status: "online",
            scenario: scenario || getScenario(),
            version: "0.01-demo",
            chains: chains,
            chainsUnique: uniqueEncoding(groups),
            groups: groups,
            allTilesPresent: tilesPresent,
            tileRawHigh: !tilesPresent,
            fansOn: fansOn,
            ports: ports,
            jackMap: demoConfig.jackMap,
            wifiConnected: true,
            wifiSsid: "Paradox-StageAP",
            wifiRssi: -47
        };
    }

    let demoCache = scenarioState(getScenario());
    let demoFansOverride = null;
    let demoChainsOverride = null;

    function currentState() {
        const base = scenarioState(getScenario());
        if (demoChainsOverride != null) {
            base.chains = demoChainsOverride;
            Object.assign(base, buildState(base.chains, base.allTilesPresent, base.fansOn, base.scenario, Date.now()));
        }
        if (demoFansOverride !== null) {
            base.fansOn = demoFansOverride;
        }
        demoCache = base;
        return base;
    }

    function hydrateLiveState(state) {
        if (!state || typeof state !== "object") {
            return state;
        }
        if (!Array.isArray(state.groups)) {
            state.groups = parseChains(state.chains || "");
        }
        if (Array.isArray(state.targetSets)) {
            demoConfig.targetSets = normalizeTargetSets(state.targetSets);
        }
        if (Array.isArray(state.jackMap)) {
            demoConfig.jackMap = mergeJackMap(state.jackMap);
        }
        return state;
    }

    async function api(path, options) {
        if (getDemoMode()) {
            return mockResponse(path, options);
        }
        const requestUrl = resolveApiUrl(path);
        const res = await fetch(requestUrl, options);
        const rawText = await res.text();
        let data = null;
        if (rawText) {
            try {
                data = JSON.parse(rawText);
            } catch {
                data = null;
            }
        }
        if (!res.ok) {
            throw new Error("HTTP " + res.status + " " + res.statusText);
        }
        if (path === "/api/state" || path === "/api/monitor") {
            return hydrateLiveState(data);
        }
        return data;
    }

    async function mockResponse(path, options) {
        await new Promise((r) => setTimeout(r, 50));
        const payload = options && options.body ? JSON.parse(options.body) : {};
        const cmd = payload.Command || payload.command;

        if (path === "/api/state" || path === "/api/monitor") {
            return currentState();
        }
        if (path === "/api/config/defaults") {
            return JSON.parse(JSON.stringify(DEFAULT_CONFIG));
        }
        if (path === "/api/config" && options && options.method === "POST") {
            saveDemoConfig(payload);
            return { ok: true, mock: true };
        }
        if (path === "/api/config") {
            return JSON.parse(JSON.stringify(demoConfig));
        }
        if (path === "/api/config/save") {
            saveDemoConfig(payload);
            return { ok: true, mock: true, persisted: true };
        }
        if (path === "/api/config/restore" || path === "/api/config/restore/save") {
            saveDemoConfig(DEFAULT_CONFIG);
            return { ok: true, mock: true };
        }
        if (path === "/api/command") {
            if (cmd === "fansOn") {
                demoFansOverride = true;
            } else if (cmd === "fansOff") {
                demoFansOverride = false;
            } else if (cmd === "reportState") {
                demoChainsOverride = currentState().chains;
            }
            return { ok: true, mock: true, Command: cmd || payload };
        }
        if (path === "/api/connection") {
            if (options && options.method === "POST") {
                return { ok: true, mock: true, applied: true };
            }
            return {
                wifiSsid: "Paradox-StageAP",
                wifiPassword: "",
                mqttHost: "192.168.8.132",
                mqttPort: 1883,
                mqttUsername: "",
                mqttPassword: "",
                mqttBaseTopic: "/Paradox/TFD/Patch/Prop",
                mqttCommandTopic: "/Paradox/TFD/Patch/Prop/Commands",
                mqttStateTopic: "/Paradox/TFD/Patch/Prop/state",
                mqttEventsTopic: "/Paradox/TFD/Patch/Prop/Events",
                mqttWarningsTopic: "/Paradox/TFD/Patch/Prop/warnings",
                mqttGameStateTopic: "paradox/tfd/state",
                mqttPropAnnounceTopic: "/Paradox/Props",
                networkName: "patch",
                apSsid: "Paradox-PXPatchV1-A1B2",
                apIpAddress: "192.168.4.1",
                apPassword: "",
                apEnabled: true
            };
        }
        if (path === "/api/connection/scan") {
            return {
                ok: true,
                networks: [
                    { ssid: "Paradox-TFD-1", rssi: -42 },
                    { ssid: "Props-Backstage", rssi: -55 },
                    { ssid: "TMOBILE", rssi: -67 }
                ]
            };
        }
        if (path === "/api/details") {
            return {
                propName: "Patch32Prop",
                ipAddress: "192.168.8.52",
                softwareVersion: "0.01-demo",
                buildNumber: "demo",
                buildDate: "2026-09-03",
                cpuTemp: "41 C",
                freeMemory: "182 KB"
            };
        }
        return { ok: true, mock: true };
    }

    function appendLog(node, value) {
        if (!node) {
            return;
        }
        node.textContent = "[" + nowIso() + "]\n" + JSON.stringify(value, null, 2) + "\n\n" + node.textContent;
    }

    function tablerWifiSvg(level) {
        const l = Math.max(0, Math.min(4, Number(level || 0)));
        const color = l >= 3 ? "#00c45c" : l === 2 ? "#f0a92a" : "#ef4444";
        const op1 = l >= 1 ? 1 : 0.25;
        const op2 = l >= 2 ? 1 : 0.25;
        const op3 = l >= 3 ? 1 : 0.25;
        const op4 = l >= 4 ? 1 : 0.25;
        return `<svg class="wifi-svg" viewBox="0 0 24 24" aria-hidden="true"><path d="M3 9.5a13 13 0 0 1 18 0" fill="none" stroke="${color}" stroke-opacity="${op4}" stroke-width="1.8" stroke-linecap="round"/><path d="M6 13a9 9 0 0 1 12 0" fill="none" stroke="${color}" stroke-opacity="${op3}" stroke-width="1.8" stroke-linecap="round"/><path d="M9 16.5a5 5 0 0 1 6 0" fill="none" stroke="${color}" stroke-opacity="${op2}" stroke-width="1.8" stroke-linecap="round"/><circle cx="12" cy="20" r="1.5" fill="${color}" fill-opacity="${op1}"/></svg>`;
    }

    function rssiLevel(rssi) {
        if (rssi >= -50) {
            return 4;
        }
        if (rssi >= -60) {
            return 3;
        }
        if (rssi >= -70) {
            return 2;
        }
        return 1;
    }

    function ensureWifiBadge() {
        let badge = el("wifiBadge");
        if (badge) {
            return badge;
        }
        const container = el("statusIcons");
        if (!container) {
            return null;
        }
        badge = document.createElement("div");
        badge.id = "wifiBadge";
        badge.className = "wifi-badge";
        badge.innerHTML = `<span id="wifiBadgeIcon" class="wifi-icon"></span><span id="wifiBadgeText">--</span>`;
        container.insertBefore(badge, container.firstChild);
        return badge;
    }

    function renderWifiStatus(details) {
        const badge = ensureWifiBadge();
        if (!badge) {
            return;
        }
        const icon = el("wifiBadgeIcon");
        const text = el("wifiBadgeText");
        if (details && details.wifiConnected) {
            if (icon) {
                icon.innerHTML = tablerWifiSvg(rssiLevel(details.wifiRssi));
            }
            if (text) {
                text.textContent = details.wifiSsid || "WiFi";
            }
        } else if (icon && text) {
            icon.innerHTML = tablerWifiSvg(0);
            text.textContent = "off";
        }
    }

    async function fetchStatusIcons() {
        try {
            renderWifiStatus(await api("/api/state"));
        } catch {
            renderWifiStatus(null);
        }
    }

    function getOverlay() {
        const sel = el("targetOverlay");
        if (sel) {
            return sel.value;
        }
        return localStorage.getItem(KEY_OVERLAY) || "B";
    }

    function getOverlayTarget() {
        const v = getOverlay();
        const sets = demoConfig.targetSets || [];
        if (!v || v === "off") {
            return "";
        }
        const i = Number(v);
        if (Number.isInteger(i) && i >= 0 && i < sets.length) {
            return sets[i].chains || "";
        }
        if (v === "A" || v === "B") {
            const hit = sets.find((s) => s.label === v);
            return (hit && hit.chains) || SOLUTIONS[v] || "";
        }
        return "";
    }

    function liveGroups(state) {
        if (state && Array.isArray(state.groups)) {
            return state.groups;
        }
        return parseChains(state && state.chains);
    }

    function renderCables(state) {
        const pts = portPoints();
        const avoid = demoConfig.jackMap.filter((slot) => slot.port != null).map((slot) => jackXY(slot, BOARD));
        const targetStr = getOverlayTarget();
        const targetSegs = segmentsForGroups(parseChains(targetStr), pts);
        const currentSegs = segmentsForGroups(liveGroups(state), pts);
        const targetEdges = edgeSet(parseChains(targetStr));
        const pathByEdge = {};

        function dFor(pa, pb) {
            const key = edgeKey(pa, pb);
            if (!pathByEdge[key]) {
                pathByEdge[key] = cablePath(pts[pa], pts[pb], avoid);
            }
            return pathByEdge[key];
        }

        const paths = [];
        targetSegs.forEach((seg) => {
            if (!pts[seg.a] || !pts[seg.b]) {
                return;
            }
            paths.push(`<path class="cable cable-target" d="${dFor(seg.a, seg.b)}" />`);
        });
        currentSegs.forEach((seg) => {
            if (!pts[seg.a] || !pts[seg.b]) {
                return;
            }
            if (!targetEdges[edgeKey(seg.a, seg.b)]) {
                paths.push(`<path class="cable cable-bad" d="${dFor(seg.a, seg.b)}" />`);
            }
        });
        currentSegs.forEach((seg) => {
            if (!pts[seg.a] || !pts[seg.b]) {
                return;
            }
            if (targetEdges[edgeKey(seg.a, seg.b)]) {
                paths.push(`<path class="cable cable-ok" d="${dFor(seg.a, seg.b)}" />`);
            }
        });
        return paths.join("");
    }

    function renderBoard(state) {
        const host = el("patchBoard");
        if (!host) {
            return;
        }
        const tiles = [];
        for (let row = 0; row < 2; row++) {
            for (let col = 0; col < 3; col++) {
                const x = BOARD.originX + col * (BOARD.tileW + BOARD.gapX);
                const y = BOARD.originY + row * (BOARD.tileH + BOARD.gapY);
                tiles.push(`<rect class="tile-plate" x="${x}" y="${y}" width="${BOARD.tileW}" height="${BOARD.tileH}" rx="18" fill="url(#tileMetal)" />`);
            }
        }
        const jacks = demoConfig.jackMap.map((slot) => {
            const p = jackXY(slot, BOARD);
            const live = slot.port != null;
            const connected = live && state.ports[slot.port] && state.ports[slot.port].connected;
            const cls = live ? (connected ? "jack jack-live jack-on" : "jack jack-live") : "jack jack-skip";
            const label = live ? hexdig(slot.port) : "";
            return `<g class="${cls}" transform="translate(${p.x},${p.y})">
                <circle class="jack-ring" r="18" />
                <circle class="jack-hole" r="8" />
                <text class="jack-label" y="34">${label}</text>
            </g>`;
        });
        host.innerHTML = `<svg viewBox="0 0 ${BOARD.w} ${BOARD.h}" preserveAspectRatio="xMidYMid meet" aria-hidden="true">
            <defs>
                <linearGradient id="tileMetal" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" stop-color="#e8edf4"/>
                    <stop offset="42%" stop-color="#b7c0cc"/>
                    <stop offset="100%" stop-color="#8e98a8"/>
                </linearGradient>
            </defs>
            ${tiles.join("")}
            <g class="cables">${renderCables(state)}</g>
            ${jacks.join("")}
        </svg>`;
    }

    function setLive(state) {
        demoCache = state;
        const panel = el("gameStatePanel");
        const badge = el("stateBadge");
        const chains = String((state && state.chains) || "");
        const target = getOverlayTarget();
        const solved = target && chains.toUpperCase() === target;
        if (badge) {
            badge.textContent = solved ? "matching overlay" : (chains ? "cables" : "idle");
            badge.className = "badge " + (solved ? "state-solved" : chains ? "state-running" : "state-paused");
        }
        if (panel) {
            panel.className = "panel pulse " + (solved ? "state-solved" : chains ? "state-running" : "state-paused");
        }
        renderBoard(state);
        const line = el("chainLine");
        if (line) {
            line.textContent = "Chains " + (chains || "(none)") +
                (state.chainsUnique && state.chainsUnique !== chains.toUpperCase()
                    ? "  ·  unique " + state.chainsUnique : "");
        }
        const pills = el("statusPills");
        if (pills) {
            pills.innerHTML =
                `<span class="pill ${state.allTilesPresent ? "pill-ok" : "pill-bad"}">Tiles ${state.allTilesPresent ? "present" : "missing"}</span>` +
                `<span class="pill ${state.fansOn ? "pill-ok" : "pill-off"}">Fans ${state.fansOn ? "on" : "off"}</span>` +
                `<span class="pill">Broker .132</span>`;
        }
    }

    function renderPortStrip(state) {
        const host = el("portStrip");
        if (!host) {
            return;
        }
        host.innerHTML = state.ports.map((p) => {
            return `<span class="port-cell ${p.connected ? "on" : ""}" title="${p.tile} ${p.corner}">${p.hex}</span>`;
        }).join("");
    }

    function renderPortTable(state) {
        const tbody = document.querySelector("#portTable tbody");
        if (!tbody) {
            return;
        }
        tbody.innerHTML = state.ports.map((p) => {
            return `<tr>
                <td>${p.hex}</td>
                <td>${p.tile}</td>
                <td>${p.corner}</td>
                <td>${p.chain || "—"}</td>
                <td class="${p.level === 0 ? "lvl-low" : "lvl-high"}">${p.level === 0 ? "LOW" : "HIGH"}</td>
            </tr>`;
        }).join("");
    }

    function renderMonitorFacts(state) {
        const host = el("monitorFacts");
        if (!host) {
            return;
        }
        host.innerHTML = [
            ["Chains", state.chains || "(none)"],
            ["Unique 3+", state.chainsUnique || "(same)"],
            ["AllTilesPresent", String(state.allTilesPresent)],
            ["Tile GPIO 18", state.tileRawHigh ? "HIGH (missing)" : "LOW (present)"],
            ["Fan GPIO 23", state.fansOn ? "HIGH" : "LOW"]
        ].map((row) => `<div class="detail-item"><span>${row[0]}</span><strong>${row[1]}</strong></div>`).join("");
        const dump = el("chainDump");
        if (dump) {
            dump.textContent = JSON.stringify({ pairwise: state.chains, unique: state.chainsUnique, groups: state.groups }, null, 2);
        }
    }

    function renderMcp(state) {
        const host = el("mcpGrid");
        if (!host) {
            return;
        }
        const rows = state.ports.map((p) => {
            return `<tr>
                <td>${p.id}</td>
                <td>${p.hex} · ${p.tile} ${p.corner}</td>
                <td>${p.connected ? "scan" : "in"}</td>
                <td class="${p.level === 0 ? "lvl-low" : "lvl-high"}">${p.level === 0 ? "LOW" : "HIGH"}</td>
            </tr>`;
        }).join("");
        host.innerHTML = `<div class="mcp-chip">
            <h3>MCP addr 0 · pins 0–15</h3>
            <table class="pin-table">
                <thead><tr><th>Pin</th><th>Role</th><th>Mode</th><th>Lvl</th></tr></thead>
                <tbody>${rows}</tbody>
            </table>
        </div>`;
    }

    function fillJackMap(map) {
        const host = el("jackMap");
        if (!host) {
            return;
        }
        map = mergeJackMap(map);
        const tiles = [];
        const displayCorners = ["tl", "tr", "bl", "br"];
        for (let row = 0; row < 2; row++) {
            for (let col = 0; col < 3; col++) {
                const slots = map.filter((s) => s.row === row && s.col === col);
                const selects = displayCorners.map((corner) => {
                    const slot = slots.find((s) => s.corner === corner);
                    const val = slot && slot.port != null ? String(slot.port) : "";
                    const opts = ["<option value=\"\">Skip</option>"].concat(
                        Array.from({ length: 16 }, (_, i) => {
                            const sel = String(i) === val ? " selected" : "";
                            return `<option value="${i}"${sel}>${hexdig(i)}</option>`;
                        })
                    ).join("");
                    return `<label>${CORNER_LABEL[corner]}<select data-row="${row}" data-col="${col}" data-corner="${corner}">${opts}</select></label>`;
                }).join("");
                tiles.push(`<div class="jack-tile"><h4>R${row} C${col}</h4><div class="jack-tile-grid">${selects}</div></div>`);
            }
        }
        host.innerHTML = tiles.join("");
    }

    function collectJackMap() {
        const host = el("jackMap");
        if (!host) {
            return demoConfig.jackMap;
        }
        const slots = [];
        host.querySelectorAll("select").forEach((sel) => {
            const v = sel.value;
            slots.push({
                row: Number(sel.getAttribute("data-row")),
                col: Number(sel.getAttribute("data-col")),
                corner: sel.getAttribute("data-corner"),
                port: v === "" ? null : Number(v)
            });
        });
        return mergeJackMap(slots);
    }

    function escapeAttr(value) {
        return String(value == null ? "" : value)
            .replace(/&/g, "&amp;")
            .replace(/"/g, "&quot;")
            .replace(/</g, "&lt;");
    }

    function fillTargetSets(sets) {
        const host = el("targetSets");
        if (!host) {
            return;
        }
        host.innerHTML = normalizeTargetSets(sets).map((s, i) => {
            return `<div class="target-set-row">
                <span class="target-set-idx">${i + 1}</span>
                <label>Name<input data-target-label="${i}" maxlength="16" value="${escapeAttr(s.label)}"></label>
                <label>Chains<input data-target-chains="${i}" maxlength="47" placeholder="09,28,3E,4C" value="${escapeAttr(s.chains)}"></label>
            </div>`;
        }).join("");
    }

    function collectTargetSets() {
        const host = el("targetSets");
        if (!host) {
            return normalizeTargetSets(demoConfig.targetSets);
        }
        const rows = [];
        for (let i = 0; i < TARGET_SET_COUNT; i++) {
            const labelEl = host.querySelector('[data-target-label="' + i + '"]');
            const chainsEl = host.querySelector('[data-target-chains="' + i + '"]');
            rows.push({
                label: labelEl ? labelEl.value : TARGET_LABELS.charAt(i),
                chains: chainsEl ? chainsEl.value : ""
            });
        }
        return normalizeTargetSets(rows);
    }

    function fillOverlaySelect(sel) {
        if (!sel) {
            return;
        }
        const saved = localStorage.getItem(KEY_OVERLAY);
        const scenario = getScenario();
        const sets = normalizeTargetSets(demoConfig.targetSets);
        const options = ['<option value="off">Off</option>'];
        sets.forEach((s, i) => {
            if (!s.chains) {
                return;
            }
            options.push('<option value="' + i + '">' + escapeAttr(s.label || ("Set " + (i + 1))) +
                " · " + escapeAttr(s.chains) + "</option>");
        });
        sel.innerHTML = options.join("");
        let pick = "off";
        if (scenario === "solved-a" && sel.querySelector('option[value="0"]')) {
            pick = "0";
        } else if ((scenario === "solved-b" || scenario === "fans-on") && sel.querySelector('option[value="1"]')) {
            pick = "1";
        } else if (saved && sel.querySelector('option[value="' + saved + '"]')) {
            pick = saved;
        } else if (sel.querySelector('option[value="1"]')) {
            pick = "1";
        } else if (sel.querySelector('option[value="0"]')) {
            pick = "0";
        }
        sel.value = pick;
    }

    function applyConfigObject(cfg) {
        if (!cfg) {
            return;
        }
        if (cfg.jackMap) {
            demoConfig.jackMap = mergeJackMap(cfg.jackMap);
        }
        if (cfg.targetSets) {
            demoConfig.targetSets = normalizeTargetSets(cfg.targetSets);
        }
        ["scanPeriodMs", "debounceCount", "settleMs", "heartbeatInterval"].forEach((k) => {
            if (cfg[k] != null) {
                demoConfig[k] = Number(cfg[k]);
            }
        });
        if (cfg.debug != null) {
            demoConfig.debug = Boolean(cfg.debug);
        }
    }

    function fillForm(form, cfg) {
        if (!form) {
            return;
        }
        Array.from(form.elements).forEach((field) => {
            if (!field.name) {
                return;
            }
            if (field.type === "checkbox") {
                field.checked = Boolean(cfg[field.name]);
            } else if (cfg[field.name] != null) {
                field.value = cfg[field.name];
            }
        });
    }

    function collectForm(form, body) {
        if (!form) {
            return;
        }
        Array.from(form.elements).forEach((field) => {
            if (!field.name) {
                return;
            }
            if (field.type === "checkbox") {
                body[field.name] = field.checked;
            } else if (field.type === "number") {
                body[field.name] = Number(field.value);
            } else {
                body[field.name] = field.value;
            }
        });
    }

    async function pageDashboard() {
        const overlay = el("targetOverlay");
        try {
            applyConfigObject(await api("/api/config"));
        } catch {
            /* keep baked / localStorage defaults */
        }
        fillOverlaySelect(overlay);
        if (overlay) {
            overlay.addEventListener("change", () => {
                localStorage.setItem(KEY_OVERLAY, overlay.value);
                setLive(demoCache);
            });
        }
        async function refresh() {
            const state = await api("/api/state");
            setLive(state);
            appendLog(el("actionLog"), { event: "state", chains: state.chains, tiles: state.allTilesPresent, fans: state.fansOn });
        }
        if (el("refreshBtn")) {
            el("refreshBtn").addEventListener("click", () => refresh().catch((e) => appendLog(el("actionLog"), String(e))));
        }
        document.querySelectorAll("[data-cmd]").forEach((btn) => {
            btn.addEventListener("click", async () => {
                try {
                    appendLog(el("actionLog"), await api("/api/command", { method: "POST", body: btn.getAttribute("data-cmd") }));
                    await refresh();
                } catch (e) {
                    appendLog(el("actionLog"), String(e));
                }
            });
        });
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
        refresh().catch((e) => appendLog(el("actionLog"), String(e)));
        setInterval(() => refresh().catch(() => {}), 2000);
    }

    async function pageMonitor() {
        async function refresh() {
            const state = await api("/api/monitor");
            if (el("monitorScenario")) {
                el("monitorScenario").textContent = state.scenario || "live";
            }
            renderPortStrip(state);
            renderPortTable(state);
            renderMonitorFacts(state);
            renderMcp(state);
            const dump = el("monitorLog");
            if (dump && demoConfig.debug) {
                dump.classList.remove("hidden");
                dump.textContent = JSON.stringify(state.ports, null, 2);
            }
        }
        if (el("refreshMonitor")) {
            el("refreshMonitor").addEventListener("click", () => refresh().catch(() => {}));
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
        refresh();
        setInterval(() => refresh().catch(() => {}), 2000);
    }

    async function pageConfig() {
        const form = el("configForm");
        const log = el("configLog");
        try {
            const cfg = await api("/api/config");
            fillForm(form, cfg);
            fillJackMap(cfg.jackMap);
            fillTargetSets(cfg.targetSets);
            applyConfigObject(cfg);
            appendLog(log, cfg);
        } catch (e) {
            fillJackMap(demoConfig.jackMap);
            fillTargetSets(demoConfig.targetSets);
            appendLog(log, String(e));
        }

        async function postConfig(path) {
            const body = {};
            collectForm(form, body);
            body.jackMap = collectJackMap();
            body.targetSets = collectTargetSets();
            appendLog(log, await api(path, { method: "POST", body: JSON.stringify(body) }));
            saveDemoConfig(body);
        }

        if (el("applyConfig")) {
            el("applyConfig").addEventListener("click", (ev) => {
                ev.preventDefault();
                postConfig("/api/config").catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("saveConfig")) {
            el("saveConfig").addEventListener("click", (ev) => {
                ev.preventDefault();
                postConfig("/api/config/save").catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("restoreDefaults")) {
            el("restoreDefaults").addEventListener("click", (ev) => {
                ev.preventDefault();
                api("/api/config/defaults").then((cfg) => {
                    fillForm(form, cfg);
                    fillJackMap(cfg.jackMap);
                    fillTargetSets(cfg.targetSets);
                    applyConfigObject(cfg);
                    appendLog(log, cfg);
                }).catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("sendRaw")) {
            el("sendRaw").addEventListener("click", async () => {
                try {
                    appendLog(el("rawLog"), await api("/api/command", { method: "POST", body: el("rawCommand").value }));
                } catch (e) {
                    appendLog(el("rawLog"), String(e));
                }
            });
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
    }

    async function pageConnection() {
        const log = el("connectionLog");
        function fillTopics(conn) {
            ["mqttCommandTopic", "mqttStateTopic", "mqttEventsTopic", "mqttWarningsTopic"].forEach((id) => {
                if (el(id)) {
                    el(id).textContent = conn[id] || "";
                }
            });
        }
        try {
            const conn = await api("/api/connection");
            ["wifiSsid", "wifiPassword", "mqttHost", "mqttPort", "mqttUsername", "mqttPassword", "mqttBaseTopic", "mqttGameStateTopic", "mqttPropAnnounceTopic", "networkName", "apPassword"].forEach((id) => {
                if (el(id) && conn[id] != null) {
                    el(id).value = conn[id];
                }
            });
            if (el("apSsidDisplay") && conn.apSsid) {
                el("apSsidDisplay").value = conn.apSsid;
            }
            if (el("apEnabled")) {
                el("apEnabled").checked = Boolean(conn.apEnabled);
            }
            if (el("apIpNote") && conn.apIpAddress) {
                el("apIpNote").textContent = "AP IP Address: " + conn.apIpAddress;
            }
            fillTopics(conn);
            if (el("wifiStatus")) {
                el("wifiStatus").innerHTML = `<span class="wifi-icon">${tablerWifiSvg(4)}</span> Connected to <strong>${conn.wifiSsid}</strong>`;
            }
            appendLog(log, conn);
        } catch (e) {
            appendLog(log, String(e));
        }

        try {
            const scan = await api("/api/connection/scan");
            const list = el("ssidList");
            if (list && scan.networks) {
                list.innerHTML = "";
                scan.networks.forEach((n) => {
                    const b = document.createElement("button");
                    b.type = "button";
                    b.className = "ssid-item";
                    b.innerHTML = `<span>${n.ssid}</span><span class="ssid-meta"><span class="wifi-icon">${tablerWifiSvg(rssiLevel(n.rssi))}</span>${n.rssi} dBm</span>`;
                    b.addEventListener("click", () => {
                        if (el("wifiSsid")) {
                            el("wifiSsid").value = n.ssid;
                        }
                    });
                    list.appendChild(b);
                });
            }
        } catch {
            /* ignore */
        }

        async function loadDetails() {
            try {
                const d = await api("/api/details");
                const map = {
                    detailPropName: d.propName,
                    detailIpAddress: d.ipAddress,
                    detailSoftwareVersion: d.softwareVersion,
                    detailBuildNumber: d.buildNumber,
                    detailBuildDate: d.buildDate,
                    detailCpuTemp: d.cpuTemp,
                    detailFreeMemory: d.freeMemory
                };
                Object.keys(map).forEach((id) => {
                    if (el(id)) {
                        el(id).textContent = map[id] || "-";
                    }
                });
            } catch (e) {
                appendLog(el("deviceLog"), String(e));
            }
        }
        if (el("refreshDetails")) {
            el("refreshDetails").addEventListener("click", loadDetails);
        }
        if (el("connectWifi")) {
            el("connectWifi").addEventListener("click", async () => {
                appendLog(log, await api("/api/connection", { method: "POST", body: "{}" }));
            });
        }
        if (el("saveConnection")) {
            el("saveConnection").addEventListener("click", async () => {
                appendLog(log, await api("/api/connection", { method: "POST", body: "{}" }));
            });
        }
        if (el("applyTopics") || el("applyDeviceName")) {
            const apply = async () => appendLog(log, await api("/api/connection", { method: "POST", body: "{}" }));
            if (el("applyTopics")) {
                el("applyTopics").addEventListener("click", apply);
            }
            if (el("applyDeviceName")) {
                el("applyDeviceName").addEventListener("click", apply);
            }
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
        loadDetails();
    }

    window.PX = {
        pageDashboard: pageDashboard,
        pageConfig: pageConfig,
        pageMonitor: pageMonitor,
        pageConnection: pageConnection
    };
})();
