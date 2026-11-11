# Packaging Murmur

Murmur has a native CMake install and a developer Flatpak manifest. The Flatpak
files are a local development foundation, not a Flathub submission or a claim
that Murmur is published on Flathub.
The Flatpak application ID is provisional and is not a verified publisher identity.

## Native builds

Build dependencies are CMake 3.20+, a C++20 compiler, Ninja, Qt 6.7+ (Core,
DBus, Network, Widgets and Test), PipeWire development files 0.3.65+, and
`dbus-run-session` for the private portal tests.
The native Arch build is covered by `.github/workflows/ci.yml`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
DESTDIR="$PWD/build/stage" cmake --install build --prefix /usr
```

Use the staged tree for distribution packages. The supplied user service is
installed for native distributions; installing it does not enable login startup.
`install-user.py` is for a personal native install and is not used inside Flatpak.

## Developer Flatpak

`packaging/io.github.murmur_voice.Murmur.json` builds the local source directory
with the KDE 6.10 runtime and SDK. The provisional ID
`io.github.murmur_voice.Murmur` is for local testing only. It does **not** assert
ownership of a GitHub account named `murmur-voice`. Choose an owned domain or the
actual GitHub publisher's ID before creating public package history, since ID
changes affect application data and store identity.

Install `flatpak` and `flatpak-builder` using your distribution. After configuring
the Flathub remote, build from the repository root:

```sh
flatpak install --user flathub org.kde.Platform//6.10 org.kde.Sdk//6.10
flatpak-builder --user --force-clean --repo=repo-flatpak build-flatpak \
  packaging/io.github.murmur_voice.Murmur.json
flatpak-builder --user --install --force-clean build-flatpak \
  packaging/io.github.murmur_voice.Murmur.json
flatpak run io.github.murmur_voice.Murmur
```

The manual GitHub Flatpak workflow performs a real sandbox build, runs the
unit/UI suites and exports a developer bundle. It does not publish anything.
Check the workflow result before treating its bundle as tested.

### Sandbox permissions

The manifest requests Wayland, X11 as a fallback, shared IPC for X11, rendering
devices, and read-only access to the `pipewire-0` socket. PipeWire socket access
is sufficient to connect and grants access to audio according to the host
session manager's policy. Read-only here describes the socket's filesystem
mount; it is not a promise of read-only microphone processing.

Murmur does not request broad host/home filesystem access, network access, all
devices, unfiltered D-Bus access, or permission to execute host commands. The KDE
runtime additionally inherits read-only `kdeglobals` preferences and its standard
menu/settings notification bus access. Preset file
selection uses Qt's desktop file chooser integration; check portal support on
the target desktop. App settings stay under `~/.var/app/APP_ID/`.

Murmur's native PipeWire client identifies its media category as `Manager`, so
WirePlumber can allow creation and control of its virtual source. This access is
necessary for an audio processor and means the sandbox has audio graph access.
It should be declared accurately in any store permission review.

### Audio lifecycle and IPC

The GUI launches `/app/bin/murmur-daemon` using `flatpak-spawn` in another instance
of the same sandbox, without `--host` or `--watch-bus`. The worker requests
Background portal permission when audio becomes active, with login autostart
disabled. Closing the editor leaves the worker independent, subject to the
desktop's background policy. If permission is denied or unavailable, Murmur shows
why you should keep the editor open. There is no host systemd dependency.
Use Murmur's Stop control to stop processing or its explicit service quit action
to end the worker. `flatpak kill APP_ID` is an emergency way to end all instances.

Each instance uses `$XDG_RUNTIME_DIR/app/$FLATPAK_ID/murmur/control.sock`, with
owner-only directory/socket access. A kernel file lock protects the worker across
separate PID namespaces. Flatpak and native installs intentionally use separate
settings and control sockets; avoid running both audio workers simultaneously,
as both advertise Murmur's virtual microphone name.

Before shipping, verify all of these on a real sandbox installation:

- Microphone discovery, processed audio in a separate voice-chat client, and
  explicit Stop and mute behavior.
- Closing and reopening the editor while the worker keeps running, including
  two simultaneous editor launches and crash recovery without duplicate nodes.
- Wayland and X11 startup, preset import/export through portals, and the host's
  microphone privacy indicators.
- Logout, Flatpak updates, full service quit and reopening afterward. An already
  running worker uses the version it started with until it is restarted.

## Public release preparation

The local developer manifest is AI-authored. Current Flathub policy forbids
AI-generated or AI-assisted submission manifests and requires disclosure of other
generated material. A human publisher must independently author compliant Flathub
packaging and provide the required disclosure; this developer manifest must not be
submitted as-is. See [PROVENANCE.md](PROVENANCE.md) and the linked current policy.
Ordinary self-hosted Flatpak distribution and GitHub source hosting are separate
from Flathub's submission process.

1. Select an application ID owned by the publisher. Keep the manifest ID, metadata ID, developer identity, desktop
   launchable and icon name consistent.
2. Add real homepage, issue tracker, source and private security-report links to
   the metadata and documentation. Include hosted screenshots of the actual app.
   Do not insert placeholder URLs to silence validation warnings.
3. Tag the tested source release and publish its source archive. Replace the local
   directory source in the manifest with an HTTPS archive URL and its SHA-256,
   or a Git URL pinned to the release commit. Build exclusively from source.
4. Check the latest supported runtime and current Flathub requirements, then run
   the AppStream validator, desktop validator and Flathub builder linter. Resolve
   release metadata issues and permissions review before submitting.
5. Keep the manifest at the root of the Flathub submission, named after the final
   ID. Upstream source and binaries do not belong in the Flathub submission.

`python3 packaging/validate.py` checks local packaging consistency without network
access. `appstreamcli validate --no-net packaging/*.metainfo.xml` validates the
metadata schema. Missing real URLs and hosted screenshots are expected publication
warnings until a publisher is assigned; they are outstanding release work.

The developer Flatpak was built in an isolated workspace installation using
KDE SDK 6.10 (Qt 6.10.3, PipeWire client 1.4.9) and an extracted, signature-verified
Arch `flatpak-builder` 1.4.12. All four CTest suites passed in the SDK. The manifest
builds D-Bus 1.16.2 test tools from a pinned official source archive because the SDK
lacks a private test bus; those tools are removed from the exported app.

Fourteen actual sandbox checks passed against a private PipeWire/D-Bus session
and a synthetic microphone. They cover real audio/effect changes, mute, GUI
closure, concurrent editors, duplicate-worker locking, source reconnect, explicit
Stop, service quit and restored settings. The host audio socket was removed from
the test sandbox. `flatpak-integration-result.json` records the result.

The final example UI was also rendered from the installed developer Flatpak on
Wayland at 1220×840 and in an X11-only test configuration at 520×680. The render
checks used no-audio demo mode, removed host PipeWire access and bypassed the
document portal; they do not verify file-chooser integration.

Background permission in that integration run was supplied by a private test
fixture. It proves the request and lifecycle mechanics, not acceptance by a real
desktop policy. Live KDE/GNOME portal approval, preset file chooser integration
and pure X11 fallback-session testing remain release validation work.

Developer CI downgrades only AppStream's `url-homepage-missing` warning to an
informational notice because there is no real publisher URL yet. Remove that
exception for the public release. It does not bypass other metadata failures.

## Primary references

- [Flatpak sandbox permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html)
- [Flatpak command reference, including flatpak-spawn](https://docs.flatpak.org/en/latest/flatpak-command-reference.html)
- [WirePlumber client access policy](https://pipewire.pages.freedesktop.org/wireplumber/policies/client_access.html)
- [Flathub application requirements](https://docs.flathub.org/docs/for-app-authors/requirements)
- [Flathub generative AI policy](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy)
- [Flathub MetaInfo guidelines](https://docs.flathub.org/docs/for-app-authors/metainfo-guidelines)
