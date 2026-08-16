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

    // Step down an octave only when a partial genuinely stands at f/2. A
    // harmonic sum cannot settle this on its own -- every harmonic of f is
    // also a harmonic of f/2, so the sum always looks comparable -- and in
    // this instrument the second partial often stands above the first, which
    // pulls the estimate an octave high. Asking whether there is actually
    // energy at the lower frequency is the question that decides it.
    for (int i = 0; i < 2; ++i)
    {
        const float halfF = bestF * 0.5f;
        if (halfF < hint * 0.45f) break;
        if (goertzelDb (x, halfF, from, to) > goertzelDb (x, bestF, from, to) - 12.0f)
            bestF = halfF;
        else
            break;
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
// The optional decay stage must actually run the breath out under a held
// note, and must not do so when it is switched off.
// ---------------------------------------------------------------------------
static void testDecayStage()
{
    EngineParams sustained;
    EngineParams decayed = sustained;
    decayed.decayOn = true;
    decayed.decayMs = 300.0f;
    decayed.sustain = 0.0f;

    DidgeEngine e1; e1.prepare (kFs, 256);
    DidgeEngine e2; e2.prepare (kFs, 256);
    const auto a = hold (e1, sustained, 38, 3.0f);
    const auto b = hold (e2, decayed,   38, 3.0f);

    // Measure the loudest part of the first second rather than assuming when
    // it falls. The bore takes time to fill, so the acoustic peak lags the
    // envelope's attack and a fixed early window can miss it entirely.
    auto peakEarly = [] (const std::vector<float>& x)
    {
        float best = 0.0f;
        for (float t = 0.0f; t < 0.9f; t += 0.1f)
            best = std::max (best, rmsOf (x, t, t + 0.1f));
        return best;
    };

    const float aEarly = peakEarly (a), aLate = rmsOf (a, 2.5f, 3.0f);
    const float bEarly = peakEarly (b), bLate = rmsOf (b, 2.5f, 3.0f);

    CHECK (aLate > 0.4f * aEarly,
           "the drone died without a decay stage: %.5f early, %.5f late", aEarly, aLate);
    // -54 dBFS: the bar is "clearly audible", not a round number. What the
    // test is really for is the ratio below.
    CHECK (bEarly > 0.002f, "decay stage never spoke at all: %.5f", bEarly);
    CHECK (bLate < 0.05f * bEarly,
           "decay stage did not run the breath out: %.5f early, %.5f late", bEarly, bLate);

    // A full sustain level should behave like no decay at all.
    EngineParams held = decayed;
    held.sustain = 1.0f;
    DidgeEngine e3; e3.prepare (kFs, 256);
    const auto c = hold (e3, held, 38, 3.0f);
    CHECK (rmsOf (c, 2.5f, 3.0f) > 0.4f * rmsOf (c, 0.3f, 0.5f),
           "full sustain still decayed away");
}

// ---------------------------------------------------------------------------
// Velocity routing must change the sound, and must do nothing when off.
// ---------------------------------------------------------------------------
static void testVelocityRouting()
{
    auto renderVel = [] (int target, float vel)
    {
        EngineParams p;
        p.velTarget = target;
        p.velAmount = 1.0f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        std::vector<float> L (256), R (256), out;
        const int total = (int) (2.5f * kFs);
        int done = 0;
        while (done < total)
        {
            const int n = std::min (256, total - done);
            NoteEvent ev { 0, NoteEvent::noteOn, 38, vel };
            e.process (L.data(), R.data(), n, p, done == 0 ? &ev : nullptr, done == 0 ? 1 : 0);
            for (int i = 0; i < n; ++i) out.push_back (0.5f * (L[i] + R[i]));
            done += n;
        }
        return rmsOf (out, 1.8f, 2.5f);
    };

    // Off: velocity must not change the level.
    const float offSoft = renderVel (0, 0.2f), offHard = renderVel (0, 1.0f);
    CHECK (std::abs (offSoft - offHard) < 0.15f * std::max (offSoft, offHard),
           "velocity changed the sound with routing off: %.5f vs %.5f", offSoft, offHard);

    // Breath: harder must be louder.
    const float bSoft = renderVel (1, 0.2f), bHard = renderVel (1, 1.0f);
    CHECK (bHard > 1.5f * bSoft,
           "velocity to breath did not scale the level: %.5f soft, %.5f hard", bSoft, bHard);
}

// ---------------------------------------------------------------------------
// Pitch bend must actually bend the pitch, over a wide range.
//
// Bending the embouchure alone barely moves the sounding note, because the
// bore decides it; the bend therefore scales the tube as well, and this checks
// that a requested interval really arrives. The tolerance widens with the
// interval, since the lip resonance and the tube do not scale identically
// through an outward-striking valve.
// ---------------------------------------------------------------------------
static void testPitchBend()
{
    auto soundedAfterBend = [] (float semis)
    {
        EngineParams p;
        DidgeEngine e;
        e.prepare (kFs, 256);
        std::vector<float> L (256), R (256), out;
        const int blocks = (int) (5.0 * kFs / 256);
        for (int i = 0; i < blocks; ++i)
        {
            if (i == (int) (2.0 * kFs / 256)) e.setPitchBend (semis);
            NoteEvent ev { 0, NoteEvent::noteOn, 38, 0.8f };
            e.process (L.data(), R.data(), 256, p, i == 0 ? &ev : nullptr, i == 0 ? 1 : 0);
            for (int j = 0; j < 256; ++j) out.push_back (0.5f * (L[j] + R[j]));
        }
        // Search a narrow window around the expected pitch. The general
        // estimator sweeps [0.5, 2] x hint, and in this bore the second
        // harmonic stands 7 dB above the fundamental, so it can settle an
        // octave high. The window here is +/-35%, far wider than the
        // tolerance being asserted, so it cannot manufacture a pass.
        const float want = noteHz (38) * std::pow (2.0f, semis / 12.0f);
        float bestF = want;
        double bestE = -1e300;
        for (int i = 0; i <= 700; ++i)
        {
            const float f = want * (0.65f + 0.70f * (float) i / 700.0f);
            double e = 0.0;
            for (int h = 1; h <= 5; ++h)
                e += std::pow (10.0, goertzelDb (out, f * h, 4.0f, 5.0f) / 10.0);
            if (e > bestE) { bestE = e; bestF = f; }
        }
        return bestF;
    };

    const float base = noteHz (38);
    for (float semis : { -12.0f, -7.0f, -2.0f, 2.0f, 7.0f, 12.0f })
    {
        const float got = soundedAfterBend (semis);
        const float cents = 1200.0f * std::log2 (got / base);
        const float err = cents - semis * 100.0f;
        // Allow a tenth of the requested interval, and at least 30 cents.
        const float tol = std::max (30.0f, std::abs (semis) * 10.0f);
        CHECK (std::abs (err) < tol,
               "bend of %+.0f st sounded %+.0f cents, off by %+.0f (tolerance %.0f)",
               semis, cents, err, tol);
    }
}

// ---------------------------------------------------------------------------
// Humanising must make successive notes differ, and must do so subtly. Off,
// the instrument has to stay repeatable.
// ---------------------------------------------------------------------------
static void testHumanize()
{
    auto spread = [] (float amount)
    {
        DidgeEngine e;               // one engine: notes vary against each other
        e.prepare (kFs, 256);
        EngineParams p;
        p.humanize = amount;

        float lo = 1.0e9f, hi = -1.0e9f;
        for (int rep = 0; rep < 5; ++rep)
        {
            const auto m = hold (e, p, 38, 2.5f);
            const float f = estimateF0 (m, noteHz (38), 1.4f, 2.4f);
            const float c = 1200.0f * std::log2 (f / noteHz (38));
            lo = std::min (lo, c);
            hi = std::max (hi, c);

            // Let it fall silent between notes.
            std::vector<float> L (256), R (256);
            NoteEvent off { 0, NoteEvent::noteOff, 38, 0.0f };
            for (int i = 0; i < 130; ++i)
                e.process (L.data(), R.data(), 256, p, i == 0 ? &off : nullptr, i == 0 ? 1 : 0);
        }
        return hi - lo;
    };

    const float off = spread (0.0f);
    const float on  = spread (1.0f);

    CHECK (off < 2.5f, "notes vary with humanising off: %.1f cents spread", off);
    CHECK (on > off + 1.5f,
           "humanising did not vary the notes: %.1f cents off, %.1f cents on", off, on);
    CHECK (on < 25.0f,
           "humanising is far too strong: %.1f cents spread at maximum", on);
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
// The bore profile must change the resonance series, not just the drawing.
// A cylinder closed at one end resonates at odd multiples of its fundamental,
// so its second harmonic is weak; the natural flared bore supports the even
// ones and typically leads with the second. That contrast is the whole reason
// the profile control exists.
// ---------------------------------------------------------------------------
static void testBoreProfiles()
{
    auto secondVsFirst = [] (int profile)
    {
        EngineParams p;
        p.shape.profile = profile;
        p.pressure = 0.75f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        const auto m = hold (e, p, 38, 4.0f);
        const float f0 = estimateF0 (m, noteHz (38), 3.0f, 4.0f);
        return goertzelDb (m, f0 * 2.0f, 3.0f, 4.0f) - goertzelDb (m, f0, 3.0f, 4.0f);
    };

    const float natural  = secondVsFirst (0);
    const float cylinder = secondVsFirst (1);

    CHECK (natural - cylinder > 8.0f,
           "cylinder did not suppress the even harmonic against the natural bore: "
           "natural H2-H1 %.1f dB, cylinder %.1f dB", natural, cylinder);
    CHECK (cylinder < 0.0f,
           "cylinder's second harmonic should sit below its fundamental, got %+.1f dB",
           cylinder);

    // Every profile must still speak and stay finite.
    for (int prof = 0; prof < 12; ++prof)
    {
        EngineParams p;
        p.shape.profile = prof;
        p.pressure = 0.8f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        const auto m = hold (e, p, 38, 3.0f);
        bool finite = true;
        for (float v : m) if (! std::isfinite (v)) finite = false;
        CHECK (finite, "profile %d produced non-finite output", prof);
        CHECK (rmsOf (m, 2.0f, 3.0f) > 0.002f,
               "profile %d barely speaks: rms %.5f", prof, rmsOf (m, 2.0f, 3.0f));
    }
}


// ---------------------------------------------------------------------------
// Excitation type. The striking direction is the whole point: lips are blown
// open and sound above a bore resonance, a cane reed is blown shut and sounds
// below it. Every type has to speak, stay finite, and land near the note the
// keyboard asked for -- each carries its own measured starting offset, since
// the learner only corrects after a note has been held.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Every bore profile must play in tune on the FIRST note, before the learner
// has heard anything. The linearised solver places the bore for threshold
// oscillation, but the nonlinear sounding pitch sits tens to over a hundred
// cents sharp of that for the brass profiles -- a trumpet a whole tone sharp --
// so a measured per-profile calibration corrects it feed-forward. This pins
// that calibration: without it the brass profiles are unplayable out of the box.
// Tested with lips over each profile's practical low-to-mid register (the very
// top of the range is genuinely out of reach for the low instruments, as it is
// on the real ones, so it is not asserted here).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// The air jet (flute / panpipe). It has no valve -- a ribbon of air across the
// mouth edge, deflected by the pipe's own acoustic field a transit-time later.
// It must speak, hold the bore's pitch rather than running off at its own delay
// frequency (an earlier cut did exactly that), and give the odd-harmonic
// spectrum of a stopped pipe, which is what this closed-mouth bore is.
// ---------------------------------------------------------------------------
static void testAirJet()
{
    const int jetExciter = 4;   // Exciter::airJet
    for (int note : { 38, 45, 50, 55 })
    {
        EngineParams p;
        p.exciter = jetExciter;
        p.shape.profile = 1;      // cylinder -> recorder / panpipe
        p.pressure = 0.6f;
        p.humanize = 0.0f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        const auto m = hold (e, p, note, 3.0f);
        const float rms = rmsOf (m, 2.0f, 3.0f);
        CHECK (rms > 0.01f, "air jet did not speak at note %d: rms %.4f", note, rms);

        const float want = noteHz (note);
        const float f0 = estimateF0 (m, want, 2.0f, 3.0f);
        const float cents = 1200.0f * std::log2 (f0 / want);
        CHECK (std::abs (cents) < 35.0f,
               "air jet off pitch at note %d: %.0f cents (%.2f Hz for %.2f Hz) -- "
               "the jet ran off at its own delay frequency instead of the bore's",
               note, cents, f0, want);

        // Stopped pipe: odd harmonics dominate the even ones.
        const float h1 = std::pow (10.0f, goertzelDb (m, f0,        2.0f, 3.0f) / 20.0f);
        const float h2 = std::pow (10.0f, goertzelDb (m, f0 * 2.0f, 2.0f, 3.0f) / 20.0f);
        const float h3 = std::pow (10.0f, goertzelDb (m, f0 * 3.0f, 2.0f, 3.0f) / 20.0f);
        CHECK (h3 > h2 || h1 > 4.0f * h2,
               "air jet at note %d is not odd-harmonic (h1 %.4f h2 %.4f h3 %.4f)",
               note, h1, h2, h3);
    }
}

static void testProfileTuning()
{
    for (int prof = 0; prof < 12; ++prof)
    {
        for (int note : { 31, 38, 45, 52 })
        {
            EngineParams p;
            p.shape.profile = prof;
            p.pressure = 0.62f;
            p.humanize = 0.0f;
            DidgeEngine e;
            e.prepare (kFs, 256);
            const auto m = hold (e, p, note, 3.0f);
            const float rms = rmsOf (m, 2.0f, 3.0f);
            if (rms < 0.003f)
                continue;   // profile does not speak here; covered elsewhere
            const float want = noteHz (note);
            const float f0 = estimateF0 (m, want, 2.0f, 3.0f);
            const float cents = 1200.0f * std::log2 (f0 / want);
            CHECK (std::abs (cents) < 30.0f,
                   "profile %d first-note pitch %.0f cents off at note %d "
                   "(%.2f Hz for %.2f Hz) -- per-profile calibration regressed",
                   prof, cents, note, f0, want);
        }
    }
}

static void testExciterTypes()
{
    for (int ex = 0; ex < 4; ++ex)
    {
        for (int note : { 31, 38, 45, 52 })
        {
            EngineParams p;
            p.exciter = ex;
            p.pressure = 0.75f;
            p.humanize = 0.0f;
            DidgeEngine e;
            e.prepare (kFs, 256);
            const auto m = hold (e, p, note, 3.0f);

            bool finite = true;
            for (float v : m) if (! std::isfinite (v)) finite = false;
            CHECK (finite, "exciter %d produced non-finite output at note %d", ex, note);

            const float rms = rmsOf (m, 2.0f, 3.0f);
            CHECK (rms > 0.002f,
                   "exciter %d barely speaks at note %d: rms %.5f", ex, note, rms);

            const float want = noteHz (note);
            const float f0 = estimateF0 (m, want, 2.0f, 3.0f);
            const float cents = 1200.0f * std::log2 (f0 / want);
            CHECK (std::abs (cents) < 20.0f,
                   "exciter %d at note %d sounded %.1f cents off (%.2f Hz for %.2f Hz)",
                   ex, note, cents, f0, want);
        }
    }
}

// ---------------------------------------------------------------------------
// The striking direction, measured rather than asserted. An inward-striking
// valve is forced shut by mouth pressure, so past the beating pressure it stops
// speaking however hard it is blown -- a real clarinet does exactly this. Lips
// are blown open and only get louder. This is the one behaviour that cannot be
// faked by retuning a valve, so it is the proof the sign is doing its work.
// ---------------------------------------------------------------------------
static void testReedBeatsShut()
{
    auto levelAt = [] (int ex, float pressure)
    {
        EngineParams p;
        p.exciter = ex;
        p.pressure = pressure;
        p.embouchure = 0.12f;   // a tight reed beats shut at a reachable pressure
        p.humanize = 0.0f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        return rmsOf (hold (e, p, 38, 2.5f), 1.5f, 2.5f);
    };

    const float lipsSoft = levelAt (0, 0.35f), lipsHard = levelAt (0, 1.0f);
    CHECK (lipsHard > lipsSoft,
           "blowing lips harder should be louder, got %.5f then %.5f", lipsSoft, lipsHard);

    const float reedSoft = levelAt (1, 0.35f), reedHard = levelAt (1, 1.0f);
    CHECK (reedSoft > 0.002f,
           "a tight single reed should speak at moderate breath, rms %.5f", reedSoft);
    CHECK (reedHard < 0.25f * reedSoft,
           "a single reed should be choked by too much breath, not merely quieter: "
           "rms %.5f at moderate breath against %.5f at full", reedSoft, reedHard);

    // And the analytic threshold the model is built on: oscillation begins at a
    // third of the beating pressure, whatever the other parameters.
    const auto& s = exciterSpec (Exciter::singleReed);
    const float rest = s.restBias + s.restScale * 0.5f;
    CHECK (std::abs (reedThresholdPressure (s, rest) - beatingPressure (s, rest) / 3.0f) < 1.0f,
           "threshold should be a third of the beating pressure");
}

// ---------------------------------------------------------------------------
// Bore diameter. Two effects pull against each other and both are real: the
// characteristic impedance goes as 1/r^2, so a narrow tube stands a far larger
// pressure against the exciter and drives the wave harder into the nonlinear
// regime, while the wall boundary layer is a fixed thickness whatever the bore,
// so loss per unit length goes as 1/r and works the other way. Measured, the
// impedance wins: narrow is the bright one, which is also how a narrow-bore
// trumpet sounds against a large-bore one.
// ---------------------------------------------------------------------------
static void testBoreDiameter()
{
    auto brightness = [] (float diameter)
    {
        EngineParams p;
        p.shape.diameter = diameter;
        p.pressure = 0.8f;
        p.humanize = 0.0f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        const auto m = hold (e, p, 38, 3.0f);
        const float f0 = estimateF0 (m, noteHz (38), 2.0f, 3.0f);
        float num = 0.0f, den = 0.0f;
        for (int h = 1; h <= 16; ++h)
        {
            const float a = std::pow (10.0f, goertzelDb (m, f0 * h, 2.0f, 3.0f) / 20.0f);
            num += a * f0 * h;
            den += a;
        }
        return den > 0.0f ? num / den : 0.0f;
    };

    const float narrow = brightness (0.0f);
    const float mid    = brightness (0.5f);
    const float wide   = brightness (1.0f);

    CHECK (narrow > mid && mid > wide,
           "a narrower bore must be brighter: centroid %.0f Hz narrow, %.0f mid, %.0f wide",
           narrow, mid, wide);
    CHECK (narrow - wide > 80.0f,
           "bore diameter barely changed the timbre: %.0f Hz of centroid across the range",
           narrow - wide);

    // The whole range has to stay playable and finite.
    for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        EngineParams p;
        p.shape.diameter = d;
        p.pressure = 0.8f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        const auto m = hold (e, p, 38, 2.5f);
        bool finite = true;
        for (float v : m) if (! std::isfinite (v)) finite = false;
        CHECK (finite, "bore diameter %.2f produced non-finite output", d);
        CHECK (rmsOf (m, 1.5f, 2.5f) > 0.002f,
               "bore diameter %.2f barely speaks: rms %.5f", d, rmsOf (m, 1.5f, 2.5f));
    }
}

// ---------------------------------------------------------------------------
// The wave field the display is built on. A quadrature detector in the engine
// reports the pressure and the air's displacement at every segment boundary as
// complex amplitudes, so the interface can reconstruct the wave at its own
// frame rate. That is only worth anything if the field is real, so this checks
// its physics rather than its existence: a tube closed at the lips and open at
// the bell has its pressure antinode at the mouth and its node at the open end,
// with the air's displacement exactly the other way round, and the phase must
// advance from mouth to bell because energy is leaving through the bell.
// ---------------------------------------------------------------------------
static void testWaveField()
{
    EngineParams p;
    p.pressure = 0.8f;
    p.humanize = 0.0f;
    DidgeEngine e;
    e.prepare (kFs, 256);
    e.setSpectrumEnabled (true);          // the field is display telemetry
    hold (e, p, 38, 3.0f);

    const int N = Bore::kSegments;
    std::vector<float> pMag (N), dMag (N), pPhase (N);
    for (int i = 0; i < N; ++i)
    {
        const float re = e.vizPressureRe (i), im = e.vizPressureIm (i);
        pMag[i] = std::hypot (re, im);
        pPhase[i] = std::atan2 (im, re);
        dMag[i] = std::hypot (e.vizDisplaceRe (i), e.vizDisplaceIm (i));
    }

    float pTop = 0.0f, dTop = 0.0f;
    for (int i = 0; i < N; ++i) { pTop = std::max (pTop, pMag[i]); dTop = std::max (dTop, dMag[i]); }
    CHECK (pTop > 0.0f && dTop > 0.0f,
           "the wave field is empty while a note is sounding");

    // Pressure antinode at the lips, node at the open end.
    CHECK (pMag[0] > 0.6f * pTop,
           "pressure should be near its maximum at the mouth: %.2f of peak",
           pMag[0] / pTop);
    CHECK (pMag[N - 1] < 0.5f * pMag[0],
           "pressure should fall toward the open bell: %.2f at the mouth, %.2f at the bell",
           pMag[0] / pTop, pMag[N - 1] / pTop);

    // Displacement is the complement: the air barely moves where it is most
    // compressed, and moves most where it is free to.
    CHECK (dMag[N - 1] > dMag[0],
           "the air should swing more at the open end than at the lips: %.3g against %.3g",
           dMag[N - 1], dMag[0]);

    // Phase must actually vary along the bore. A field with one phase
    // everywhere is a pure standing wave, which only happens if nothing
    // radiates; a real bell means a travelling component, and it is that
    // phase progression the display turns into a moving wave.
    float spread = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        float d = std::abs (pPhase[i] - pPhase[0]);
        while (d > 3.14159265f) d = 6.2831853f - d;
        spread = std::max (spread, d);
    }
    CHECK (spread > 0.15f,
           "the wave field carries no phase progression (%.3f rad across the bore), "
           "so nothing in it can travel", spread);

    for (int i = 0; i < N; ++i)
    {
        CHECK (std::isfinite (pMag[i]) && std::isfinite (dMag[i]),
               "wave field went non-finite at segment %d", i);
    }
}

// ---------------------------------------------------------------------------
// A control must never be able to switch the instrument off. Lip damping used
// to: past a damping ratio of about 0.18 an outward-striking valve stops
// oscillating on this bore, and the top two thirds of the control ran straight
// past it -- thirty decibels down with the pitch running away. No amount of
// breath recovers it, because the drive falls as the square of the damping and
// pressure only helps as its square root. The control is held below that edge
// now, so every position sounds.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// The vocal tract shapes the drone; it must never switch it off. A real
// player's tongue colours the sound and cannot silence the instrument, so no
// combination of the voice, vowel and mouth controls may kill the drone. An
// earlier pass raised the tract impedance limit too far and created exactly
// that: full voice with a closed "oo" dropped the output thirty decibels, and
// only adding growl -- which brings its own oscillation -- restarted it. This
// scans the corner of the control space where that happened.
// ---------------------------------------------------------------------------
static void testTractNeverChokesTheDrone()
{
    // A reference level with the voice out of the way, to compare against.
    float ref = 0.0f;
    {
        EngineParams p;
        p.tractMix = 0.0f;
        p.pressure = 0.85f;
        p.humanize = 0.0f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        ref = rmsOf (hold (e, p, 38, 2.5f), 1.8f, 2.5f);
    }

    for (float mix : { 0.7f, 0.85f, 1.0f })
        for (float vx : { 0.0f, 0.1f, 0.25f, 0.5f, 1.0f })
            for (float vy : { 0.1f, 0.2f, 0.5f })
            {
                EngineParams p;
                p.tractMix = mix;
                p.vowelX = vx;
                p.vowelY = vy;
                p.pressure = 0.85f;
                p.growl = 0.0f;         // growl would mask the fault
                p.humanize = 0.0f;
                DidgeEngine e;
                e.prepare (kFs, 256);
                const float rms = rmsOf (hold (e, p, 38, 2.5f), 1.8f, 2.5f);
                CHECK (rms > 0.2f * ref,
                       "the tract choked the drone at voice %.2f vowel %.2f/%.2f: "
                       "%.5f against %.5f with the voice off (%.1f dB down)",
                       mix, vx, vy, rms, ref,
                       20.0f * std::log10 (std::max (1.0e-9f, rms / ref)));
            }
}

static void testLipDampingAlwaysSpeaks()
{
    float loud = 0.0f;
    for (float d : { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f })
    {
        EngineParams p;
        p.lipDamp = d;
        p.pressure = 0.75f;
        p.humanize = 0.0f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        const auto m = hold (e, p, 38, 3.0f);
        const float rms = rmsOf (m, 2.0f, 3.0f);
        loud = std::max (loud, rms);
        CHECK (rms > 0.004f,
               "lip damping %.2f stopped the instrument speaking: rms %.5f", d, rms);

        const float f0 = estimateF0 (m, noteHz (38), 2.0f, 3.0f);
        const float cents = 1200.0f * std::log2 (f0 / noteHz (38));
        CHECK (std::abs (cents) < 40.0f,
               "lip damping %.2f threw the pitch %.0f cents off", d, cents);
    }

    // And no position may be a small fraction of the loudest: a control that
    // technically still sounds but is 20 dB down is the same bug, quieter.
    for (float d : { 0.4f, 0.7f, 1.0f })
    {
        EngineParams p;
        p.lipDamp = d;
        p.pressure = 0.75f;
        p.humanize = 0.0f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        const float rms = rmsOf (hold (e, p, 38, 3.0f), 2.0f, 3.0f);
        CHECK (rms > 0.25f * loud,
               "lip damping %.2f is %.1f dB below the loudest setting",
               d, 20.0f * std::log10 (std::max (1.0e-9f, rms / loud)));
    }
}

// ---------------------------------------------------------------------------
// Material must change the timbre: a hard, smooth wall loses less at the top
// than a soft, rough one, so metal has to come out brighter than wood.
// ---------------------------------------------------------------------------
static void testMaterials()
{
    auto brightness = [] (int material)
    {
        EngineParams p;
        p.shape.material = material;
        p.pressure = 0.75f;
        DidgeEngine e;
        e.prepare (kFs, 256);
        const auto m = hold (e, p, 38, 4.0f);
        const float f0 = estimateF0 (m, noteHz (38), 3.0f, 4.0f);
        double num = 0.0, den = 0.0;
        for (int h = 1; h <= 40; ++h)
        {
            const double e2 = std::pow (10.0, goertzelDb (m, f0 * h, 3.0f, 4.0f) / 10.0);
            num += e2 * f0 * h;
            den += e2;
        }
        return den > 0.0 ? (float) (num / den) : 0.0f;
    };

    const float wood = brightness (0), glass = brightness (4);
    CHECK (glass > wood * 1.03f,
           "material did not change the timbre: wood centroid %.0f Hz, glass %.0f Hz",
           wood, glass);
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
    testDecayStage();
    testVelocityRouting();
    testPitchBend();
    testHumanize();
    testReleaseToSilence();
    testBoreProfiles();
    testAirJet();
    testProfileTuning();
    testExciterTypes();
    testReedBeatsShut();
    testBoreDiameter();
    testWaveField();
    testLipDampingAlwaysSpeaks();
    testTractNeverChokesTheDrone();
    testMaterials();
    testStability();
    testSampleRates();

    if (failures == 0)
        std::printf ("All DSP tests passed.\n");
    else
        std::printf ("%d DSP test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
