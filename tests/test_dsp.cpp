/*
  Didge — physically modeled didgeridoo
  Copyright (C) 2026 DatanoiseTV

  Framework-free acoustic tests for the physical model. These MEASURE the
  rendered audio — pitch, spectrum, decay — rather than asserting that the
  code ran. Exit code != 0 on any failure; each check prints its own
  diagnostic.
*/

#include "dsp/DidgeModel.h"
#include "dsp/DidgeEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace didge;

static int failures = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++failures;                                                     \
            std::printf ("FAIL %s:%d  ", __FILE__, __LINE__);               \
            std::printf (__VA_ARGS__);                                      \
            std::printf ("\n");                                             \
        }                                                                   \
    } while (0)

static constexpr double kFs = 48000.0;

// ---------------------------------------------------------------------------
// Measurement helpers
// ---------------------------------------------------------------------------
static float goertzelDb (const std::vector<float>& x, float freq, float fromSec, float toSec)
{
    const int a = (int) (fromSec * kFs);
    const int b = std::min ((int) x.size(), (int) (toSec * kFs));
    if (b - a <= 0) return -200.0f;
    const double w = 2.0 * 3.14159265358979 * freq / kFs, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = a; i < b; ++i)
    {
        const double s0 = x[(size_t) i] + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double n = b - a;
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 10.0f * (float) std::log10 (std::max (1e-20, p / (n * n / 4.0)));
}

// Harmonic-sum pitch estimate around a hint. Unbiased over [0.5, 2] x hint and
// immune to the faint period-doubling a hard-driven lip valve can show, which
// plain autocorrelation reports as an octave error.
static float estimateF0 (const std::vector<float>& x, float hint, float from, float to)
{
    // Two-stage: a coarse sweep over [0.5, 2] x hint, then a fine sweep around
    // the winner. A single fine grid costs a Goertzel per candidate per
    // harmonic over a second of audio, which ran to billions of operations and
    // made the suite take minutes.
    auto score = [&] (float f)
    {
        double e = 0.0;
        for (int h = 1; h <= 5; ++h)
            e += std::pow (10.0, goertzelDb (x, f * h, from, to) / 10.0);
        return e;
    };

    float bestF = hint;
    double bestE = -1e300;
    constexpr int kCoarse = 90;
    for (int i = 0; i <= kCoarse; ++i)
    {
        const float f = hint * (0.5f + 1.5f * (float) i / kCoarse);
        const double e = score (f);
        if (e > bestE) { bestE = e; bestF = f; }
    }

    const float step = hint * 1.5f / kCoarse;
    const float lo = bestF - step, hi = bestF + step;
    for (int i = 0; i <= 40; ++i)
    {
        const float f = lo + (hi - lo) * (float) i / 40.0f;
        const double e = score (f);
        if (e > bestE) { bestE = e; bestF = f; }
    }
    return bestF;
}

static float rmsOf (const std::vector<float>& x, float fromSec, float toSec)
{
    const int a = (int) (fromSec * kFs);
    const int b = std::min ((int) x.size(), (int) (toSec * kFs));
    if (b <= a) return 0.0f;
    double s = 0.0;
    for (int i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt ((float) (s / (b - a)));
}

static float noteHz (int note)
{
    return 440.0f * std::pow (2.0f, (note - 69) / 12.0f);
}

// Render a held note and return the mono sum.
static std::vector<float> hold (DidgeEngine& e, const EngineParams& p, int note,
                                float seconds, int secondNote = 0)
{
    std::vector<float> L (256), R (256), out;
    const int total = (int) (seconds * kFs);
    int done = 0;
    bool sentSecond = false;
    while (done < total)
    {
        const int n = std::min (256, total - done);
        NoteEvent ev[2];
        int nev = 0;
        if (done == 0)
            ev[nev++] = { 0, NoteEvent::noteOn, note, 0.8f };
        if (secondNote > 0 && ! sentSecond && done >= (int) (0.4 * kFs))
        {
            ev[nev++] = { 0, NoteEvent::noteOn, secondNote, 0.8f };
            sentSecond = true;
        }
        e.process (L.data(), R.data(), n, p, ev, nev);
        for (int i = 0; i < n; ++i) out.push_back (0.5f * (L[i] + R[i]));
        done += n;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Fractional delay: the loop must never index past the buffer, and read()
// after write() must be a true d-sample delay.
// ---------------------------------------------------------------------------
static void testFracDelay()
{
    FracDelay d;
    d.prepare (64);
    for (int i = 0; i < 200; ++i)
    {
        const float want = (float) i;
        // Read before write is the contract the waveguides rely on.
        const float got = d.read (4.0f);
        d.write (want);
        if (i >= 4)
            CHECK (std::abs (got - (want - 4.0f)) < 1.0e-3f,
                   "FracDelay read(4) at i=%d gave %f, want %f", i, got, want - 4.0f);
    }

    // Fractional reads interpolate between neighbours, and every delay up to
    // the maximum must stay finite (no wrap past the end of the buffer).
    for (float delay = 1.0f; delay <= d.maxDelay(); delay += 0.5f)
        CHECK (std::isfinite (d.read (delay)), "FracDelay read(%f) not finite", delay);
}

// ---------------------------------------------------------------------------
// The bore's tuner must agree with the waveguide it describes: ringing the
// passive tube has to show a resonance where the impedance solver says.
// ---------------------------------------------------------------------------
static void testBoreResonance()
{
    Bore bore;
    bore.prepare (kFs);
    bore.setSmoothing (kFs);
    BoreShape shape;
    bore.setShape (shape);
    bore.tuneTo (73.42f);
    bore.snapToTargets();

    CHECK (std::abs (bore.droneFrequency() - 73.42f) < 1.0f,
           "bore tuner placed the peak at %f, want 73.42", bore.droneFrequency());

    // Excite the tube with a rigid mouth and confirm it rings at that peak.
    std::vector<float> ring;
    const int n = (int) (2.0 * kFs);
    ring.reserve ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const float b = bore.beginStep();
        const float kick = (i == 0) ? 1000.0f : 0.0f;
        ring.push_back (bore.finishStep (b * 0.98f + kick));
    }

    const float atPeak = goertzelDb (ring, bore.droneFrequency(), 0.1f, 2.0f);
    const float below  = goertzelDb (ring, bore.droneFrequency() * 0.75f, 0.1f, 2.0f);
    const float above  = goertzelDb (ring, bore.droneFrequency() * 1.30f, 0.1f, 2.0f);
    CHECK (atPeak > below + 10.0f && atPeak > above + 10.0f,
           "passive bore does not ring at the tuned peak: peak %.1f dB, below %.1f, above %.1f",
           atPeak, below, above);
}

// ---------------------------------------------------------------------------
// A wider bell must actually widen the bore.
// ---------------------------------------------------------------------------
static void testBoreShape()
{
    Bore narrow, wide;
    narrow.prepare (kFs);
    wide.prepare (kFs);

    BoreShape a; a.bell = 0.1f;
    BoreShape b; b.bell = 0.9f;
    narrow.setShape (a);
    wide.setShape (b);

    CHECK (wide.segmentRadius (Bore::kSegments - 1) > narrow.segmentRadius (Bore::kSegments - 1) * 1.5f,
           "bell parameter did not widen the bore: narrow %f, wide %f",
           narrow.segmentRadius (Bore::kSegments - 1), wide.segmentRadius (Bore::kSegments - 1));

    // The bore must widen from mouth to bell, never pinch shut.
    for (int i = 0; i < Bore::kSegments; ++i)
        CHECK (wide.segmentRadius (i) > 0.005f, "bore radius %d collapsed to %f",
               i, wide.segmentRadius (i));
}

// ---------------------------------------------------------------------------
// Pitch: the instrument must sound the note it was asked for, across its
// range. The engine learns its own length trim, so allow it to settle.
// ---------------------------------------------------------------------------
static void testPitchAccuracy()
{
    EngineParams p;
    for (int note : { 29, 31, 34, 36, 38, 40, 43, 45, 48, 50 })
    {
        DidgeEngine e;
        e.prepare (kFs, 256);
        const auto m = hold (e, p, note, 4.0f);
        const float want = noteHz (note);
        const float got  = estimateF0 (m, want, 3.0f, 4.0f);
        const float cents = 1200.0f * std::log2 (got / want);
        CHECK (std::abs (cents) < 12.0f,
               "note %d wanted %.2f Hz, sounded %.2f Hz (%+.1f cents)", note, want, got, cents);
        CHECK (rmsOf (m, 3.0f, 4.0f) > 0.005f,
               "note %d barely speaks: rms %.5f", note, rmsOf (m, 3.0f, 4.0f));
    }
}

// ---------------------------------------------------------------------------
// A didgeridoo drone is buzzy, not sinusoidal: the upper partials must carry
// real energy, and the second harmonic typically dominates the fundamental.
// ---------------------------------------------------------------------------
static void testHarmonicRichness()
{
    EngineParams p;
    DidgeEngine e;
    e.prepare (kFs, 256);
    const auto m = hold (e, p, 38, 4.0f);
    const float f0 = estimateF0 (m, noteHz (38), 3.0f, 4.0f);

    const float h1 = goertzelDb (m, f0,        3.0f, 4.0f);
    const float h2 = goertzelDb (m, f0 * 2.0f, 3.0f, 4.0f);
    const float h3 = goertzelDb (m, f0 * 3.0f, 3.0f, 4.0f);
    const float h5 = goertzelDb (m, f0 * 5.0f, 3.0f, 4.0f);

    CHECK (h2 > h1 - 6.0f, "2nd harmonic %.1f dB too weak against fundamental %.1f dB", h2, h1);
    CHECK (h3 > h1 - 25.0f, "3rd harmonic %.1f dB too weak against fundamental %.1f dB", h3, h1);
    CHECK (h5 > h1 - 45.0f, "5th harmonic %.1f dB too weak against fundamental %.1f dB", h5, h1);
}

// ---------------------------------------------------------------------------
// The tongue must audibly colour the sound.
//
// This deliberately does NOT assert a speech-like direction (say, that "ee"
// puts more energy near F2 than "oo"). A didgeridoo embouchure leaves the
// tract closed at the lips as well as at the glottis, so its modes are the
// half-wave modes of a closed-closed tube, not the quarter-wave formant
// pattern of open-mouthed speech; asserting speech behaviour would encode a
// premise the instrument does not obey. What must be true is that moving the
// vowel control measurably rewrites the spectral envelope.
// ---------------------------------------------------------------------------
static void testVowelFormants()
{
    auto envelope = [] (const std::vector<float>& x, std::vector<float>& out)
    {
        out.clear();
        for (int h = 4; h <= 40; ++h)
            out.push_back (goertzelDb (x, noteHz (38) * h, 3.0f, 4.0f));
    };

    EngineParams ee; ee.vowelX = 1.0f; ee.tractMix = 0.9f;
    EngineParams oo; oo.vowelX = 0.0f; oo.tractMix = 0.9f;
    EngineParams off; off.tractMix = 0.0f;

    DidgeEngine e1; e1.prepare (kFs, 256);
    DidgeEngine e2; e2.prepare (kFs, 256);
    DidgeEngine e3; e3.prepare (kFs, 256);
    std::vector<float> a, b, c;
    envelope (hold (e1, ee,  38, 4.0f), a);
    envelope (hold (e2, oo,  38, 4.0f), b);
    envelope (hold (e3, off, 38, 4.0f), c);

    float maxDiff = 0.0f, meanDiff = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const float d = std::abs (a[i] - b[i]);
        maxDiff = std::max (maxDiff, d);
        meanDiff += d;
    }
    meanDiff /= (float) a.size();

    CHECK (maxDiff > 6.0f,
           "vowel barely changed the spectrum: max difference only %.1f dB", maxDiff);
    CHECK (meanDiff > 1.5f,
           "vowel barely changed the spectrum: mean difference only %.1f dB", meanDiff);

    // And engaging the tract at all must do something.
    float vsOff = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        vsOff = std::max (vsOff, std::abs (a[i] - c[i]));
    CHECK (vsOff > 6.0f, "engaging the vocal tract changed nothing: %.1f dB", vsOff);
}

// ---------------------------------------------------------------------------
// Growl adds voiced modulation, so energy must appear away from the drone's
// own harmonic series.
// ---------------------------------------------------------------------------
static void testGrowl()
{
    // The voice pitch must be deliberately inharmonic. The default of 19
    // semitones is 2.997x, so f0 + gf lands on the 4th harmonic and the probe
    // measures ordinary harmonic energy instead of growl sidebands.
    EngineParams clean;
    clean.growlSemis = 7.0f;
    EngineParams growly = clean;
    growly.growl = 0.8f;

    DidgeEngine e1; e1.prepare (kFs, 256);
    DidgeEngine e2; e2.prepare (kFs, 256);
    const auto a = hold (e1, clean,  38, 4.0f);
    const auto b = hold (e2, growly, 38, 4.0f);

    const float f0 = estimateF0 (a, noteHz (38), 3.0f, 4.0f);
    const float gf = f0 * std::pow (2.0f, 7.0f / 12.0f);
    const float sideClean  = goertzelDb (a, f0 + gf, 3.0f, 4.0f);
    const float sideGrowly = goertzelDb (b, f0 + gf, 3.0f, 4.0f);

    CHECK (sideGrowly > sideClean + 2.0f,
           "growl added no sideband energy: clean %.1f dB, growl %.1f dB", sideClean, sideGrowly);
}

// ---------------------------------------------------------------------------
// Overblowing: a second, much higher held note must move the instrument to a
// higher register while the bore stays on the drone.
// ---------------------------------------------------------------------------
static void testTootRegister()
{
    EngineParams p;
    DidgeEngine e;
    e.prepare (kFs, 256);
    const auto m = hold (e, p, 38, 4.0f, 54);

    const float drone = noteHz (38);
    const float got = estimateF0 (m, drone * 2.2f, 3.0f, 4.0f);
    CHECK (got > drone * 1.6f,
           "overblowing did not reach a higher register: drone %.1f Hz, sounded %.1f Hz",
           drone, got);
    CHECK (rmsOf (m, 3.0f, 4.0f) > 0.004f, "overblown register barely speaks");
}

// ---------------------------------------------------------------------------
// Silence: with no breath there is no energy source, so the model must fall
// genuinely silent rather than settle into a low-level limit cycle.
// ---------------------------------------------------------------------------
static void testReleaseToSilence()
{
    EngineParams p;
    DidgeEngine e;
    e.prepare (kFs, 256);

    std::vector<float> L (256), R (256), tail;
    NoteEvent on { 0, NoteEvent::noteOn, 38, 0.8f };
    for (int i = 0; i < 560; ++i)
        e.process (L.data(), R.data(), 256, p, i == 0 ? &on : nullptr, i == 0 ? 1 : 0);

    NoteEvent off { 0, NoteEvent::noteOff, 38, 0.0f };
    for (int i = 0; i < 800; ++i)
    {
        e.process (L.data(), R.data(), 256, p, i == 0 ? &off : nullptr, i == 0 ? 1 : 0);
        for (int j = 0; j < 256; ++j) tail.push_back (L[j]);
    }

    const float late = rmsOf (tail, 3.0f, 4.0f);
    CHECK (late < 1.0e-4f, "instrument never falls silent: tail rms %.3e", late);
}

// ---------------------------------------------------------------------------
// Numerical safety: every parameter extreme must stay finite and bounded.
// ---------------------------------------------------------------------------
static void testStability()
{
    const float lo = 0.0f, hi = 1.0f;
    int caseIdx = 0;
    for (float pressure : { lo, 0.5f, hi })
        for (float damp : { lo, hi })
            for (float emb : { lo, hi })
                for (float growl : { lo, hi })
                {
                    EngineParams p;
                    p.pressure = pressure;
                    p.lipDamp = damp;
                    p.embouchure = emb;
                    p.growl = growl;
                    p.breath = hi;
                    p.tractMix = hi;
                    p.shape.bell = (caseIdx % 2) ? hi : lo;
                    p.shape.flare = (caseIdx % 3) ? hi : lo;

                    DidgeEngine e;
                    e.prepare (kFs, 256);
                    const auto m = hold (e, p, 38, 1.5f);

                    float peak = 0.0f;
                    bool finite = true;
                    for (float v : m)
                    {
                        if (! std::isfinite (v)) finite = false;
                        peak = std::max (peak, std::abs (v));
                    }
                    CHECK (finite, "non-finite output for case %d (pressure %.1f damp %.1f emb %.1f growl %.1f)",
                           caseIdx, pressure, damp, emb, growl);
                    CHECK (peak <= 4.0f, "output ran away for case %d: peak %f", caseIdx, peak);
                    ++caseIdx;
                }
}

// ---------------------------------------------------------------------------
// Sample-rate independence: the sounding pitch must not depend on the rate.
// ---------------------------------------------------------------------------
static void testSampleRates()
{
    for (double fs : { 44100.0, 96000.0 })
    {
        EngineParams p;
        DidgeEngine e;
        e.prepare (fs, 256);

        std::vector<float> L (256), R (256), out;
        NoteEvent on { 0, NoteEvent::noteOn, 38, 0.8f };
        const int blocks = (int) (4.0 * fs / 256);
        for (int i = 0; i < blocks; ++i)
        {
            e.process (L.data(), R.data(), 256, p, i == 0 ? &on : nullptr, i == 0 ? 1 : 0);
            for (int j = 0; j < 256; ++j) out.push_back (0.5f * (L[j] + R[j]));
        }

        // Re-measure against this rate rather than the module-level kFs.
        const float want = noteHz (38);
        float bestF = want;
        double bestE = -1e300;
        for (int i = 0; i <= 1200; ++i)
        {
            const float f = want * (0.5f + 1.5f * (float) i / 1200.0f);
            double acc = 0.0;
            for (int h = 1; h <= 5; ++h)
            {
                const double w = 2.0 * 3.14159265358979 * (f * h) / fs, c = 2.0 * std::cos (w);
                double s1 = 0.0, s2 = 0.0;
                const int a = (int) (3.0 * fs), b = (int) (4.0 * fs);
                for (int k = a; k < b && k < (int) out.size(); ++k)
                {
                    const double s0 = out[(size_t) k] + c * s1 - s2;
                    s2 = s1; s1 = s0;
                }
                acc += s1 * s1 + s2 * s2 - c * s1 * s2;
            }
            if (acc > bestE) { bestE = acc; bestF = f; }
        }
        const float cents = 1200.0f * std::log2 (bestF / want);
        CHECK (std::abs (cents) < 20.0f,
               "at %.0f Hz the drone sounded %.2f Hz, want %.2f (%+.1f cents)",
               fs, bestF, want, cents);
    }
}

int main()
{
    testFracDelay();
    testBoreResonance();
    testBoreShape();
    testPitchAccuracy();
    testHarmonicRichness();
    testVowelFormants();
    testGrowl();
    testTootRegister();
    testReleaseToSilence();
    testStability();
    testSampleRates();

    if (failures == 0)
        std::printf ("All DSP tests passed.\n");
    else
        std::printf ("%d DSP test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
