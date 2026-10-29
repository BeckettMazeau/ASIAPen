## Formalize exam GUI, startup, sign-in and encrypted backups

### Why
The backend worked, but the GUI and startup weren't ready for client review:
- Scores were small black handwriting, and the toolbar was crowded with calibration controls.
- The active cell was hard to spot.
- Starting an exam meant answering terminal prompts.

Clinicians need to read the sheet and the active dermatome from across the room and confirm each entry without looking straight at the screen.

### What changed
**Exam screen (`index.html`)**
- The worksheet scales to fit the window. The internal 1311×858 coordinates are unchanged.
- Scores show as large color chips: 0 red, 1 amber, 2 green, NT gray. A color-blind-safe palette and a large size are available.
- The active cell pulses magenta, with row and column markers. The matching dermatome is shaded in the same color.
- A status panel shows the side, level and modality in oversized type, plus the last entry, per-column progress and on-screen 0/1/2/NT / undo / prev / next buttons.
- Each entry (and each undo) can show as a full-screen colored confirmation, readable from peripheral vision. It can be turned off and is also announced via `aria-live`.
- A Settings drawer is saved per viewer. Outline calibration tools now sit behind a calibration-mode toggle, and session actions are in a menu.

**Startup**
- `NeuroSense.bat` is a double-click launcher. It starts the server under `pythonw` (no console) and opens an Edge/Chrome `--app` window with its own browser profile.
- The terminal prompts are replaced by in-app sign-in and start screens: continue, new exam, and recent sessions. Session → Quit stops the server.
- Launching again reuses a server that is already running. Errors show as message boxes.
- An `APP_DIR`/`DATA_DIR` split prepares for a later PyInstaller build.

**Security and backups (`start_exam.py`)**
- Sign-in uses a placeholder demo PIN, **1234**. The check is one hook, `verify_credentials()`, so a hospital badge/LDAP/SSO system can replace it later. Data endpoints require sign-in, and `audit.log` records events without patient names.
- Disk backups are encrypted with Windows DPAPI, so there are no new dependencies. `--decrypt FILE` reads one back.
- A rolling snapshot is taken every 2 minutes, keeping 30 per session. Writes are atomic and fsync'd.
- The server only serves a fixed list of static files, so backups, logs and source are no longer reachable over HTTP.
- `.gitignore` now excludes `backups/`.

**Bug fix**: `/api/save` accepted a blank `sessionId`. That let a page that hadn't synced yet overwrite the server's saved session with an empty one. Blank and stale ids now return 409, and the page doesn't save to disk until it has synced.

This PR also includes earlier pending work: the Arduino Due pins moved to A0–A3 with 3.3 V notes, and the launcher's port-in-use check.

### Reviewer notes
- **Tested on a scratch copy of real backups:**
  - Sign-in, including a wrong PIN and the lockout.
  - New, resumed and recent sessions.
  - Keyboard and serial-line scoring, and undo.
  - The confirmation overlay, palette, size and calibration settings.
  - The narrow/stacked layout.
  - 409 on stale and blank ids.
  - The 401/403/404 access checks, the encrypt/decrypt round-trip and snapshots.
  - Launching with `NeuroSense.bat` (pythonw + Edge app window), reusing a running server, and Quit.
- **Not tested:** print preview, and real Arduino hardware (the regex and firmware commands are unchanged).
- **Breaking:** clients that POST `/api/save` without a `sessionId` now get 409.
- **Known gaps:**
  - Old backup files stay as plain JSON with patient names in the filename; only the autosave is re-encrypted on the first launch.
  - Browser localStorage, the "Download backup file" output and CSV exports are not encrypted.
  - There is no idle auto-lock.
  - Custom outlines saved in a normal browser profile need to be exported and imported once into the app window.
- `AGENTS.md` and `README.txt` are updated to match.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
