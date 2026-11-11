# Murmur voice console

Murmur is a native microphone workbench for players who want an expressive voice
in Discord, streaming or a game without learning an audio patch bay. The primary
job is to choose a sound, adjust it precisely, and keep the live microphone state
visible. This redesign preserves its warm amber identity and deliberately spends
visual personality on a compact audio-console composition.

## Visual system

- Canvas #171915; supporting surface #20231d; control surface #292d24.
- Main text #efece4; secondary text #b4b6aa; amber #e6ad66; live meter #97b691.
- Noto Sans from the desktop: 14 px body, 12 px help, 20 px module headings,
  28 px wordmark. Tabular digits stabilize values and meter readings.
- 4/8 px spacing rhythm. 24 px functional boundaries; 4 px control corners.
- Flat surfaces and quiet separators. No shadows, glow, gradients, decorative
  animation, RGB styling or fabricated hardware readings.

## Composition chosen before implementation

```
Microphone choice -> Murmur Virtual Microphone          Start / Stop
Presets       Tone | Character | Cleanup | Space
              One focused module, with its editable controls
Effect mix + input meter                 Output level + output meter
Mute                       Bypass                      Hear myself
```

The old simultaneous tone and texture columns spread attention across unrelated
jobs, while extra effects would make that composition sprawl. The new console
keeps one focused module visible. Navigation is persistent at both desktop and
compact widths; it is not a numbered processing sequence. The left preset rail
is a quick sound palette. At compact widths it becomes a selector so the module
gets the entire width.

Tone retains a broad, calculated frequency-response display and three physical
faders on desktop; compact windows use three horizontal rows. Character groups
pitch and drive separately from sample-rate and bit-depth texture. Cleanup uses three aligned full-width rows for
low cut, noise gate and compression, with live threshold and ratio explanations.
Space groups echo amount with delay time and a calculated diagram of configured
echo repeats, matching the engine’s fixed 35 percent feedback.
All controls have exact values, plain descriptions, keyboard access and a
slider double-click reset. In compact windows, the same controls become dense
horizontal rows; supporting explanations remain available in tooltips. Module
pages scroll when a short window or a system warning leaves insufficient room.

The audience suggests fast access, tactile faders and clear active states rather
than neon game decoration. The amber scale, waveform wordmark, audio meters and
flat module strip carry the identity. This is a microphone console, so no giant
hero copy, decorative cards or invented waveform is needed.

## Product truth and state

The graph is read-only and calculated from configured EQ. Bypass shows the
original voice response. Input/output meters come from the actual engine; example
mode says no audio and uses identified sample levels. System mute is separate
from Murmur mute and exposes an explicit recovery action. Session controls and
routing remain outside module pages so users can always recover the microphone.

## Verification

Inspect 1220×840 desktop and 520×680 compact renders, all four pages, frequency
disclosure, focus, offline, bypass and system-mute recovery. Check Wayland and X11
native rendering. Test that new controls dispatch validated parameters and that
incoming snapshots do not overwrite unfinished numeric edits. Preserve current audio settings during installation. Restart the worker when
upgrading DSP code, and verify that the selected microphone and requested state
are restored.
