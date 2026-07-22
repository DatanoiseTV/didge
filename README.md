<div align="center">

# Qube

### Quadraphonic spatial panner — VST3 · AU · CLAP · Standalone

Place a sound anywhere in a four-speaker room, move it on tempo-synced paths,
and monitor the result on plain stereo through an ambisonic binaural or UHJ
fold-down — so surround panning is audible on headphones and ordinary
speakers, no quad rig required.

![License](https://img.shields.io/badge/license-GPL--3.0-blue)
![Formats](https://img.shields.io/badge/formats-VST3%20%C2%B7%20AU%20%C2%B7%20CLAP%20%C2%B7%20Standalone-38e1ff)
![JUCE](https://img.shields.io/badge/JUCE-8.0-cccccc)
![C++](https://img.shields.io/badge/C%2B%2B-20-555)

</div>

## What it does

- **Quad panning (4.0)** — equal-power pairwise panning (2D VBAP) across
  FL/FR/RL/RR. The pan law is exactly power-preserving at every azimuth, with
  interior panning: a source crossing the centre of the room morphs smoothly
  through "everywhere at once" instead of snapping between speaker pairs.
- **Motion engine** — Orbit, Figure-8, Pendulum, Bounce and a seeded Random
  walk, free-running (0.02–8 Hz) or tempo-synced (8 bars … 1/16, with triplet
  divisions). Sync mode is transport-locked: phase derives from the host song
  position, so every loop pass renders identically. Phase offset and reverse
  included.
- **Distance model** — distance-dependent attenuation, air absorption
  (distance-scaled lowpass that goes fully transparent up close), and a
  doppler mode with a slewed fractional delay: fast moves bend pitch, still
  sources stay put.
- **Room** — an 8-line Householder FDN reverb that renders straight into the
  speaker bed with four decorrelated outputs, so early energy is spatialised
  through the same output stage as the direct sound.
- **Stereo monitoring of the quad field** — on a stereo bus (or by choice on
  a quad bus):
  - **Binaural** — the four speakers are virtualised over headphones with a
    spherical-head model: Woodworth ITDs plus Brown-Duda head-shadow shelves,
    with rear speakers darkened for the front/back cue. No HRTF dataset, no
    convolution, no zipper while the source moves.
  - **Stereo UHJ** — first-order ambisonic encode of the bed folded to
    two-channel UHJ through a 90° phase-difference network. Mono- and
    stereo-compatible, image wider than the speaker base.
  - **Stereo Mix** — plain equal-power fold of the rears into the fronts.
  - **Auto** picks Quad on a 4-channel bus and Binaural on stereo.
- **Source spread** — MDAP-style width from point-source to wrapped around
  the head; stereo inputs keep their L/R identity as two separated virtual
  sources.
- **WebView UI** — a draggable top-down room with live motion trails, the
  actual engine position (what you see is what plays), level-reactive
  speakers, path previews, and per-speaker metering. Aspect-locked resizable
  window.

## Building

```sh
git clone --recursive https://github.com/DatanoiseTV/qube
cd qube
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Qube_All Qube_CLAP
```

Built plugins are copied into the user plug-in folders by default
(`-DQUBE_INSTALL_LOCAL=OFF` to disable, e.g. in CI).

Run the tests:

```sh
cmake --build build --target qube_dsp_tests qube_state_tests
ctest --test-dir build --output-on-failure
```

## Host setup

| Bus | Result |
| --- | --- |
| stereo in → quad out (4.0) | true quadraphonic panning |
| stereo in → stereo out | binaural / UHJ / stereo fold-down of the same quad field |
| mono in | single-source panning, spread fans it out |

In stereo-out configurations the Output mode selects the fold-down; "Quad"
falls back to Binaural (a stereo bus cannot carry four channels — the UI says
so instead of going silent).

Latency: the doppler path has a fixed 3-sample interpolation guard
(62 µs at 48 kHz); no latency is reported to the host.

## License

GPL-3.0 — see [LICENSE](LICENSE). JUCE is used under its GPL option;
`clap-juce-extensions` is MIT. The vendored React and Babel-standalone
runtimes in `ui/vendor` are MIT.
