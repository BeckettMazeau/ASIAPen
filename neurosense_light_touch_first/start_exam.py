"""NeuroSense launcher: local exam server, encrypted auto-backups, app window.

Double-click NeuroSense.bat (or run `python start_exam.py` for a console with status output).
"""
import argparse
import base64
import glob
import hashlib
import hmac
import http.server
import json
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.request
import webbrowser
from datetime import datetime, timezone
from http.server import ThreadingHTTPServer
from urllib.parse import urlsplit

# Optional: pyserial for listing COM ports on the start screen (pip install pyserial)
try:
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

APP_NAME = "NeuroSense"
VERSION = "0.4.0"
PORT = 8000
HOST = "127.0.0.1"
ALLOWED_HOSTS = {f"localhost:{PORT}", f"127.0.0.1:{PORT}"}  # blocks DNS-rebinding
URL = f"http://localhost:{PORT}/index.html"

# Read-only resources live next to the script (or inside a PyInstaller bundle); data lives next to the
# script/exe so a future packaged build keeps the same layout.
FROZEN = getattr(sys, "frozen", False)
APP_DIR = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.dirname(os.path.abspath(sys.executable if FROZEN else __file__))
BACKUP_DIR = os.path.join(DATA_DIR, "backups")
SNAPSHOT_DIR = os.path.join(BACKUP_DIR, "snapshots")
AUTOSAVE_FILE = os.path.join(BACKUP_DIR, "autosave_latest.json")
LOG_FILE = os.path.join(BACKUP_DIR, "neurosense.log")
AUDIT_FILE = os.path.join(BACKUP_DIR, "audit.log")

# Only these files are served; everything else (backups, logs, source) is 404.
STATIC_FILES = {"/index.html", "/worksheet.png", "/worksheet.jpg", "/neurosense-dermatome-regions.json"}

SNAPSHOT_SECONDS = 120  # a rolling copy of each session at most this often
SNAPSHOTS_KEPT = 30     # per session
SID_RE = re.compile(r"^\d{8}_\d{6}(_[0-9a-f]{4})?$")

HAS_CONSOLE = sys.stdout is not None  # False under pythonw.exe
SESSION_ID = ""
write_lock = threading.Lock()
last_snapshot = {}

# --------------------------------------------------------------------------------------------------
# Encryption at rest: Windows DPAPI (stdlib ctypes, no key to manage). Files can only be decrypted by
# the same Windows user account. Elsewhere, or with NEUROSENSE_ENCRYPT=0, files are plain JSON.
# --------------------------------------------------------------------------------------------------
ENCRYPT = os.name == "nt" and os.environ.get("NEUROSENSE_ENCRYPT", "1") != "0"
DPAPI_ENTROPY = b"NeuroSense session backup v1"

if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    class _Blob(ctypes.Structure):
        _fields_ = [("cbData", wintypes.DWORD), ("pbData", ctypes.POINTER(ctypes.c_char))]

    def _blob(data):
        buf = ctypes.create_string_buffer(data, len(data))
        return _Blob(len(data), ctypes.cast(buf, ctypes.POINTER(ctypes.c_char))), buf

    def _dpapi(data, protect):
        crypt32, kernel32 = ctypes.windll.crypt32, ctypes.windll.kernel32
        kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        (src, _keep1), (ent, _keep2), out = _blob(data), _blob(DPAPI_ENTROPY), _Blob()
        flags = 0x1  # CRYPTPROTECT_UI_FORBIDDEN
        if protect:
            ok = crypt32.CryptProtectData(ctypes.byref(src), "NeuroSense", ctypes.byref(ent), None, None, flags, ctypes.byref(out))
        else:
            ok = crypt32.CryptUnprotectData(ctypes.byref(src), None, ctypes.byref(ent), None, None, flags, ctypes.byref(out))
        if not ok:
            raise OSError(ctypes.WinError())
        try:
            return ctypes.string_at(out.pbData, out.cbData)
        finally:
            kernel32.LocalFree(ctypes.cast(out.pbData, ctypes.c_void_p))

def write_json(path, data):
    """Atomic write: a crash mid-write can never leave a truncated file."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)

def write_session_file(path, data):
    if ENCRYPT:
        blob = _dpapi(json.dumps(data).encode("utf-8"), True)
        data = {"app": APP_NAME, "enc": "dpapi", "data": base64.b64encode(blob).decode("ascii")}
    write_json(path, data)

def read_session_file(path):
    """Reads plain or DPAPI-wrapped session JSON. Raises OSError/ValueError if unreadable."""
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if isinstance(data, dict) and data.get("enc") == "dpapi":
        if os.name != "nt":
            raise ValueError("encrypted with Windows DPAPI")
        data = json.loads(_dpapi(base64.b64decode(data["data"]), False).decode("utf-8"))
    if not isinstance(data, dict):
        raise ValueError("not a session object")
    return data

# --------------------------------------------------------------------------------------------------
# Sign-in. PLACEHOLDER: a single demo PIN (1234). verify_credentials() is the only thing the server
# calls, so a hospital badge / LDAP / SSO provider replaces just this function later.
# --------------------------------------------------------------------------------------------------
DEMO_PIN_SALT = b"neurosense-demo-salt"
DEMO_PIN_HASH = bytes.fromhex("55617e4a16875dd42ec6dcddc1ccabf13c1f2253d6905fa5c4575f44c28810ab")  # PBKDF2("1234")

def verify_credentials(user_id, password):
    digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), DEMO_PIN_SALT, 200_000)
    return hmac.compare_digest(digest, DEMO_PIN_HASH)

auth_tokens = {}  # token -> user id, for the lifetime of this server
login_failures = []  # timestamps, for a simple lockout
LOCKOUT_AFTER, LOCKOUT_SECONDS = 5, 30

def audit(event, **fields):
    """Append-only, PHI-free event log (no patient names)."""
    line = " ".join([datetime.now().isoformat(timespec="seconds"), event] + [f"{k}={v}" for k, v in fields.items()])
    try:
        with open(AUDIT_FILE, "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except OSError:
        pass

# --------------------------------------------------------------------------------------------------
# Sessions
# --------------------------------------------------------------------------------------------------
def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")

def new_session_id():
    return datetime.now().strftime("%Y%m%d_%H%M%S") + "_" + secrets.token_hex(2)

def session_file(sid):
    return os.path.join(BACKUP_DIR, f"session_{sid}.json")

def find_session_file(sid):
    """New files are session_<sid>.json; older ones also carry the patient name."""
    if os.path.exists(session_file(sid)):
        return session_file(sid)
    for path in glob.glob(os.path.join(BACKUP_DIR, f"session_*_{sid}.json")):
        return path
    return None

def start_session(patient):
    """Creates a blank session. Caller holds write_lock (or is single-threaded at boot)."""
    global SESSION_ID
    SESSION_ID = new_session_id()
    data = {"version": 1, "sessionId": SESSION_ID, "timestamp": now_iso(), "patient": patient,
            "scores": {}, "active": 0, "history": []}
    write_session_file(AUTOSAVE_FILE, data)
    return data

def load_or_create_session():
    global SESSION_ID
    if os.path.exists(AUTOSAVE_FILE):
        try:
            existing = read_session_file(AUTOSAVE_FILE)
            SESSION_ID = existing.get("sessionId") or new_session_id()
            if existing.get("sessionId") != SESSION_ID or (ENCRYPT and not is_encrypted(AUTOSAVE_FILE)):
                existing["sessionId"] = SESSION_ID
                write_session_file(AUTOSAVE_FILE, existing)
        except (OSError, ValueError, KeyError):
            # Keep an unreadable autosave instead of overwriting it
            try:
                shutil.copyfile(AUTOSAVE_FILE, os.path.join(BACKUP_DIR, "autosave_corrupt_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".json"))
            except OSError:
                pass
        else:
            print(f"[✓] Previous session available to resume ({len(existing.get('scores', {}))}/112 scores)")
            return
    start_session({})

def is_encrypted(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f).get("enc") == "dpapi"
    except (OSError, ValueError, AttributeError):
        return False

def maybe_snapshot(data):
    """Rolling point-in-time copies, so a bad edit can be recovered even after autosave overwrote it."""
    sid = data["sessionId"]
    now = time.monotonic()
    if now - last_snapshot.get(sid, -SNAPSHOT_SECONDS) < SNAPSHOT_SECONDS:
        return
    last_snapshot[sid] = now
    folder = os.path.join(SNAPSHOT_DIR, sid)
    os.makedirs(folder, exist_ok=True)
    write_session_file(os.path.join(folder, datetime.now().strftime("%Y%m%d_%H%M%S") + ".json"), data)
    for old in sorted(glob.glob(os.path.join(folder, "*.json")))[:-SNAPSHOTS_KEPT]:
        try:
            os.remove(old)
        except OSError:
            pass

def list_sessions(limit=40):
    out, seen = [], set()
    paths = sorted(glob.glob(os.path.join(BACKUP_DIR, "session_*.json")), key=os.path.getmtime, reverse=True)
    for path in paths[:limit]:
        try:
            d = read_session_file(path)
        except (OSError, ValueError, KeyError):
            continue
        if d.get("sessionId") in seen:  # an older name-based file of the same session
            continue
        seen.add(d.get("sessionId"))
        p = d.get("patient") if isinstance(d.get("patient"), dict) else {}
        out.append({"sessionId": d.get("sessionId", ""), "patientName": p.get("patientName", ""),
                    "examinerName": p.get("examinerName", ""), "examDate": p.get("examDate", ""),
                    "scored": len(d.get("scores", {})) if isinstance(d.get("scores"), dict) else 0,
                    "timestamp": d.get("timestamp", "")})
    return out

def serial_ports():
    if not HAS_SERIAL:
        return None
    try:
        return [{"device": p.device, "description": p.description} for p in serial.tools.list_ports.comports()]
    except Exception:
        return []

def ensure_worksheet_image():
    """Ensures worksheet.png exists if only worksheet.jpg is present."""
    if not os.path.exists("worksheet.png") and os.path.exists("worksheet.jpg"):
        shutil.copyfile("worksheet.jpg", "worksheet.png")
        print("[✓] Created worksheet.png from worksheet.jpg")

# --------------------------------------------------------------------------------------------------
# HTTP
# --------------------------------------------------------------------------------------------------
class NeuroSenseHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=APP_DIR, **kwargs)

    def log_message(self, format, *args):
        pass  # Keep output clean

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def bad_host(self):
        if self.headers.get("Host", "") in ALLOWED_HOSTS:
            return False
        self.send_error(403)
        return True

    def send_json(self, obj, status=200, cookie=None):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        if cookie is not None:
            self.send_header("Set-Cookie", cookie)
        self.end_headers()
        self.wfile.write(body)

    def user(self):
        for part in self.headers.get("Cookie", "").split(";"):
            name, _, value = part.strip().partition("=")
            if name == "ns_auth" and value in auth_tokens:
                return auth_tokens[value]
        return None

    def require_user(self):
        if self.user() is None:
            self.send_json({"error": "sign-in required"}, 401)
            return False
        return True

    def read_json(self):
        """Returns the parsed JSON body, or None after sending an error."""
        # Requiring application/json forces a CORS preflight for cross-site pages
        if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
            self.send_error(415)
            return None
        try:
            length = int(self.headers.get("Content-Length", 0))
            if not 0 < length <= 1_000_000:
                raise ValueError("bad length")
            data = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(data, dict):
                raise ValueError("bad shape")
            return data
        except (ValueError, UnicodeDecodeError):
            self.send_error(400)
            return None

    def do_GET(self):
        if self.bad_host():
            return
        path = urlsplit(self.path).path
        if path == "/api/info":
            self.send_json({"app": APP_NAME, "version": VERSION, "encrypted": ENCRYPT,
                            "signedIn": self.user() is not None, "ports": serial_ports()})
        elif path == "/api/session":
            if not self.require_user():
                return
            with write_lock:
                try:
                    data = read_session_file(AUTOSAVE_FILE)
                except (OSError, ValueError, KeyError):
                    data = {"sessionId": SESSION_ID, "patient": {}}
            self.send_json(data)
        elif path == "/api/sessions":
            if self.require_user():
                self.send_json({"current": SESSION_ID, "sessions": list_sessions()})
        elif path == "/":
            self.send_response(302)
            self.send_header("Location", "/index.html")
            self.end_headers()
        elif path in STATIC_FILES:
            super().do_GET()
        else:
            self.send_error(404)

    def do_HEAD(self):
        if self.bad_host():
            return
        if urlsplit(self.path).path in STATIC_FILES:
            super().do_HEAD()
        else:
            self.send_error(404)

    def do_POST(self):
        if self.bad_host():
            return
        path = urlsplit(self.path).path
        routes = {"/api/login": self.post_login, "/api/logout": self.post_logout, "/api/save": self.post_save,
                  "/api/session/new": self.post_new, "/api/session/resume": self.post_resume,
                  "/api/shutdown": self.post_shutdown}
        if path not in routes:
            self.send_error(404)
            return
        data = self.read_json()
        if data is None:
            return
        if path != "/api/login" and not self.require_user():
            return
        try:
            routes[path](data)
        except OSError as e:
            print(f"[!] {path} failed: {e}", file=sys.stderr)
            self.send_error(500)

    def post_login(self, data):
        now = time.monotonic()
        login_failures[:] = [t for t in login_failures if now - t < LOCKOUT_SECONDS]
        if len(login_failures) >= LOCKOUT_AFTER:
            self.send_json({"error": f"Too many attempts. Wait {LOCKOUT_SECONDS} seconds."}, 429)
            return
        user_id = str(data.get("user") or "").strip()[:80] or "examiner"
        if not verify_credentials(user_id, str(data.get("password") or "")):
            login_failures.append(now)
            audit("login_failed", user=user_id)
            self.send_json({"error": "Incorrect ID or PIN."}, 401)
            return
        token = secrets.token_urlsafe(32)
        auth_tokens[token] = user_id
        audit("login", user=user_id)
        self.send_json({"user": user_id}, cookie=f"ns_auth={token}; HttpOnly; SameSite=Strict; Path=/")

    def post_logout(self, data):
        for part in self.headers.get("Cookie", "").split(";"):
            name, _, value = part.strip().partition("=")
            if name == "ns_auth":
                audit("logout", user=auth_tokens.pop(value, "?"))
        self.send_json({"status": "ok"}, cookie="ns_auth=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0")

    def post_save(self, data):
        patient, scores = data.get("patient"), data.get("scores")
        if not isinstance(patient, dict) or not isinstance(scores, dict):
            self.send_error(400)
            return
        with write_lock:
            sid = data.get("sessionId")
            if sid != SESSION_ID:  # stale page from a previous session, or one that never synced
                self.send_error(409)
                return
            data["sessionId"] = sid
            os.makedirs(BACKUP_DIR, exist_ok=True)
            write_session_file(AUTOSAVE_FILE, data)
            write_session_file(session_file(sid), data)
            maybe_snapshot(data)
        self.send_json({"status": "ok"})

    def post_new(self, data):
        patient = {k: str(data.get(k) or "").strip()[:200] for k in ("patientName", "examinerName", "examDate")}
        with write_lock:
            session = start_session(patient)
        audit("session_new", user=self.user(), session=session["sessionId"])
        self.send_json(session)

    def post_resume(self, data):
        global SESSION_ID
        sid = str(data.get("sessionId") or "")
        path = find_session_file(sid) if SID_RE.match(sid) else None
        if not path:
            self.send_error(404)
            return
        with write_lock:
            try:
                session = read_session_file(path)
            except (ValueError, KeyError):
                self.send_error(422)
                return
            SESSION_ID = session["sessionId"] = sid
            write_session_file(AUTOSAVE_FILE, session)
        audit("session_resume", user=self.user(), session=sid)
        self.send_json(session)

    def post_shutdown(self, data):
        audit("shutdown", user=self.user())
        self.send_json({"status": "ok"})
        threading.Thread(target=self.server.shutdown, daemon=True).start()

class NeuroSenseServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = os.name != "nt"  # on Windows it would allow a second instance on the same port

# --------------------------------------------------------------------------------------------------
# Launcher
# --------------------------------------------------------------------------------------------------
def notify(message, error=False):
    """Console output when there is one, otherwise a Windows message box."""
    print(("[!] " if error else "") + message, file=sys.stderr if error else sys.stdout)
    if not HAS_CONSOLE and os.name == "nt":
        ctypes.windll.user32.MessageBoxW(None, message, APP_NAME, 0x10 if error else 0x40)

def setup_output():
    """Under pythonw there is no console: send status output to a small rotating log file."""
    if HAS_CONSOLE:
        for stream in (sys.stdout, sys.stderr):
            try:
                stream.reconfigure(errors="replace")  # "✓" on a cp1252 console or pipe
            except (AttributeError, ValueError):
                pass
        return
    try:
        if os.path.getsize(LOG_FILE) > 1_000_000:
            os.replace(LOG_FILE, LOG_FILE + ".1")
    except OSError:
        pass
    log = open(LOG_FILE, "a", encoding="utf-8", buffering=1)
    sys.stdout = sys.stderr = log
    print(f"\n--- {datetime.now().isoformat(timespec='seconds')} {APP_NAME} {VERSION} ---")

def port_in_use():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        return s.connect_ex((HOST, PORT)) == 0

def neurosense_running():
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/api/info", timeout=2) as r:
            return json.load(r).get("app") == APP_NAME
    except (OSError, ValueError, AttributeError):
        return False

def find_browser():
    """Edge or Chrome (both support Web Serial and --app windows)."""
    if os.name != "nt":
        return None
    names = [("Microsoft", "Edge", "msedge.exe"), ("Google", "Chrome", "chrome.exe")]
    for vendor, product, exe in names:
        for env in ("PROGRAMFILES(X86)", "PROGRAMFILES", "LOCALAPPDATA"):
            base = os.environ.get(env)
            path = base and os.path.join(base, vendor, product, "Application", exe)
            if path and os.path.exists(path):
                return path
    import winreg
    for _, _, exe in names:
        for root in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
            try:
                with winreg.OpenKey(root, rf"SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\{exe}") as key:
                    path = winreg.QueryValue(key, None)
                    if path and os.path.exists(path):
                        return path
            except OSError:
                pass
    return None

def open_app_window(use_tab=False):
    browser = None if use_tab else find_browser()
    if not browser:
        webbrowser.open(URL)
        return
    # A dedicated profile: exam data and USB permissions stay out of personal browsing.
    profile = os.path.join(os.environ.get("LOCALAPPDATA") or DATA_DIR, APP_NAME, "browser")
    subprocess.Popen([browser, f"--app={URL}", f"--user-data-dir={profile}", "--start-maximized",
                      "--no-first-run", "--no-default-browser-check"], close_fds=True)

def main():
    parser = argparse.ArgumentParser(description="NeuroSense exam launcher")
    parser.add_argument("--no-browser", action="store_true", help="start the server only")
    parser.add_argument("--tab", action="store_true", help="open in a normal browser tab instead of an app window")
    parser.add_argument("--decrypt", metavar="FILE", help="print a (DPAPI-encrypted) backup as plain JSON and exit")
    args = parser.parse_args()

    os.chdir(APP_DIR)
    os.makedirs(BACKUP_DIR, exist_ok=True)
    setup_output()

    if args.decrypt:
        print(json.dumps(read_session_file(args.decrypt), indent=2))
        return

    # Already open (e.g. the app window was closed but the server kept running): just reopen the window
    if port_in_use():
        if neurosense_running():
            print("[i] NeuroSense is already running; reopening the window.")
            if not args.no_browser:
                open_app_window(args.tab)
        else:
            notify(f"Port {PORT} is used by another program (or an older NeuroSense launcher).\n"
                   "Close it and start NeuroSense again.", error=True)
        return

    ensure_worksheet_image()
    load_or_create_session()

    try:
        httpd = NeuroSenseServer((HOST, PORT), NeuroSenseHandler)
    except OSError as e:
        notify(f"Could not start the NeuroSense server on port {PORT}:\n{e}", error=True)
        sys.exit(1)

    print(f"[✓] Server running at {URL}")
    print(f"[✓] Auto-backup to {BACKUP_DIR} ({'encrypted, DPAPI' if ENCRYPT else 'plain JSON'})")
    if HAS_CONSOLE:
        print("    Use Session > Quit in the app (or Ctrl+C here) when finished.\n")

    with httpd:
        if not args.no_browser:
            open_app_window(args.tab)  # only after the socket is bound
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass
    print("[✓] Session safely saved. Shut down.")

if __name__ == "__main__":
    main()
