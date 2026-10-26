================================================================================
NeuroSense — ASIA ISNCSCI Sensory Examination & Logging System
================================================================================

OVERVIEW
NeuroSense is a sensory scoring tool for the ASIA ISNCSCI spinal cord injury
examination. It combines an interactive worksheet with dermatome highlighting,
automatic totals, USB serial hardware input from an Arduino RF receiver, and
continuous, encrypted, multi-tier backup.

--------------------------------------------------------------------------------
QUICK START
--------------------------------------------------------------------------------
1. Connect the Arduino receiver by USB.
2. Double-click NeuroSense.bat. No terminal stays open; NeuroSense opens in its
   own Edge (or Chrome) app window.
3. Sign in with your examiner ID / badge and PIN.
   DEMO BUILD: the PIN is 1234. Hospital credential sign-in will replace this.
4. Start screen: "Continue exam" resumes the last session, "New exam" starts a
   new patient, and "Recent sessions" reopens any earlier saved exam.
5. Click the device pill in the top bar ("No device · click to connect") and
   pick the Arduino (115200 baud). After the first time it reconnects by itself.
6. When finished: Session > Quit NeuroSense. Closing just the window leaves the
   server running quietly; double-clicking NeuroSense.bat again reopens it.

Developers can still run "python start_exam.py" in a terminal to see status
output (options: --no-browser, --tab, --decrypt FILE).

--------------------------------------------------------------------------------
THE EXAM SCREEN
--------------------------------------------------------------------------------
• Worksheet (left): scales to fit the window. Scores are shown as large colored
  chips: 0 red, 1 amber, 2 green, NT gray. The active box pulses in magenta, its
  row and column are marked, and the matching dermatome is shaded magenta on
  the body diagram.
• Status panel (right): huge side / level / modality ("RIGHT  C6  LIGHT TOUCH"),
  the last value entered, progress per column, and on-screen 0/1/2/NT, Undo,
  Prev and Next buttons.
• Entry confirmation: after every score the value flashes full-screen in its
  color (e.g. a giant green "2") with the box it went into, so it can be checked
  from peripheral vision. Undo shows "UNDO". Toggle it in Settings.

--------------------------------------------------------------------------------
HARDWARE CONTROLS (ARDUINO / RF TRANSMITTER)
--------------------------------------------------------------------------------
The sketch targets an Arduino DUE (connect via the Programming USB port; I/O is
3.3 V only). It reads pins A0-A3 as digital inputs:
  • A0 (Button 0) : Scores 0 (Absent) and advances to the next cell.
  • A1 (Button 1) : Scores 1 (Altered) and advances to the next cell.
  • A2 (Button 2) : Scores 2 (Normal) and advances to the next cell.
  • A3 (Button 3) : Undo / Delete (reverts the last entry and moves cursor back).

--------------------------------------------------------------------------------
KEYBOARD SHORTCUTS & INTERACTION
--------------------------------------------------------------------------------
  • 0, 1, 2       : Record score for selected cell.
  • N             : Record NT (Not Testable).
  • Backspace / Z : Undo last entry.
  • Arrow Keys    : Move selection forward or backward.
  • Click any box : Jump directly to that dermatome cell to retest.
  • Esc           : Close Settings / menus.

--------------------------------------------------------------------------------
SETTINGS (⚙ in the top bar, remembered on this computer)
--------------------------------------------------------------------------------
• Entry confirmation on/off and display time (0.7 s or 1.4 s).
• Score colors: standard, or color-blind safe (orange / yellow / blue / gray).
• Score size on the sheet: normal or large.
• Highlight active dermatome, Auto-advance, Testing order.
• Dermatome calibration mode: shows the outline editing tools (below).

--------------------------------------------------------------------------------
BACKUP, ENCRYPTION & FAULT TOLERANCE
--------------------------------------------------------------------------------
• Continuous: every score, undo, or patient-detail edit is saved to the app's
  browser storage and to disk (backups/autosave_latest.json and
  backups/session_<id>.json). The top-bar pill shows "Backed up · time".
• Rolling snapshots: every 2 minutes of activity a point-in-time copy goes to
  backups/snapshots/<session id>/ (the last 30 are kept per session).
• Encryption: on Windows, disk backups are encrypted with Windows DPAPI. Only
  the same Windows user account can read them. To read one manually:
      python start_exam.py --decrypt backups\session_<id>.json
  Backup files made before this version stay as plain JSON until deleted.
  "Download backup file" and "Export CSV" produce plain, unencrypted files.
• Audit: backups/audit.log records sign-ins, new/resumed sessions and quits
  (no patient names).
• Crash / closed window: start NeuroSense again; "Continue exam" restores
  scores, active box, undo history and patient details.
• Lock: Session > Lock / sign out. The server also asks for sign-in again
  after it restarts.
• USB cable disconnection: scores stay on screen; re-plugging reconnects
  automatically without a reload.
• Manual backups: Session > Download backup file / Load backup file.

--------------------------------------------------------------------------------
DERMATOME OUTLINE TOOLS (Settings > Dermatome calibration mode)
--------------------------------------------------------------------------------
• Outline tool > Draw new region: Click to define polygon corners; click
  "Finish drawing" to complete the shape.
• Outline tool > Edit corners: Drag handles to adjust boundaries. Double-click an
  edge to add a vertex; right-click a vertex to delete it.
• Outline tool > Move testing dot: Reposition key sensory reference points.
• Export / Import outlines: Save or restore custom SVG calibrations as JSON.
• The app window uses its own browser profile. Custom outlines that were saved
  only in a normal browser tab must be moved once: Export outlines there, then
  Import outlines in the app window.

--------------------------------------------------------------------------------
FILES
--------------------------------------------------------------------------------
• NeuroSense.bat                  : Double-click launcher (no console window).
• index.html                      : User interface and application runtime.
• worksheet.png / worksheet.jpg   : Base ASIA worksheet background image.
• start_exam.py                   : Local server, sign-in, encrypted backups, app window.
• neurosense-dermatome-regions.json: Calibrated dermatome boundary definitions.
• backups/                        : Session backups, snapshots, logs (patient data:
                                    never commit or share).
================================================================================
