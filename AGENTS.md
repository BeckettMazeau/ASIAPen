# Agent Instructions

Repository for **ASIAPen / NeuroSense**, an interactive sensory examination tool implementing the ASIA ISNCSCI worksheet with SVG dermatome overlays, physical 4-button RF input via Arduino USB serial, and automated multi-tier session recovery[cite: 1, 2, 5].

## Project Structure
- `index.html` — Self-contained single-page application (HTML, CSS, vanilla JS runtime)[cite: 2, 5].
- `worksheet.png` (or `worksheet.jpg`) — Standard ASIA ISNCSCI reference image (1311 × 858 px coordinate baseline)[cite: 1, 2, 5].
- `neurosense-dermatome-regions.json` — Pre-calibrated v2 polygon definitions and testing dots across 28 spinal levels[cite: 3, 5].
- `start_exam.py` — Local HTTP server, session backup endpoint, USB port detector, and browser launcher[cite: 5].
- `arduino/` — Firmware sketch for the 4-channel RF receiver / button logger (`0`, `1`, `2`, `UNDO`).
- `backups/` — Auto-generated disk persistence folder containing rolling snapshots and per-patient recovery JSON files.
- `README.txt` — Examiner- and engineer-facing setup and workflow documentation[cite: 4, 5].

## Architecture & How It Works
- **Coordinate Space**: Fixed 1311 × 858 px canvas matching `worksheet.png`[cite: 2, 5]. SVG overlays (`.dermatome-map`), sensory score buttons, totals, and patient header input fields map to these absolute pixel coordinates[cite: 2, 5].
- **Sensory Examination Grid**: 28 levels (`C2`–`S4-5`) across 4 columns: `R-LT` (Right Light Touch), `R-PP` (Right Pin Prick), `L-LT` (Left Light Touch), and `L-PP` (Left Pin Prick), totaling 112 score boxes[cite: 2, 5].
  - Permitted cell values: `0`, `1`, `2`, `NT`[cite: 2, 5].
  - Subtotals and grand totals compute automatically once all required modality cells contain numeric entries[cite: 2, 5].
- **Hardware Integration & Serial Protocol**:
  - Web Serial API (`navigator.serial` @ 115200 baud)[cite: 2, 5].
  - Line-based serial input matching `/^(?:SCORE\s*[:=]\s*|BTN\s*[:=]\s*)?([012]|UNDO|DEL|DELETE)$/i`.
  - Commands `0`, `1`, `2` record score and advance according to selected sequence[cite: 2].
  - Commands `UNDO`, `DEL`, or `DELETE` pop the last action from `history`, restore the previous cell value, and focus that cell[cite: 2].
  - Re-plug resilience: Handled via `navigator.serial.addEventListener('connect')` and `navigator.serial.getPorts()` for automatic reconnect on USB interruption.
- **State Persistence & Backup Tiers**:
  - **Tier 1 (Browser Storage)**: `localStorage` keys `neurosense-overlay-v1` (active scores), `neurosense-full-session-v1` (scores, active cell, history stack, test order, patient metadata), `neurosense-multi-regions-v2`, and `neurosense-test-dots-v2`[cite: 2, 5].
  - **Tier 2 (Disk Auto-Save)**: `POST /api/save` called on every score and metadata change to update `backups/autosave_latest.json` and patient-specific JSON archives via `start_exam.py`.
  - **Tier 3 (Manual Backup/Restore)**: Top bar buttons allow downloading timestamped session JSON files or importing previous `.json` backups.

## Local Development & Testing

| Task | Command |
|---|---|
| Full System Launch (Server + Hardware Check + UI) | `python start_exam.py` |
| Fallback Dev Server (Standard Library) | `python -m http.server 8000`[cite: 5] |
| Dev Server (Node) | `npx serve .`[cite: 5] |
| Validate Regions JSON | `powershell -Command "Get-Content .\neurosense-dermatome-regions.json \| ConvertFrom-Json \| Out-Null"`[cite: 5] |

> Note: Web Serial API strictly requires `localhost` or an `HTTPS` origin in Chromium-based browsers (Chrome / Edge)[cite: 2, 4, 5]. Do not load `index.html` directly via `file://`[cite: 2, 5].

## Coding & Modification Conventions
- **Zero-Dependency Core**: Maintain vanilla HTML, CSS, and modern JavaScript in `index.html` without build tools or third-party frontend frameworks[cite: 5].
- **Coordinate Integrity**: Do not adjust the 1311 × 858 px canvas dimensions without recalculating `ys`, `columns`, `totals`, patient header positions, and the SVG `viewBox`[cite: 2, 5].
- **Robust Serial Parsing**: Serial buffers must remain newline-delimited and tolerate variable packet rates, trailing whitespace, or command prefixes[cite: 2].
- **Data Validation & Compatibility**: Preserve defensive JSON checks (`validPolygon`, schema checks) and maintain backward compatibility with `version: 2` region schemas[cite: 2, 3, 5].
- **Accessibility**: Preserve full keyboard navigation (`0`, `1`, `2`, `N`, `Backspace`/`Z`, arrow keys) and ARIA attributes for screen readers[cite: 2, 5].

## Commit Attribution
AI commits MUST include: