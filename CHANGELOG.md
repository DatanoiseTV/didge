# Changelog

All notable changes to Qube are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[semver](https://semver.org/) (pre-1.0: minor bumps may break).

## [0.1.0] — 2026-07-22

### Added

- Quadraphonic panning engine: equal-power pairwise (2D VBAP) law with exact
  power preservation, MDAP-style source spread, and interior panning through
  the room centre.
- Motion engine: Orbit, Figure-8, Pendulum, Bounce, seeded Random walk;
  free-running 0.02–8 Hz or tempo-synced (8 bars…1/16 incl. triplets) with
  transport-locked phase, phase offset and reverse.
- Distance model: attenuation, air absorption (transparent at zero distance),
  doppler via slewed fractional delay (3-sample interpolation guard).
- Room reverb: 8-line Householder FDN rendering four decorrelated outputs
  directly into the speaker bed.
- Stereo monitoring of the quad field: spherical-head binaural rendering
  (Woodworth ITD, Brown-Duda shadow shelves, darkened rears), first-order
  ambisonic UHJ stereo encode (90° ± 1° quadrature network, verified
  200 Hz–20 kHz), and an equal-power stereo fold-down. Auto mode resolves
  against the bus width.
- WebView UI: draggable top-down room fed by the engine's actual post-motion
  position, motion trails, path previews, level-reactive speakers, spread
  wedge, per-speaker + output metering, preset browser.
- Ten factory presets, XML user presets.
- Formats: VST3, AU, CLAP, Standalone (macOS/Linux/Windows CI).
