# Murmur

Murmur lets you change how your microphone sounds in Discord, OBS, or games.
Pick **Murmur Virtual Microphone** as your input after starting it. The audio
keeps running in the background when you close the window.

Right now, Murmur supports Linux with PipeWire and WirePlumber. It works with
Wayland and X11. Version **0.3.0** adds four effect sections and six controls.

![Murmur voice console](docs/screenshots/voice-console.png)

*Example mode: no microphone audio. Meter values in this screenshot are samples.*

## Start talking

1. Open Murmur from your app launcher. Its service starts on demand.
2. Choose your physical microphone and click **Start microphone**.
3. Select **Murmur Virtual Microphone** as the input in your voice-chat app.
4. Start with **Comms** for team chat, or **Clean** for an unaltered signal.
   Choose Tone, Character, Cleanup or Space to adjust your sound.
5. Close the window when finished adjusting. Native audio continues running.
   Flatpak asks your desktop for background access; keep the window open if the
   permission is denied. Murmur displays the actual permission state.

Routing, input/output meters, Effect mix, Output level, Mute, Bypass and headphone
preview stay visible while you change modules. Compact windows use the same
four modules and a preset selector. Sliders support keyboard arrow keys and
**double-click to reset that control**. Numeric fields allow precise values.

**Stop microphone** removes the virtual input and remembers that you stopped it.
**Mute microphone** keeps it connected and sends silence. The system can mute the
virtual input separately; Murmur shows a warning with an explicit **Unmute system
input** action. This preserves Murmur's own Mute switch. Internal meters can
still show processing activity while system mute blocks delivery to other apps.

## Shape your voice

| Module | Controls | Purpose |
| --- | --- | --- |
| Tone | Bass, Middle, Treble: −18 to +18 dB; adjustable band frequencies | Body, speech presence and clarity |
| Character | Pitch: −12 to +12 semitones; Drive: Off–100% | Voice height and saturation |
| Character | Sample rate: 1,000–48,000 Hz; Bit depth: 2–24 bits | Digital texture and quantization grit |
| Cleanup | Low cut: Off–300 Hz | Reduces low rumble |
| Cleanup | Noise gate: Off, or −79 to −20 dB | Reduces sound below the chosen threshold |
| Cleanup | Compression: Off–100% | Softens louder speech above −18 dB, up to 6:1 |
| Space | Echo: Off–60%; Echo time: 80–600 ms | Blended, repeating delay |
| Always available | Effect mix: 0–100%; Output level: −24 to +12 dB | Original/processed blend and final gain |

The Tone graph calculates the three EQ bands only. The Space diagram shows
configured echo timing and decay; it is not a live waveform. Frequency fields
move the bass shelf, middle bell and treble shelf.

Low cut is bypassed at its Off position (20 Hz). The gate is a threshold gate,
not AI noise cancellation: too high a threshold can cut quiet words. Compression
uses a fixed −18 dB threshold with no automatic makeup gain. Echo feedback is
fixed at 35%; the amount and time sliders control its wet level and spacing.
Mute and Bypass clear stored echo to avoid playing earlier speech afterward.

Sample rate and bit depth are audible effects. Audio transport remains 48 kHz
mono float32 for compatibility; these sliders do not change hardware format,
file encoding or speaking speed. Rate reduction is off at 48 kHz and bit crushing
is off at 24 bits. Pitch uses a delay-based effect and can sound grainy at large
shifts. That stage adds up to about 44 ms; complete headset-to-app latency has
not been measured.

**Clean, Comms, Warm, Bright, Telephone, Digital grit and Deep** are editable
starting points. Comms applies low cut, a mild gate, compression and a small
presence boost. Import/export JSON presets from the application menu. Older
version-1 presets work, with the new effects defaulting to Off. Changing presets
preserves session Mute and Bypass.

The clipping guard leaves samples below 0.95 unchanged and softly limits higher
peaks. Reduce Output level or EQ boosts if it activates often. **Hear myself**
previews the processed voice through the default output; use headphones to avoid
feedback. Enabling or disabling preview briefly reconnects the streams.

## Build and install

On Arch, install the build/runtime dependencies:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base dbus \
  pipewire wireplumber pipewire-pulse
```

This source requires C++20, Qt 6.7+ (Core, DBus, Network, Widgets; Test for tests),
PipeWire development files 0.3.65+, and a running user PipeWire/WirePlumber session.
Older Qt packages may also need `qt6-wayland`. A PulseAudio-only session is not
supported; `pipewire-pulse` provides compatibility for PulseAudio clients.

From the source directory:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/murmur
```

Install the launcher, icon, two binaries and user service without root:

```sh
python3 install-user.py
```

The installer uses `bin/` in a runnable release, or your local `build/` otherwise.
It backs up replaced files under `$XDG_STATE_HOME/murmur/install-backup-*`,
preserves settings, and leaves login startup disabled. On an upgrade, restart
an already running worker to activate the new processing code:

```sh
systemctl --user restart murmur.service
```

Restarting briefly reconnects the virtual microphone. Preview starts Off after
worker restarts. Native distribution packages use the CMake install target; an
Arch recipe accompanies the source release. Use one installation method to avoid
an older personal launcher overriding a system package.

For **Flatpak**, build instructions, permissions, sandbox lifecycle and public
release preparation, see [PACKAGING.md](PACKAGING.md). The developer bundle uses
a provisional application ID that needs to be updated before public Flatpak
distribution. Murmur is not listed on a store yet.

Qt chooses the display server for your session. You can choose explicitly:

```sh
QT_QPA_PLATFORM=wayland ./build/murmur
QT_QPA_PLATFORM=xcb ./build/murmur
```

## Background service and command line

```sh
murmur --status
murmur --list-devices
murmur --start --source YOUR_MIC_NODE_NAME --preset Comms
murmur --set bits=8 --set rate=12000 --set drive=20
murmur --set highpass=90 --set gate=-50 --set compression=35
murmur --set echo=25 --set echoMs=180
murmur --set mute=true
murmur --stop
murmur --quit-daemon
```

`--start` launches the worker when necessary. `--status` and other read-only
commands never resurrect a stopped service. `--headless` is an alias for
`--start`. On a CLI start, `--seconds 10` stops the mic after ten seconds; with a
window, it closes the window without stopping audio. `--monitor` enables preview
when starting. Invalid control patches are rejected atomically.

Settings and the selected microphone belong to the daemon. Reopening the window
shows the current controls. If the selected microphone disappears, the worker
waits for that same microphone instead of selecting another one. It recovers the
last requested configuration after a worker crash or PipeWire restart, when the
selected device is available. An explicit Stop remains stopped across restarts.

The native worker has no Qt GUI or display-server dependency. `murmur.service`
is a user service with `Restart=on-failure`; an explicit **Stop background
service** exits cleanly. Login startup remains disabled. In unpacked builds the
controller starts its sibling daemon directly. Flatpak uses a second instance of
the same sandbox and the desktop Background portal, without host systemd.

Control traffic uses owner-only Unix sockets, and a kernel file lock allows only
one worker per installation scope. Native settings live at
`$XDG_CONFIG_HOME/Murmur/murmur.conf` (normally `~/.config/Murmur/murmur.conf`).
Flatpak has separate settings and IPC. Avoid running native and Flatpak workers
simultaneously: they advertise the same virtual microphone name.

Audio processing is local. Murmur contains no microphone-recording, telemetry,
cloud-processing or network feature. Audio callbacks use preallocated buffers;
control and persistence work runs outside those callbacks.

## Troubleshooting

Do not run Murmur as root. Inspect the user audio session:

```sh
systemctl --user status pipewire wireplumber pipewire-pulse murmur
journalctl --user -u murmur.service
murmur --status
```

If disconnected, open Murmur or choose **Start service**. If Discord hears nothing,
check the selected input, Start/Stop state, both Murmur and system mute, Output
level, and the gate threshold. Test with Clean. Murmur does not change your system
input default or your voice-chat application's settings.

## Development and verification

Four CTest suites cover DSP behavior, model/preset validation, native UI
interaction and the Background portal. A private integration harness starts
isolated audio servers with hardware monitoring disabled and captures only a
synthetic microphone:

```sh
dbus-run-session -- python3 tests/integration.py ./build /tmp/murmur-audio-test
```

It requires PipeWire, WirePlumber, `pactl` and `pacat` (`libpulse` on Arch).
See [VALIDATION.md](VALIDATION.md) for measured results and remaining limits,
[CONTRIBUTING.md](CONTRIBUTING.md) for development expectations,
[SECURITY.md](SECURITY.md) for privacy/security boundaries, and
[PROVENANCE.md](PROVENANCE.md) for generated-source disclosure. Licensed under
[MIT](LICENSE).
