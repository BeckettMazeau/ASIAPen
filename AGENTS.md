# Agent Instructions

Repository for **ASIAPen / NeuroSense**, an interactive sensory examination tool implementing the ASIA ISNCSCI worksheet with SVG dermatome overlays, physical 4-button RF input via Arduino USB serial, and automated multi-tier session recovery.

## Project Structure
All application code lives in `neurosense_light_touch_first/`. `start_exam.py` changes to its own directory at startup (`APP_DIR`, `sys._MEIPASS`-aware for a future PyInstaller build) and keeps data in `DATA_DIR/backups`, so it works from anywhere; run the static dev server from that directory.

- `neurosense_light_touch_first/index.html` — Self-contained single-page app (HTML, CSS, vanilla JS).
- `neurosense_light_touch_first/worksheet.png` — ASIA ISNCSCI reference image (1311 × 858 px). `start_exam.py` copies `worksheet.jpg` to `worksheet.png` if only the jpg exists.
- `neurosense_light_touch_first/neurosense-dermatome-regions.json` — Pre-calibrated polygon and testing-dot definitions for 28 spinal levels.
- `neurosense_light_touch_first/NeuroSense.bat` — Double-click entry point: starts `start_exam.py` with `pythonw` (falls back to `pyw -3`), so no console stays open. Keep CRLF line endings.
- `neurosense_light_touch_first/start_exam.py` — Local HTTP server (port 8000), sign-in, session API, DPAPI-encrypted backups and snapshots, optional pyserial port listing, Edge/Chrome app-window launcher.
- `neurosense_light_touch_first/arduino-code.cpp` — Arduino sketch (paste into an `.ino`); 4-channel button/RF logger.
- `neurosense_light_touch_first/README.txt` — Examiner-facing setup and workflow guide.
- `neurosense_light_touch_first/backups/` — Created at runtime, git-ignored (patient data): `autosave_latest.json`, `session_<sessionId>.json` (older builds wrote `session_<patient>_<sessionId>.json`; both are still read), `snapshots/<sessionId>/*.json`, `autosave_corrupt_*.json` copies of unreadable autosaves, `audit.log` (PHI-free events), `neurosense.log` (status output under pythonw).
- `state/` (repo root) — Empty, unused.
- `neurosense_light_touch_first/__pycache__/` is currently tracked in git; do not edit it, and avoid adding more compiled files.

## Architecture & How It Works
- **Coordinate Space**: Fixed 1311 × 858 px canvas matching `worksheet.png`. The SVG overlay (`viewBox="0 0 1311 858"`, class `.dermatome-map`), score cells, totals, and patient header fields use these absolute coordinates. Row centers are the `ys` array and column positions the `columns` array in `index.html`.
- **Sensory Grid**: 28 levels (`C2`–`S4-5`) × 4 columns `R-LT`, `R-PP`, `L-LT`, `L-PP` = 112 cells. Permitted values: `0`, `1`, `2`, `NT`. Totals compute automatically once all required cells are numeric.
- **Exam UI**: the `#sheet` stays 1311 × 858 internally and is scaled to fit by `fitSheet()` (CSS `transform: scale`, `ResizeObserver`); `svgPoint` uses `getScreenCTM()` so calibration works under scaling. Score cells get a `data-v` attribute and are colored from CSS custom properties (`--s0`, `--s1`, `--s2`, `--snt`; `data-palette="cb"` on `<html>` swaps to Okabe-Ito). Active cell, `.row-band`, `.col-mark` and the dermatome all use the magenta `--accent`. The right-hand panel is rendered by `renderNow()`. `record()`/`undoLast()` call `confirmEntry()`, which shows the full-screen `.flash` overlay (toggleable) and updates an `aria-live` region. Print CSS hides all of this and prints plain black values.
- **Screens**: `#login` (sign-in) → `#start` (continue / new exam / recent sessions) → exam. A `sessionStorage` key `ns-in-exam` skips the start screen on a reload of the same window. Keyboard and serial input are ignored while a screen or Settings is open; covered regions get `inert`.
- **Settings** (`neurosense-settings-v1` in localStorage, per viewer, not in the session): flash on/off and duration, palette, score size, auto-advance, dermatome highlight, calibration mode (shows the `#calibBar` outline tools).
- **Test order** (`#order` select in Settings, persisted in the session): `lt` (default, "All Light Touch → All Pin Prick": R-LT, L-LT, R-PP, L-PP), `columns`, `row` (per level R-LT → R-PP → L-LT → L-PP), `pp` (pin prick first). Order is built by `sequence()`. An "advance" checkbox controls auto-advance after each score, skipping already-filled cells.
- **Outline tool modes**: View only, Move selected region, Move testing dot, Edit corners, Draw new region. Regions are validated by `validPolygon` (3–300 points, coordinates within 0–1311).
- **Serial Input**:
  - Web Serial API, 115200 baud, newline-delimited lines.
  - Accepted line format: `/^(?:SCORE\s*[:=]\s*|BTN\s*[:=]\s*)?([012]|UNDO|DEL|DELETE)$/i`.
  - `0`/`1`/`2` record a score and advance; `UNDO`/`DEL`/`DELETE` pop `history`, restore the prior value, and refocus that cell. `history` is capped at 500 entries.
  - Auto-reconnect on re-plug via `navigator.serial` `connect` events and `getPorts()`.
  - Firmware (`arduino-code.cpp`): targets the Arduino Due (3.3 V I/O, Programming USB port); pins A0/A1/A2/A3 (read via `digitalRead`) send `0`/`1`/`2`/`UNDO`; 50 ms debounce, fires on press only; `ACTIVE_HIGH` (default `true`) is for RF receivers, set `false` for bare buttons to GND (uses `INPUT_PULLUP`). Prints `READY` at boot — the parser ignores it because it doesn't match.
- **Persistence tiers**:
  1. **Browser** `localStorage` (the launcher's app window uses its own profile at `%LOCALAPPDATA%\NeuroSense\browser`): `neurosense-overlay-v1` (scores), `neurosense-full-session-v1` (scores, active cell, history, order, patient metadata), `neurosense-multi-regions-v2` (outlines), `neurosense-test-dots-v2` (dots), `ns-sid` (current server session id). Legacy `neurosense-custom-outlines-v1` and `neurosense-map-adjustments-v1` are migrated on load.
  2. **Disk** via server: the page `POST`s to `/api/save` after every change (one request in flight, latest state re-sent afterward), but only after it has reconciled with `/api/session` (`synced`). The server atomically (`.tmp` + fsync + `os.replace`) rewrites `backups/autosave_latest.json` and `session_<sid>.json`, and at most every `SNAPSHOT_SECONDS` (120) adds a snapshot (keeps `SNAPSHOTS_KEPT` = 30). `sessionId` must equal the server's (a blank id is rejected too), otherwise 409. Files are wrapped as `{"app","enc":"dpapi","data"}` when `ENCRYPT` (Windows, unless `NEUROSENSE_ENCRYPT=0`); `read_session_file` reads both forms.
  3. **Manual**: Session menu "Download backup file" (plain JSON) and "Load backup file".
- **Server (`start_exam.py`) behavior**: binds `127.0.0.1:8000`; rejects requests whose `Host` header is not `localhost:8000`/`127.0.0.1:8000` (DNS-rebinding guard); serves only the `STATIC_FILES` allow-list (backups, logs and source are 404). All POSTs require `Content-Type: application/json` and a body ≤ 1 MB.
  - Endpoints: `GET /api/info` (public: app, version, `encrypted`, `signedIn`, serial `ports` or `null` without pyserial); `POST /api/login` / `/api/logout`; and, signed-in only (401 otherwise): `GET /api/session`, `GET /api/sessions` (recent, deduplicated by id), `POST /api/save`, `/api/session/new`, `/api/session/resume`, `/api/shutdown`.
  - **Sign-in is a placeholder**: `verify_credentials()` checks a PBKDF2 hash of the demo PIN `1234`; it is the only hook to replace with a hospital badge/LDAP/SSO provider. Tokens are in memory (`ns_auth` HttpOnly, SameSite=Strict cookie), so a server restart requires signing in again. 5 failures lock sign-in for 30 s. Events go to `audit()`.
  - Startup has no prompts: it loads (and re-encrypts if plain) `autosave_latest.json`, or creates a blank session. If port 8000 already answers `/api/info` as NeuroSense, it just reopens the app window and exits; another occupant or a bind failure is reported with a message box under pythonw. `open_app_window()` launches Edge/Chrome with `--app` and a dedicated `--user-data-dir`, falling back to `webbrowser.open`. CLI: `--no-browser`, `--tab`, `--decrypt FILE`.

## Local Development & Testing

| Task | Command (from `neurosense_light_touch_first/`) |
|---|---|
| Double-click launch (no console, app window) | `NeuroSense.bat` |
| Launch with console output | `python start_exam.py` (add `--no-browser` for preview tools) |
| Read an encrypted backup | `python start_exam.py --decrypt backups\session_<id>.json` |
| Static-only dev server (no `/api/*`, no disk backup) | `python -m http.server 8000` |
| Validate regions JSON | `powershell -Command "Get-Content .\neurosense-dermatome-regions.json \| ConvertFrom-Json \| Out-Null"` |
| Optional port detection | `pip install pyserial` |

There is no build step or automated test suite; verify changes by running the app and exercising the keyboard/serial paths. Web Serial requires Chrome/Edge on `localhost` or HTTPS — never open `index.html` via `file://`. The `start_exam.py` server only accepts port 8000 and will not work behind a different Host name. When testing, copy the app to a scratch folder first: running the server converts the real `backups/` autosave to encrypted form.

**Packaging later**: PyInstaller `--windowed --onedir` with `index.html`, `worksheet.png` and the regions JSON as data files (`APP_DIR` already handles `sys._MEIPASS`; `DATA_DIR` is next to the exe). Keep the Chromium `--app` window: Web Serial isn't reliably available in an embedded WebView2/pywebview.

## Coding & Modification Conventions
- **Zero-dependency core**: vanilla HTML/CSS/modern JS in `index.html`; no build tools or frontend frameworks. Python server uses the standard library only (pyserial optional).
- **Coordinate integrity**: don't change the 1311 × 858 canvas without recalculating `ys`, `columns`, totals, patient header positions, and the SVG `viewBox`.
- **Serial parsing**: keep it line-based and tolerant of trailing whitespace, prefixes, and variable packet rates. Keep the firmware command strings and the regex in sync.
- **Validation & compatibility**: keep defensive checks (`validPolygon`, `cleanSession`) and `version: 2` region schema compatibility; keep the localStorage migrations.
- **Server hardening**: preserve the Host check, static allow-list, sign-in requirement on data endpoints, JSON content-type requirement, size limit, session-id check, encryption wrapper, and atomic writes when touching `start_exam.py`. Never put patient names in filenames or `audit.log`.
- **Accessibility**: keep keyboard navigation (`0`, `1`, `2`, `N` = NT, `Backspace`/`Z` = undo, `ArrowLeft`/`ArrowRight` move along the current order) and the per-cell `aria-label`s.
- **Docs**: when behavior, file names, or shortcuts change, update `README.txt` and this file together.

## Commit Attribution
AI commits MUST include:

```
Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
```
