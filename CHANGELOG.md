# Changelog

All notable changes to Didge are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow
[semver](https://semver.org/) (pre-1.0: minor bumps may break).

## [0.1.0] — 2026-07-23

### Added

- Physical model of a didgeridoo: lungs, vocal tract, lip valve, waveguide
  bore, bell radiation. No samples and no oscillator.
- Bore: 16-segment Kelly-Lochbaum waveguide over a shapeable radius profile
  (bell, flare, wall texture, wall damping) with a deterministic seeded wall
  irregularity, and power-complementary bell radiation whose corner tracks the
  bell radius.
- Lip valve: one-mass outward-striking exciter after Silva / Menguy-Gilbert,
  integrated with the Newmark scheme (beta 1/4, eta 1/2) plus a fixed point on
  the pressure across the lips. Bernoulli slit flow is solved in closed form
  against tract and bore impedances in series, so the lips beat shut for part
  of every cycle.
- Vocal tract: 8-section waveguide, morphable vowel area functions, and an
  allpass glottis that is open to the lungs at low frequency and reflective at
  formant frequencies.
- Nonlinear wave propagation: amplitude-dependent segment delay, bounded to
  stay stable, so the timbre brightens with blowing pressure.
- Turbulence as a fluctuating pressure jump proportional to the Bernoulli drop
  across the lips (Hirschberg & Verge), gated by the lip opening so it stops
  during the closed phase. Calibrated by measured harmonic-to-noise ratio to
  about +23 dB at the default breath setting.
- Self-calibrating tuning: a linearised lip-plus-bore solver places the bore,
  then a servo measures the sounding period and trims bore length per frequency
  band until the note is in tune. Settles within a couple of cents.
- Overblowing: a second, much higher held note selects the toot register and
  firms the embouchure automatically.
- MIDI: note on/off, pitch bend, CC2/CC11 breath and expression on the blowing
  pressure. Bend range is settable from zero to two octaves, and the bend
  shortens the tube as well as the lips -- bending the embouchure alone barely
  moves the sounding pitch, since the bore decides it.
- WebView UI with a live cutaway of the instrument driven by the engine's own
  bore profile and vocal tract, plus breath, embouchure, voice, instrument and
  space panels.
- Output spectrum analyser behind the instrument: 32 log-spaced constant-Q
  bands from 45 Hz to 12 kHz with octave marks. It runs only while the editor
  is open, since it costs about a quarter of the engine's CPU and is only ever
  looked at.
- Optional decay stage: with it switched on the breath falls away under a
  held note, turning the model into a struck exciter rather than a drone.
- Velocity routing to breath, breath plus attack, embouchure or brightness --
  the destinations that follow from blowing harder -- with an amount control
  and an explicit off.
- Bore profile: twelve of them, from the natural termite-hollowed tube through
  cylinder, cone and horn to trumpet, trombone, flugelhorn, french horn, tuba,
  alphorn and contrabass. Each is built from the parallel fraction before the
  bell, the bell's flare exponent and the bore width at both ends. This sets
  the resonance series, so it changes the instrument far more than any single
  knob, and profiles carry a level trim so switching does not jump by the
  18 dB they otherwise differ by.
- Wall material: wood, bamboo, brass, steel or glass, specified by the loss
  it produces over a full round trip and solved back into the per-traversal
  filter. Overblowing now also takes more breath, as it does in life, so the
  higher register survives a damped wooden wall.
- Ten factory presets, XML user presets.
- Acoustic test suite that renders audio and measures pitch, spectrum, vowel
  response, growl sidebands, overblowing, silence, numerical stability and
  sample-rate independence.
- Formats: VST3, AU, CLAP, Standalone (macOS/Linux/Windows CI).
