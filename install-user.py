#!/usr/bin/env python3
"""Install the current build as a native user app, without login activation."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--prefix", type=Path, default=Path.home() / ".local")
parser.add_argument("--config-dir", type=Path, default=Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")))
parser.add_argument("--state-dir", type=Path, default=Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")))
parser.add_argument("--no-service-reload", action="store_true", help="For staged installs only; skip systemctl.")
args = parser.parse_args()
project = Path(__file__).resolve().parent
prefix = args.prefix.expanduser().resolve()
config = args.config_dir.expanduser().resolve()
state = args.state_dir.expanduser().resolve() / "murmur"
binary_dir = project / "bin"
if not all((binary_dir / name).is_file() for name in ("murmur", "murmur-daemon")):
    binary_dir = project / "build"
if not all((binary_dir / name).is_file() for name in ("murmur", "murmur-daemon")):
    parser.error("Build the app and daemon first; neither bin/ nor build/ contains both executables.")

def systemd_quote(value):
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%") + '"'

def desktop_quote(value):
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"').replace("$", "\\$").replace("~", "\\~") + '"'

desktop = (project / "packaging/murmur.desktop").read_text()
desktop = desktop.replace("Exec=murmur", "Exec=" + desktop_quote(prefix / "bin/murmur"))
desktop = desktop.replace("Icon=murmur", "Icon=" + str(prefix / "share/icons/hicolor/scalable/apps/murmur.svg"))
unit = (project / "packaging/murmur.service.in").read_text().replace(
    "@CMAKE_INSTALL_FULL_BINDIR@/murmur-daemon", systemd_quote(prefix / "bin/murmur-daemon"))
files = {
    prefix / "bin/murmur": ((binary_dir / "murmur").read_bytes(), 0o755),
    prefix / "bin/murmur-daemon": ((binary_dir / "murmur-daemon").read_bytes(), 0o755),
    prefix / "share/applications/murmur.desktop": (desktop.encode(), 0o644),
    prefix / "share/icons/hicolor/scalable/apps/murmur.svg": ((project / "packaging/murmur.svg").read_bytes(), 0o644),
    config / "systemd/user/murmur.service": (unit.encode(), 0o644),
}
state.mkdir(parents=True, exist_ok=True)
backup = state / ("install-backup-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
manifest = {}
for index, (path, (content, mode)) in enumerate(files.items()):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() or path.is_symlink():
        backup.mkdir(exist_ok=True)
        saved = backup / (str(index) + "-" + path.name)
        shutil.copy2(path, saved)
        manifest[str(path)] = str(saved)
    descriptor, temporary = tempfile.mkstemp(prefix=".murmur-install-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
if manifest:
    (backup / "restore-map.json").write_text(json.dumps(manifest, indent=2) + "\n")
(state / "install.json").write_text(json.dumps({
    "version": "0.3.0",
    "files": {str(path): hashlib.sha256(content).hexdigest() for path, (content, _) in files.items()},
    "backup": str(backup) if manifest else None,
    "login_start": False
}, indent=2) + "\n")
if not args.no_service_reload:
    subprocess.run(["systemctl", "--user", "disable", "murmur.service"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    reload = subprocess.run(["systemctl", "--user", "daemon-reload"], check=False)
    if reload.returncode:
        print("Files installed; reload the user service manager when your desktop session is available.")
    updater = shutil.which("update-desktop-database")
    if updater:
        subprocess.run([updater, str(prefix / "share/applications")], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("Murmur 0.3.0 installed for this user. Open Murmur from the app launcher.")
print("The background service starts when the app opens. Login startup is disabled.")
if manifest:
    print("Previous files backed up:", backup)
for path in files:
    print(path)
