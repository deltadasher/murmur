#!/usr/bin/env python3
"""Exercise the daemon with an isolated audio server and synthetic microphone.

Usage: dbus-run-session -- python tests/integration.py BUILD_DIR SCRATCH_DIR
Requires pipewire, pipewire-pulse, wireplumber, pactl and pacat. This never
connects to the host audio server, records a real mic, or changes host defaults.
"""
import array
import json
import math
import os
from pathlib import Path
import socket
import stat
import subprocess
import sys
import time

build = Path(sys.argv[1]).resolve()
scratch = Path(sys.argv[2]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
runtime = scratch / "runtime"
runtime.mkdir(exist_ok=True, mode=0o700)
runtime.chmod(0o700)
config = scratch / "config"
wp = config / "wireplumber" / "wireplumber.conf.d"
wp.mkdir(parents=True, exist_ok=True)
(wp / "test.conf").write_text('''wireplumber.profiles = {
  main = {
    hardware.audio = disabled
    hardware.bluetooth = disabled
    hardware.video-capture = disabled
  }
}
''')
# Independent runs cannot inherit processingRequested from an earlier test.
settings = config / "Murmur" / "murmur.conf"
settings.unlink(missing_ok=True)
env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), PIPEWIRE_RUNTIME_DIR=str(runtime),
           XDG_CONFIG_HOME=str(config), XDG_STATE_HOME=str(scratch / "state"),
           PULSE_SERVER="unix:" + str(runtime / "pulse/native"),
           QT_QPA_PLATFORM="offscreen", MURMUR_NO_SYSTEMD="1")
for key in ("PIPEWIRE_REMOTE", "PIPEWIRE_CONFIG_DIR", "PIPEWIRE_CONFIG_PREFIX",
            "WIREPLUMBER_CONFIG_DIR", "WIREPLUMBER_PROFILE", "PULSE_SOURCE", "PULSE_SINK"):
    env.pop(key, None)
processes, logs, clients, checks = [], [], [], []
gui_supported = False
control_path = runtime / "murmur" / "control.sock"


def spawn(args, name):
    log = (scratch / (name + ".log")).open("wb")
    logs.append(log)
    p = subprocess.Popen(list(map(str, args)), env=env, stdout=log, stderr=subprocess.STDOUT)
    processes.append(p)
    return p


def run(*args, timeout=8):
    return subprocess.run(list(map(str, args)), env=env, check=True,
                          capture_output=True, timeout=timeout).stdout


def wait_until(predicate, description, seconds=10):
    deadline = time.monotonic() + seconds
    last_error = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except (subprocess.SubprocessError, OSError, ValueError) as e:
            last_error = str(e)
        time.sleep(.08)
    raise AssertionError("Timed out: " + description + ("; " + last_error if last_error else ""))


def sources():
    return json.loads(run("pactl", "-f", "json", "list", "sources"))


def virtuals():
    return [s for s in sources() if s["name"] == "murmur.microphone"]


class Client:
    def __init__(self):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(4)
        self.socket.connect(str(control_path))
        self.buffer = b""
        self.sequence = 0
        self.initial = self.read()
        assert self.initial["type"] == "state" and self.initial["protocol"] == 1
        clients.append(self)

    def close(self):
        self.socket.close()

    def read(self):
        while b"\n" not in self.buffer:
            part = self.socket.recv(65536)
            if not part:
                raise EOFError("Daemon closed control socket")
            self.buffer += part
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def reply(self, command_id=None):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            value = self.read()
            if value.get("type") == "reply" and (command_id is None or value.get("id") == command_id):
                return value
        raise AssertionError("No command reply")

    def command(self, command, expected=True, **fields):
        self.sequence += 1
        self.socket.sendall((json.dumps(dict(command=command, id=self.sequence, **fields)) + "\n").encode())
        reply = self.reply(self.sequence)
        assert reply["ok"] is expected, (command, fields, reply)
        if not expected:
            assert reply["error"], "Rejected command lacks explanation"
        return reply["state"]

    def state(self):
        return self.command("get")


def connect_daemon():
    # After a crash the old socket path exists before the replacement daemon
    # has removed it and begun listening. Retry the actual connection.
    connected = []
    def try_connect():
        connected.append(Client())
        return True
    wait_until(try_connect, "daemon socket connection")
    client = connected[0]
    wait_until(lambda: client.state()["audioReady"], "daemon audio discovery")
    return client


def wait_state(client, predicate, description, seconds=10):
    wait_until(lambda: predicate(client.state()), description, seconds)
    return client.state()


def record(name, tag, seconds=1.5):
    raw = scratch / (tag + ".raw")
    with raw.open("wb") as output:
        p = subprocess.Popen(["pacat", "--record", "--device=" + name,
                              "--format=float32le", "--rate=48000", "--channels=1",
                              "--latency-msec=20"], env=env, stdout=output, stderr=subprocess.PIPE)
        processes.append(p)
        time.sleep(seconds)
        p.terminate()
        _, error = p.communicate(timeout=5)
    samples = array.array("f")
    samples.frombytes(raw.read_bytes())
    assert len(samples) > 48000, f"Too little audio: {len(samples)} samples; {error!r}"
    samples = samples[24000:]  # Discard connection and control smoothing transients.
    assert all(math.isfinite(v) and abs(v) <= 1 for v in samples), "Invalid or clipped samples"
    return math.sqrt(sum(v * v for v in samples) / len(samples))


def stop_process(p):
    p.terminate()
    p.wait(timeout=5)
    assert p.returncode == 0, f"Process did not shut down cleanly: {p.returncode}"


def start_audio(client, **kwargs):
    client.command("start", source="murmur-test-tone", **kwargs)
    wait_state(client, lambda s: s["running"] and not s["starting"], "processing start")
    wait_until(lambda: len(virtuals()) == 1, "one virtual microphone")


def shutdown(client, daemon):
    client.command("shutdown")
    daemon.wait(timeout=5)
    assert daemon.returncode == 0
    client.close()
    wait_until(lambda: not virtuals() and not control_path.exists(), "clean shutdown")


try:
    assert (build / "murmur-daemon").is_file(), "Build the 0.2 daemon first"
    pipewire = spawn(["pipewire"], "pipewire")
    wait_until(lambda: (runtime / "pipewire-0").exists(), "private PipeWire socket")
    wireplumber = spawn(["wireplumber"], "wireplumber")
    pulse = spawn(["pipewire-pulse"], "pulse")
    wait_until(lambda: b"PipeWire" in run("pactl", "info"), "private PulseAudio bridge")
    tone = spawn([build / "tone-source"], "tone")
    wait_until(lambda: any(s["name"] == "murmur-test-tone" for s in sources()), "synthetic source")
    baseline_default = run("pactl", "get-default-source").strip()
    daemon = spawn([build / "murmur-daemon"], "daemon")
    a = connect_daemon()
    assert not a.initial["requested"] and not a.initial["running"]
    wait_state(a, lambda s: any(d["name"] == "murmur-test-tone" for d in s["sources"]), "source discovery")
    assert stat.S_IMODE(control_path.stat().st_mode) & 0o077 == 0, "Socket accessible by another user"
    assert stat.S_IMODE(control_path.parent.stat().st_mode) == 0o700
    start_audio(a)
    clean_rms = record("murmur.microphone", "clean")
    assert .11 < clean_rms < .17, f"Neutral gain incorrect: {clean_rms}"
    assert run("pactl", "get-default-source").strip() == baseline_default
    checks.extend(["same-user IPC permissions", "source discovery", "stable virtual microphone", "neutral audio", "unchanged default microphone"])

    # New effects must reach the actual stream, not just appear in state JSON.
    a.command("set", parameters={"compression": 100})
    compression_rms = record("murmur.microphone", "compression")
    assert .05 < compression_rms < clean_rms * .95, compression_rms
    a.command("set", parameters={"compression": 0, "drive": 65})
    drive_rms = record("murmur.microphone", "drive")
    assert drive_rms > clean_rms * 1.3, drive_rms
    a.command("set", parameters={"drive": 0, "echo": 35, "echoMs": 100})
    echo_rms = record("murmur.microphone", "echo")
    assert echo_rms > clean_rms * 1.2, echo_rms  # 440 Hz aligns after 100 ms.
    a.command("set", parameters={"echo": 0, "highpass": 300})
    highpass_rms = record("murmur.microphone", "highpass")
    assert highpass_rms < clean_rms * .98, highpass_rms
    a.command("set", parameters={"highpass": 20})
    stop_process(tone)
    wait_state(a, lambda s: not s["running"] and not s["starting"], "quiet test source unplug")
    tone = spawn([build / "tone-source", "0.005"], "quiet-tone")
    wait_state(a, lambda s: s["running"], "quiet test source reconnect")
    quiet_rms = record("murmur.microphone", "quiet-gate-off")
    assert .002 < quiet_rms < .0045, quiet_rms
    a.command("set", parameters={"gate": -40})
    gate_rms = record("murmur.microphone", "quiet-gated")
    assert gate_rms < 1e-5, gate_rms
    a.command("set", parameters={"gate": -80})
    stop_process(tone)
    wait_state(a, lambda s: not s["running"] and not s["starting"], "restore test source unplug")
    tone = spawn([build / "tone-source"], "restored-tone")
    wait_state(a, lambda s: s["running"], "restore test source reconnect")
    checks.extend(["live compressor reduces loud speech", "live drive adds saturation",
                   "live echo amount/time alter synthetic audio", "live rumble filter attenuates",
                   "live gate suppresses quiet input; neutral controls restored"])

    # System source mute is independent of Murmur's DSP mute. Observe the
    # native control and restore only this virtual source on explicit request.
    wait_state(a, lambda s: s["systemMuteKnown"], "system mute discovery")
    assert not a.state()["systemMuted"] and not a.state()["parameters"]["mute"]
    mute_observer = Client()
    run("pactl", "set-source-mute", "murmur.microphone", "1")
    deadline = time.monotonic() + 5
    while True:
        update = mute_observer.read()
        if update.get("type") == "state" and update["systemMuteKnown"] and update["systemMuted"]:
            break
        assert time.monotonic() < deadline, "System mute change was not broadcast"
    mute_observer.close()
    state = wait_state(a, lambda s: s["systemMuteKnown"] and s["systemMuted"], "external system mute")
    assert not state["parameters"]["mute"]
    system_mute_rms = record("murmur.microphone", "system-muted")
    assert system_mute_rms < 1e-5
    a.command("unmute-system")
    state = wait_state(a, lambda s: s["systemMuteKnown"] and not s["systemMuted"], "explicit system unmute")
    assert not state["parameters"]["mute"] and not virtuals()[0]["mute"]
    system_unmute_rms = record("murmur.microphone", "system-unmuted")
    assert abs(system_unmute_rms - clean_rms) < .012
    assert run("pactl", "get-default-source").strip() == baseline_default
    # Clearing the external mute must never clear the user's independent DSP
    # mute, even when both are active at the same time.
    a.command("set", parameters={"mute": True})
    run("pactl", "set-source-mute", "murmur.microphone", "1")
    wait_state(a, lambda s: s["systemMuted"], "both mute layers active")
    a.command("unmute-system")
    state = wait_state(a, lambda s: s["systemMuteKnown"] and not s["systemMuted"], "unmute while DSP is muted")
    assert state["parameters"]["mute"] and record("murmur.microphone", "DSP-still-muted") < 1e-5
    a.command("set", parameters={"mute": False})
    checks.extend(["external system mute observed independently of app mute", "explicit system unmute restores audio without changing defaults"])

    duplicate = subprocess.run([str(build / "murmur-daemon")], env=env, capture_output=True, timeout=5)
    assert duplicate.returncode == 1 and b"already running" in duplicate.stderr
    assert len(virtuals()) == 1 and daemon.poll() is None
    checks.append("duplicate daemon blocked")

    # Two simultaneous controllers get authoritative parameter updates.
    b = Client()
    a.command("set", parameters={"bass": 4, "mid": -2, "treble": 3, "rate": 12000,
                                  "bits": 6, "mix": 65, "gain": -3})
    deadline = time.monotonic() + 5
    while True:
        update = b.read()
        if update.get("type") == "state" and update["parameters"]["bits"] == 6:
            break
        assert time.monotonic() < deadline, "Second controller missed the change"
    assert b.state()["parameters"] == a.state()["parameters"]
    grit_rms = record("murmur.microphone", "grit")
    assert .025 < grit_rms < clean_rms * .9
    checks.extend(["live EQ and sample-rate/bit-depth/gain edits", "concurrent client synchronization"])

    a.command("set", parameters={"bypass": True})
    bypass_rms = record("murmur.microphone", "bypass")
    assert abs(bypass_rms - clean_rms) < .012, f"Bypass failed: {bypass_rms}"
    a.command("set", parameters={"mute": True})
    mute_rms = record("murmur.microphone", "mute")
    assert mute_rms < 1e-5, f"Mute failed: {mute_rms}"
    a.command("set", parameters={"mute": False, "bypass": False})
    checks.extend(["audible bypass", "mute overrides bypass"])

    old = a.state()["parameters"]
    for invalid in ({"bits": 3.5}, {"bass": 19}, {"rate": 0}, {"mute": "true"},
                    {"unknown": 1}, {"version": 2}, {"gain": -8, "bits": 1},
                    {"mid": None}, {"pitch": [1]}, {"rate": "12000"}, {"echo": 61}, {"gate": -81}, {"highpass": 10}, {"echoMs": 601}, {"compression": 101}, {"drive": -1}):
        a.command("set", expected=False, parameters=invalid)
        assert a.state()["parameters"] == old, "Invalid patch partially changed controls"
    a.command("set", expected=False, parameters=[])
    a.command("monitor", expected=False, enabled="true")
    a.command("unknown-command", expected=False)
    a.socket.sendall(b"{broken JSON}\n[]\n")
    assert not a.reply()["ok"] and not a.reply()["ok"]
    assert a.state()["parameters"] == old
    oversized = Client()
    oversized.socket.sendall(b"x" * 65537)
    try:
        while oversized.socket.recv(65536):
            pass
    except ConnectionResetError:
        pass
    oversized.close()
    assert a.state()["running"] and daemon.poll() is None
    checks.append("malformed and oversized IPC rejected; controls validated atomically")

    # Closing every controller must neither stop the audio nor reset its settings.
    a.close()
    b.close()
    time.sleep(.3)
    after_close_rms = record("murmur.microphone", "after-client-close")
    assert abs(after_close_rms - grit_rms) < .012
    a = Client()
    assert a.initial["running"] and a.initial["parameters"] == old
    checks.append("daemon survives closing and reopening controllers")

    # Exercise the actual window's close path when the GUI test hook is present.
    if (build / "murmur").is_file():
        help_text = run(build / "murmur", "--help").decode()
        if "--seconds" in help_text:
            gui_supported = True
            gui = spawn([build / "murmur", "--seconds", "0.5"], "gui-close")
            gui.wait(timeout=10)
            assert gui.returncode == 0 and a.state()["running"]
            checks.append("closing the real graphical app preserves audio")

    # SIGTERM is an intended service restart, so processingRequested survives.
    stop_process(daemon)
    a.close()
    wait_until(lambda: not virtuals(), "SIGTERM audio cleanup")
    daemon = spawn([build / "murmur-daemon"], "daemon-restarted")
    a = connect_daemon()
    state = wait_state(a, lambda s: s["running"], "processing resumes after service restart")
    assert state["requested"] and state["source"] == "murmur-test-tone" and state["parameters"] == old
    checks.append("chosen source and controls resume after service restart")

    # Removing the chosen source must wait for that same source without falling back.
    stop_process(tone)
    state = wait_state(a, lambda s: not s["running"] and not s["starting"], "source unplug")
    assert state["requested"] and state["source"] == "murmur-test-tone"
    wait_until(lambda: not virtuals(), "unplug virtual source cleanup")
    time.sleep(2.3)
    assert not a.state()["running"] and daemon.poll() is None
    tone = spawn([build / "tone-source"], "tone-reconnected")
    wait_state(a, lambda s: s["running"], "same source reconnect")
    assert len(virtuals()) == 1
    checks.append("unplug waits for the selected source and resumes on its return")

    # Explicit Stop stays off across a daemon restart and microphone reconnect.
    a.command("stop")
    wait_until(lambda: not virtuals(), "explicit stop")
    assert not a.state()["systemMuteKnown"]
    a.command("unmute-system", expected=False)
    stop_process(daemon)
    a.close()
    daemon = spawn([build / "murmur-daemon"], "daemon-stopped-restart")
    a = connect_daemon()
    assert not a.state()["requested"] and not a.state()["running"]
    stop_process(tone)
    tone = spawn([build / "tone-source"], "tone-after-stop")
    wait_state(a, lambda s: any(d["name"] == "murmur-test-tone" for d in s["sources"]), "source returns while stopped")
    time.sleep(2.3)
    assert not a.state()["running"] and not virtuals()
    checks.append("explicit Stop remains off after restart and input reconnect")

    # Preview may only play into this session's dummy output.
    sinks = json.loads(run("pactl", "-f", "json", "list", "sinks"))
    assert len(sinks) == 1 and sinks[0]["name"] == "auto_null"
    start_audio(a, monitor=True)
    preview_rms = record("auto_null.monitor", "preview")
    assert .025 < preview_rms < .2
    a.command("monitor", enabled=False)
    wait_state(a, lambda s: s["running"] and not s["monitor"], "preview off")
    checks.append("headphone preview uses only the private dummy output")

    # Restart the audio server itself while keeping the daemon alive. The daemon
    # must reconnect to the same selected source, never a default replacement.
    stop_process(tone)
    stop_process(pulse)
    stop_process(wireplumber)
    stop_process(pipewire)
    state = wait_state(a, lambda s: not s["audioReady"], "audio server disconnection")
    assert state["requested"] and state["source"] == "murmur-test-tone"
    pipewire = spawn(["pipewire"], "pipewire-restarted")
    wait_until(lambda: (runtime / "pipewire-0").exists(), "restarted PipeWire socket")
    wireplumber = spawn(["wireplumber"], "wireplumber-restarted")
    pulse = spawn(["pipewire-pulse"], "pulse-restarted")
    wait_until(lambda: b"PipeWire" in run("pactl", "info"), "restarted Pulse bridge")
    tone = spawn([build / "tone-source"], "tone-after-server-restart")
    state = wait_state(a, lambda s: s["running"], "audio server automatic reconnection", seconds=15)
    assert state["source"] == "murmur-test-tone" and len(virtuals()) == 1
    reconnect_rms = record("murmur.microphone", "after-server-restart")
    assert abs(reconnect_rms - grit_rms) < .012
    checks.append("audio server restart reconnects the chosen source and preserves processing")

    daemon.kill()
    daemon.wait(timeout=5)
    a.close()
    wait_until(lambda: not virtuals(), "crash source cleanup")
    daemon = spawn([build / "murmur-daemon"], "daemon-crash-recovered")
    a = connect_daemon()
    wait_state(a, lambda s: s["running"], "stale socket and lock recovery")
    assert len(virtuals()) == 1
    checks.extend(["crash removes audio nodes", "stale socket/lock recover without duplicate worker"])
    shutdown(a, daemon)
    daemon = spawn([build / "murmur-daemon"], "daemon-after-shutdown")
    a = connect_daemon()
    assert not a.state()["requested"] and not a.state()["running"]
    shutdown(a, daemon)
    checks.append("explicit Quit clears processing and exits cleanly")

    if gui_supported:
        gui = spawn([build / "murmur", "--seconds", "1.2"], "gui-launch-service")
        a = connect_daemon()
        gui.wait(timeout=10)
        assert gui.returncode == 0
        state = a.state()
        assert not state["requested"] and not state["running"]
        assert state["daemonPid"] != gui.pid
        a.command("shutdown")
        a.close()
        wait_until(lambda: not control_path.exists() and not virtuals(), "app-launched daemon shutdown")
        checks.append("opening the app launches its independent background service")

    report = dict(result="PASS", sample_rate=48000, channels=1, clean_rms=clean_rms,
                  grit_rms=grit_rms, bypass_rms=bypass_rms, mute_rms=mute_rms,
                  after_close_rms=after_close_rms, preview_rms=preview_rms,
                  reconnect_rms=reconnect_rms, checks=checks)
    report.update(system_mute_rms=system_mute_rms, system_unmute_rms=system_unmute_rms, compression_rms=compression_rms, drive_rms=drive_rms, echo_rms=echo_rms, highpass_rms=highpass_rms, quiet_rms=quiet_rms, gate_rms=gate_rms)
    (scratch / "integration-result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
except Exception as e:
    report = dict(result="FAIL", error=str(e), completed_checks=checks)
    (scratch / "integration-result.json").write_text(json.dumps(report, indent=2) + "\n")
    raise
finally:
    # Also reap a daemon started detached by the GUI, including on assertion failure.
    if control_path.exists():
        try:
            cleanup = Client()
            cleanup.command("shutdown")
            cleanup.close()
        except (OSError, EOFError, AssertionError):
            pass
    for client in clients:
        client.close()
    for p in reversed(processes):
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=4)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
    for log in logs:
        log.close()
