# Contributing to Murmur

Murmur is a native Qt/PipeWire voice processor for Linux gamers and voice chat.
Keep everyday voice controls understandable, preserve precise numeric editing,
and make service and microphone state truthful.

## Build and check

Follow the build dependencies and commands in [PACKAGING.md](PACKAGING.md).
Run the CTest suites after a relevant change. Audio, routing or lifecycle
changes additionally need the isolated integration scenario:

```sh
dbus-run-session -- python3 tests/integration.py build build/audio-check
```

This test requires PipeWire, PipeWire-Pulse, WirePlumber and the PulseAudio tools
`pactl` and `pacat`. It uses a private server with synthetic audio and hardware
discovery disabled. Do not substitute someone's live microphone or modify host
audio defaults for automated tests.

## Change expectations

- DSP changes must stay bounded and avoid allocation, blocking calls and Qt work
  in real-time processing callbacks. Test neutral, extreme and non-finite input
  behavior when relevant.
- Preset/IPC changes must validate atomically, keep compatibility with earlier
  saved presets, and preserve explicit mute, bypass and processing-stop intent.
- UI changes need screenshots at a typical desktop size and the smallest supported
  window, plus keyboard checks. Inspect the rendered result on native Qt; passing
  compilation is not a visual review.
- Keep examples, command-line help and control explanations aligned with actual
  DSP behavior. Distinguish synthetic previews from live audio evidence.
- Scope a pull request around one complete behavior. Explain the trigger, resulting
  behavior and relevant validation. State any platform or sandbox checks still
  outstanding.

Use the surrounding C++ style, small focused functions and descriptive names.
Avoid adding libraries for behavior that Qt or PipeWire already supplies.
Contributions use the project's [MIT license](LICENSE); no separate contributor
agreement is required.

## Reporting bugs and discussing changes

Once the project is hosted, use its issue tracker. Include the Murmur version,
distribution, Qt/PipeWire/WirePlumber versions, display backend, install method,
and clear reproduction steps. Redact usernames, device serial numbers and private
paths from logs. Voice recordings are not required for ordinary routing failures.

Treat contributors and users respectfully. Discuss the behavior and evidence,
avoid personal attacks, and accept that a design improvement needs visual review.
This local source tree does not yet name a repository maintainer or private
moderation contact; add those details when creating the public repository.
