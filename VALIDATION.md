# Validation — Murmur 0.3.0 — 2026-10-01

Native build: Arch Linux x86_64, GCC 16.2, Qt 6.11.2, PipeWire 1.6.9,
WirePlumber 0.5.18. Flatpak build: KDE SDK/Platform 6.10, Qt 6.10.3,
PipeWire client 1.4.9. These results describe this implementation and test host;
they are not a claim of store approval or universal desktop compatibility.

| Check | Result |
| --- | --- |
| Native Release build with warnings enabled | Passed |
| DSP, model/preset, native UI and Background portal CTest suites | All four passed |
| Private native audio/lifecycle integration | 29 checks passed |
| Real Flatpak build, tests and export | Built from source; all four suites passed in the SDK |
| Actual sandbox audio/lifecycle integration | 14 checks passed with a synthetic source and private portal fixture |
| Native Wayland and X11 (`xcb`) rendering | Both inspected on the current host |
| Visual inspection | 1220×840, 640×720 and 520×680; all four modules, expanded frequencies, custom effects, keyboard focus, offline and mute/background recovery |
| User installation | 0.3.0 launcher, icon, app, daemon and service installed; desktop file validated |
| Installed controllers close while audio continues | Passed on Wayland and xcb |
| Update preserves existing settings | Selected headset, prior controls, requested/running state, preview, system mute and default input preserved |
| New effects after upgrade | Neutral defaults; worker restarted to load new DSP |
| Login startup | Disabled |
| Physical headset/call recording | None |

## DSP and native controls

Tests exercise neutral controls, bypass/mute priority, non-finite input handling,
output guarding, rumble attenuation, gate threshold/hold, loud-vs-quiet
compression, drive harmonics, echo timing/decay and clearing stored speech when
mute or bypass begins. Preset/IPC tests cover old version-1 presets, range and
whole-bit validation, atomic rejection, and native/Flatpak socket isolation.

UI tests cover offline recovery, native keyboard module navigation, slider arrow
keys and per-control reset, compact field visibility, session switch preservation,
unfinished numeric edits, all six new control patches, independent system-unmute
commands, and truthful permission-denied guidance. The private portal suite tests
grants, denials, cancellation, malformed replies, alternate handles, method errors,
timeouts and the native path that does not request desktop permission.

## Private native audio measurements

`integration-result.json` records the complete 29-check result. The test session
starts private PipeWire, WirePlumber and PulseAudio-compatible servers, disables
hardware/Bluetooth/camera monitoring and captures only a generated 440 Hz mono
source at 48 kHz. Input amplitude 0.2 has expected neutral RMS ≈0.141421.
Each effect measurement uses its own controlled settings; compare only within
that scenario.

| Scenario | Measured RMS |
| --- | --- |
| Neutral | 0.141401 |
| Compression at 100% | 0.102549 |
| Drive at 65% | 0.448569 |
| Echo at 35%, 100 ms | 0.206435 |
| Low cut at 300 Hz | 0.128232 |
| Quiet source before gate | 0.003535 |
| Quiet source with gate | 3.45 × 10⁻¹⁰ |
| Digital grit | 0.095259 |
| Bypass | 0.141418 |
| Murmur mute | 2.35 × 10⁻²³ |
| External system mute | 0.0 |
| Explicit system unmute | 0.141443 |

The integration scenario also verifies owner-only IPC, device discovery, stable
virtual-source identity, unchanged default input, duplicate-worker prevention,
live EQ/rate/bit/gain edits, synchronized controllers, malformed/oversized IPC,
controller and real GUI closure, worker restart, selected-source unplug/replug,
explicit Stop across restarts, private headphone preview, audio-server restart,
crash-node cleanup, stale socket recovery, service quit and GUI-driven launch.
System mute changes are observed independently; unmuting the virtual source does
not clear Murmur's own Mute switch.

## Flatpak proof and boundaries

The developer manifest was built with a signature-verified Arch flatpak-builder
in an isolated workspace installation. D-Bus test tools are built from the pinned
official 1.16.2 source solely for tests and removed before export. Murmur's source
compiled without warnings in the SDK. No host packages, normal user Flatpak
installation or physical audio configuration were replaced by these checks.

`flatpak-integration-result.json` records fourteen actual sandbox checks. Separate
sandbox processes successfully launch the worker, connect to a synthetic source,
change real audio, mute, close/reopen editors, synchronize concurrent editors,
reject duplicate workers across PID namespaces, resume after source reconnect,
preserve explicit Stop, quit the worker and restore settings on relaunch.
Clean RMS was 0.141414, drive RMS 0.338420 and muted RMS ≈5.51 × 10⁻²³.
Final Flatpak example windows were rendered and inspected at 1220×840 on
Wayland and 520×680 with forced X11 permissions. Both used no-audio mode and
disabled the document portal for test output; this does not verify file selection.

The sandbox test removes host PipeWire access and uses a private D-Bus Background
portal that grants access with autostart disabled. That proves the permission
request and worker mechanics, not real KDE/GNOME policy approval. Actual desktop
permission dialogs, preset import/export through the file chooser portal, logout
behavior, Flatpak update behavior and a separate pure-Xorg desktop remain to be
verified. The developer identity is provisional; public repository, publisher
identity, hosted metadata URLs and security contact remain unassigned.

## Rendered product review

Installed frontend-design, design-taste-frontend and finishing guidance informed
the composition, with an independent read-only design review. Native rendered
pixels were inspected, not inferred from source. Tone, Character, Cleanup and
Space use persistent module navigation; routing, meters and session controls
remain accessible. Compact warnings prioritize editable controls over the EQ
readout. Follow-up fixes addressed the compact graph collision, keyboard tab
focus and unconditional background-lifecycle wording.

Screenshots in `docs/screenshots/` are explicitly **example/no-audio mode**.
Their sample levels illustrate layout rather than measured microphone activity.
The X11 checks use xcb/XWayland available in the current Wayland session, not a
separate Xorg desktop. UI visual acceptance belongs to the user.

Physical headset-to-app latency, sustained CPU usage while gaming, unusual
multichannel hardware, subjective voice quality and a listening test in Discord
remain unmeasured. The current host headset was discovered and restored through
the upgrade, but neither it nor a call was recorded for this validation.
