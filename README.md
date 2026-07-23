<div align="center">

# Didge

### Physically modeled didgeridoo — VST3 · AU · CLAP · Standalone

A didgeridoo built from acoustics rather than samples: lungs drive a vocal
tract, the tract drives a one-mass lip valve, and the lips drive a waveguide
bore that radiates from its bell. Play it from a MIDI keyboard and it tunes
itself to the note you asked for.

![License](https://img.shields.io/badge/license-GPL--3.0-blue)
![Formats](https://img.shields.io/badge/formats-VST3%20%C2%B7%20AU%20%C2%B7%20CLAP%20%C2%B7%20Standalone-e0913a)
![JUCE](https://img.shields.io/badge/JUCE-8.0-cccccc)
![C++](https://img.shields.io/badge/C%2B%2B-20-555)

</div>

## What it is

There is no sample content and no oscillator. Every sound the plugin makes is
the result of simulating air:

- **Bore** — a 16-segment waveguide with Kelly-Lochbaum scattering junctions,
  built from a radius profile you can shape (bell, flare, wall texture, wall
  damping). The open end uses the standard power-complementary radiation pair,
  so the radiated sound is `incident + reflected` and the bell's high-pass
  corner follows its own radius.
- **Lip valve** — a one-mass outward-striking model after the brass exciter of
  Silva, Menguy-Gilbert et al., integrated with the unconditionally stable
  Newmark scheme and a fixed point on the pressure across the lips. Bernoulli
  slit flow is solved in closed form against the tract and bore impedances in
  series. The lips genuinely beat shut for part of every cycle — that closure
  is where the buzz comes from.
- **Vocal tract** — an 8-section waveguide with morphable vowel area functions
  and a frequency-dependent glottis. The glottis is an allpass: open to the
  lungs at low frequency so the breath passes, reflective at formant
  frequencies so the tract resonates. Tarnopolsky et al. identified exactly
  that "partially closed glottis" as the difference between an experienced
  didgeridoo player and a novice.
- **Nonlinear propagation** — sound travels at `c + beta*v`, so loud waves
  steepen as they go. This is what makes brass instruments turn brassy when
  pushed, and it makes this one respond to how hard you blow.
- **Turbulence** — a fluctuating pressure jump proportional to the Bernoulli
  drop across the lips, after Hirschberg and Verge. Because it scales with the
  jet, it falls silent while the lips are shut, so the breath rides on the tone
  in step with it instead of sitting underneath it as a constant draught.

## Playing it

| Gesture | Result |
| --- | --- |
| Hold a note | The bore retunes and drones on it |
| Hold a second, much higher note | Overblows into the toot register; the embouchure firms automatically |
| Pitch bend | Bends the lips, not the tube |
| CC2 / CC11 | Breath and expression scale the blowing pressure |
| Vowel / Mouth Open | Moves the tongue, colouring the drone |
| Growl | Voiced modulation, as if humming while blowing |
| Velocity | Routable to breath, attack, embouchure or brightness |

**Bore profile** is the control with the largest effect, because it sets the
resonance series rather than the tone colour. Twelve profiles are built from
the two numbers that actually separate wind instruments — how much of the
length runs parallel before the bell, and how fast the bell then opens —
together with the bore width at each end:

| Profile | Parallel run | Character |
| --- | --- | --- |
| Natural | none | Irregular termite-hollowed tube |
| Cylinder | all | Odd harmonics only, hollow and clarinet-like |
| Cone | none | Complete harmonic series, reedy and saxophone-like |
| Flared / Horn | none / late | Smoothly opening horn |
| Trumpet | a third | Narrow and bright; harmonics rise above the fundamental |
| Trombone | half | The most cylindrical of the brass |
| Flugelhorn | little | Conical and mellow |
| French Horn | little | Narrow throat, wide late bell |
| Tuba / Contrabass | little | Very wide bore and bell, large and dark |
| Alphorn | almost none | Long gentle cone |

Measured on a held D2, the trumpet's spectral centroid is around 700 Hz against
the natural bore's 195 Hz, and the cylinder's second harmonic sits 12 dB under
its fundamental where the natural bore's sits 7 dB above. Profiles carry a
level trim so switching between them does not jump by the 18 dB they otherwise
differ by. **Material** sets how much the wall loses and how
sharply that loss rises with frequency: wood is dark and short, metal brighter
and longer-ringing. **Decay**, off by default, lets the breath run out under a
held note for short struck sounds.

The instrument tunes itself. The linearised solver places the bore so the
threshold oscillation lands on the requested note, then a servo measures the
sounding period from the lip oscillator and trims the bore length until the
note is exactly in tune, caching the correction per frequency band. The first
note in a band slides in over a few hundred milliseconds; every later note in
that band starts in tune. This is necessary because a didgeridoo is driven far
past threshold, and how sharp it plays depends almost entirely on bore shape
and blowing pressure — from about 10 to 180 cents on this model, which no fixed
correction curve covers.

## Building

```sh
git clone --recursive https://github.com/DatanoiseTV/didge
cd didge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Didge_All Didge_CLAP
```

Built plugins are copied into the user plug-in folders by default
(`-DDIDGE_INSTALL_LOCAL=OFF` to disable, e.g. in CI).

Run the tests:

```sh
cmake --build build --target didge_dsp_tests didge_state_tests
ctest --test-dir build --output-on-failure
```

## Tests

The DSP tests render audio and measure it rather than asserting that the code
ran. They check that every note from 43 to 147 Hz sounds within 12 cents of the
pitch requested, that the spectrum is buzzy rather than sinusoidal, that the
vowel control measurably rewrites the spectral envelope, that growl adds
inharmonic sidebands, that overblowing reaches a higher register, that the
instrument falls genuinely silent with no breath, that output stays finite and
bounded across parameter extremes, and that pitch does not depend on sample
rate.

## Known limitations

- **Vocal shaping is shallower than a real player's.** Measured tract-to-bore
  impedance ratios in the formant band are around 18:1; this model becomes
  unstable above roughly 2.5:1, because it applies the tract's characteristic
  impedance instantaneously at every frequency instead of only at its
  resonances. Moving the tract's loading entirely into its returning wave is
  the fix.
- Lip damping defaults to Q around 3.8. Real human lips measure Q = 0.46 to
  1.8; the Q around 7 common in the literature is an artificial-lip value.
- **Material is subtler than the name suggests.** Wall loss strong enough to
  make wood and metal obviously different also stops the overblown register
  speaking, so the spread is held to about 14 Hz of spectral centroid and a
  decibel or so in the upper harmonics. Bore profile is the control to reach
  for when you want a different instrument.

## References

- Tarnopolsky, Fletcher, Hollenberg, Lange, Smith & Wolfe, "The vocal tract and
  the sound of a didgeridoo", *Nature* **436**, 39 (2005).
- Smith, Rey, Dickens, Fletcher, Hollenberg & Wolfe, "Vocal tract resonances
  and the sound of the Australian didjeridu", *JASA* **121**(1), 547 (2007).
- Silva, Vergez, Guillemain, Kergomard et al., "Time-domain simulation of brass
  instruments", arXiv:1511.04247.
- Hirschberg & Verge, "Turbulence noise in flue instruments", ISMA 1995.
- Silva, Guillemain, Kergomard, Mallaroni & Norris, "Approximation formulae for
  the acoustic radiation impedance of a cylindrical pipe", *JSV* **322** (2009).

## License

GPL-3.0. See [LICENSE](LICENSE).
