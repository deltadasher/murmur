# Security and privacy

Murmur processes the selected microphone locally. It has no account, telemetry or
cloud audio path. Native audio continues when the editor closes. Flatpak requests
desktop background permission; if it is denied, keep the editor open, as the
application's status advises. Stop ends processing and the service quit action
ends the worker. The application makes that lifecycle visible.

The local IPC interface controls audio and settings. Its directory and socket are
restricted to the current user. Other processes running as that user are inside
the same trust boundary. Preset imports are limited to 64 KiB and validated before
settings change; malformed patches must not be partially applied.

PipeWire graph access is sensitive: it allows the worker to capture the selected
microphone and publish a source for other apps. The Flatpak manifest grants the
specific PipeWire socket rather than broad host filesystem or host command access.
X11 also has weaker isolation than Wayland. These platform permissions should be
stated clearly when distributing the app.

## Reporting a vulnerability

A private security contact has not been set up yet. The maintainer should enable
GitHub private vulnerability reporting or provide a monitored private contact.

If you are working with this local source tree, report security-sensitive details
directly to its owner. Do not put a private recording, exploit against a real user,
or identifying logs into a public issue. Useful reports include the affected
version, minimal reproduction, impact and suggested mitigation.

Only the current development version is maintained in this local tree. There is
no established support window or promised response time yet.
