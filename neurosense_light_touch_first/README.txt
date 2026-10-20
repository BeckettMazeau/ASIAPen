================================================================================
NeuroSense — ASIA ISNCSCI Sensory Examination & Logging System
================================================================================

OVERVIEW
NeuroSense is a browser-based sensory scoring tool for the ASIA ISNCSCI spinal
cord injury examination. It combines an interactive worksheet overlay with
dermatome mapping, automatic calculation of totals, USB serial hardware logging
from an Arduino RF receiver, and full multi-tier session recovery.

--------------------------------------------------------------------------------
QUICK START
--------------------------------------------------------------------------------
1. Connect your Arduino to your computer via USB.
2. Run the startup launcher:
     python start_exam.py
3. Enter the Patient Name/ID and Examiner Name when prompted in the terminal.
4. Your default browser (Chrome or Edge) will open to http://localhost:8000.
5. Click "Connect USB device" in the top bar and select your Arduino (baud: 115200).
   Once granted permission, subsequent sessions will connect automatically.

--------------------------------------------------------------------------------
HARDWARE CONTROLS (ARDUINO / RF TRANSMITTER)
--------------------------------------------------------------------------------
The Arduino reads 4 digital inputs corresponding to the following commands:
  • Button 0 : Scores 0 (Absent) and advances to the next cell.
  • Button 1 : Scores 1 (Altered) and advances to the next cell.
  • Button 2 : Scores 2 (Normal) and advances to the next cell.
  • Button 3 : Undo / Delete (reverts the last entry and moves cursor back).

--------------------------------------------------------------------------------
KEYBOARD SHORTCUTS & INTERACTION
--------------------------------------------------------------------------------
  • 0, 1, 2       : Record score for selected cell.
  • N             : Record NT (Not Testable).
  • Backspace / Z : Undo last entry.
  • Arrow Keys    : Move selection forward or backward.
  • Click any box : Jump directly to that dermatome cell to retest.

--------------------------------------------------------------------------------
BACKUP & FAULT TOLERANCE
--------------------------------------------------------------------------------
• Auto-Save (Continuous):
  Every score, patient metadata edit, or undo is automatically saved to both
  browser localStorage and disk (backups/autosave_latest.json).
• Recovery after Tab Close or Crash:
  Relaunching python start_exam.py prompts to resume your previous session.
  Scores, active cell focus, undo stack, and patient info are restored.
• USB Cable Disconnection:
  If the Arduino cable is unplugged, your scores remain on screen. Re-plugging
  the USB cable automatically re-establishes the serial connection without reload.
• Manual Backups:
  Click "Save backup" in the top bar to download a standalone JSON snapshot.
  Click "Load backup" to restore a previous session JSON at any time.

--------------------------------------------------------------------------------
DERMATOME OUTLINE TOOLS
--------------------------------------------------------------------------------
• Outline Tool > Draw new region: Click to define polygon corners; click
  "Finish drawing" to complete the shape.
• Outline Tool > Edit corners: Drag handles to adjust boundaries. Double-click an
  edge to add a vertex; right-click a vertex to delete it.
• Outline Tool > Move testing dot: Reposition key sensory reference points.
• Export / Import outlines: Save or restore custom SVG calibrations as JSON.

--------------------------------------------------------------------------------
FILES
--------------------------------------------------------------------------------
• index.html                      : Main user interface and application runtime.
• worksheet.png / worksheet.jpg   : Base ASIA worksheet background image.
• start_exam.py                   : Local HTTP server, disk auto-saver, launcher.
• neurosense-dermatome-regions.json: Calibrated dermatome boundary definitions.
• backups/                        : Auto-save directory for session JSON files.
================================================================================