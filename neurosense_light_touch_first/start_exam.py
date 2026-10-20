import http.server
import json
import os
import shutil
import sys
import threading
import webbrowser
from datetime import datetime
from http.server import ThreadingHTTPServer

# Optional: pyserial for checking Arduino connection (pip install pyserial)
try:
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

PORT = 8000
BACKUP_DIR = "backups"
AUTOSAVE_FILE = os.path.join(BACKUP_DIR, "autosave_latest.json")
HOST = "127.0.0.1"
ALLOWED_HOSTS = {f"localhost:{PORT}", f"127.0.0.1:{PORT}"}  # blocks DNS-rebinding
session_meta = {}
SESSION_ID = ""
write_lock = threading.Lock()

def write_json(path, data):
    """Atomic write: a crash mid-write can never leave a truncated file."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    os.replace(tmp, path)

def ensure_worksheet_image():
    """Ensures worksheet.png exists if only worksheet.jpg is present."""
    if not os.path.exists("worksheet.png") and os.path.exists("worksheet.jpg"):
        shutil.copyfile("worksheet.jpg", "worksheet.png")
        print("[✓] Created worksheet.png from worksheet.jpg")

def detect_arduino():
    print("\n--- Hardware Check ---")
    if not HAS_SERIAL:
        print("[i] Tip: Run 'pip install pyserial' if you want automatic COM port detection.")
        return
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("[!] No USB Serial devices detected yet. Plug in your Arduino before clicking 'Connect USB device'.")
    else:
        print("Detected Serial Ports:")
        for p in ports:
            print(f"  • {p.device}: {p.description}")
        print("[i] Select the port above when prompted in the browser.")

def collect_patient_info():
    global session_meta, SESSION_ID
    os.makedirs(BACKUP_DIR, exist_ok=True)

    print("=" * 50)
    print("   NeuroSense ASIA ISNCSCI Examination Launcher")
    print("=" * 50)

    if os.path.exists(AUTOSAVE_FILE):
        try:
            with open(AUTOSAVE_FILE, "r", encoding="utf-8") as f:
                existing = json.load(f)
            p_info = existing.get("patient", {})
            score_count = len(existing.get("scores", {}))
            print(f"\n[!] Found previous session: Patient '{p_info.get('patientName', 'Unknown')}' ({score_count}/112 scores recorded)")
            resume = input("Resume this session? (Y/n): ").strip().lower()
            if resume in ("", "y", "yes"):
                session_meta = p_info
                SESSION_ID = existing.get("sessionId") or datetime.now().strftime("%Y%m%d_%H%M%S")
                return
        except Exception:
            # Keep an unreadable autosave instead of overwriting it
            try:
                shutil.copyfile(AUTOSAVE_FILE, os.path.join(BACKUP_DIR, "autosave_corrupt_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".json"))
            except OSError:
                pass

    print("\n--- New Patient Session ---")
    patient_name = input("Patient Name / ID : ").strip() or "Anonymous"
    examiner_name = input("Examiner Name     : ").strip() or "Examiner"
    exam_date = datetime.now().strftime("%Y-%m-%d %H:%M")

    session_meta = {
        "patientName": patient_name,
        "examinerName": examiner_name,
        "examDate": exam_date
    }

    # Initialize a clean autosave file for the new session
    SESSION_ID = datetime.now().strftime("%Y%m%d_%H%M%S")
    write_json(AUTOSAVE_FILE, {"sessionId": SESSION_ID, "patient": session_meta, "scores": {}, "active": 0, "history": []})

class NeuroSenseHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass  # Keep terminal output clean

    def bad_host(self):
        if self.headers.get("Host", "") in ALLOWED_HOSTS:
            return False
        self.send_error(403)
        return True

    def do_GET(self):
        if self.bad_host():
            return
        if self.path == "/api/session":
            with write_lock:
                try:
                    with open(AUTOSAVE_FILE, "rb") as f:
                        body = f.read()
                except OSError:
                    body = json.dumps({"sessionId": SESSION_ID, "patient": session_meta}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def do_POST(self):
        if self.bad_host():
            return
        if self.path != "/api/save":
            self.send_error(404)
            return
        # Requiring application/json forces a CORS preflight for cross-site pages
        if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
            self.send_error(415)
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            if not 0 < length <= 1_000_000:
                raise ValueError("bad length")
            data = json.loads(self.rfile.read(length).decode("utf-8"))
            patient, scores = data["patient"], data["scores"]
            if not isinstance(patient, dict) or not isinstance(scores, dict):
                raise ValueError("bad shape")
            sid = data.get("sessionId") or SESSION_ID
            if sid != SESSION_ID:  # stale page from a previous session
                self.send_error(409)
                return
            data["sessionId"] = sid
            safe_name = "".join(c if c.isalnum() else "_" for c in str(patient.get("patientName") or "session"))[:40]
            patient_file = os.path.join(BACKUP_DIR, f"session_{safe_name}_{sid}.json")
            with write_lock:
                os.makedirs(BACKUP_DIR, exist_ok=True)
                write_json(AUTOSAVE_FILE, data)
                write_json(patient_file, data)
        except (ValueError, KeyError, TypeError):
            self.send_error(400)
            return
        except OSError as e:
            print(f"[!] Save failed: {e}", file=sys.stderr)
            self.send_error(500)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"status":"ok"}')

class NeuroSenseServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = os.name != "nt"  # on Windows it would allow a second instance on the same port

if __name__ == "__main__":
    ensure_worksheet_image()
    collect_patient_info()
    detect_arduino()

    url = f"http://localhost:{PORT}/index.html"
    print(f"\n[✓] Server running at {url}")
    print("[✓] Auto-saving every button press to ./backups/")
    print("    Press Ctrl+C in this window when the exam is finished.\n")

    with NeuroSenseServer((HOST, PORT), NeuroSenseHandler) as httpd:
        webbrowser.open(url)  # only after the socket is bound
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[✓] Session safely saved. Shutting down.")