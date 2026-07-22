/*
  Qube — quadraphonic spatial panner
  Copyright (C) 2026 DatanoiseTV

  Framework-free unit tests for the spatial DSP cores. Exit code != 0 on any
  failure; each check prints its own diagnostic.
*/

#include "dsp/QuadPanner.h"
#include "dsp/Motion.h"
#include "dsp/BinauralRenderer.h"
#include "dsp/UhjEncoder.h"
#include "dsp/RoomVerb.h"
#include "dsp/SpatialEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

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

static constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
static void testPanLaw()
{
    using namespace qube::quad;

    // Equal power at every azimuth, point source, fully directional.
    for (int deg = -180; deg <= 180; deg += 3)
    {
        const auto g = panGains (static_cast<float> (deg) * kPi / 180.0f, 0.0f, 1.0f);
        float p = 0.0f;
        for (auto v : g) p += v * v;
        CHECK (std::abs (p - 1.0f) < 1.0e-4f, "pan power at %d deg = %f", deg, p);
    }

    // Front centre: FL == FR, rears silent.
    {
        const auto g = panGains (0.0f, 0.0f, 1.0f);
        CHECK (std::abs (g[0] - g[1]) < 1.0e-5f, "front-centre FL %f != FR %f", g[0], g[1]);
        CHECK (g[2] < 1.0e-5f && g[3] < 1.0e-5f, "front-centre rears not silent: %f %f", g[2], g[3]);
        CHECK (std::abs (g[0] - 0.70710678f) < 1.0e-4f, "front-centre FL %f != 1/sqrt2", g[0]);
    }

    // Hard FL: everything in speaker 0.
    {
        const auto g = panGains (-45.0f * kPi / 180.0f, 0.0f, 1.0f);
        CHECK (std::abs (g[0] - 1.0f) < 1.0e-4f, "hard FL gain %f", g[0]);
        CHECK (g[1] < 1.0e-4f && g[2] < 1.0e-4f && g[3] < 1.0e-4f, "hard FL leakage %f %f %f", g[1], g[2], g[3]);
    }

    // Behind: RL == RR.
    {
        const auto g = panGains (kPi, 0.0f, 1.0f);
        CHECK (std::abs (g[2] - g[3]) < 1.0e-4f, "rear-centre RL %f != RR %f", g[2], g[3]);
        CHECK (g[0] < 1.0e-4f && g[1] < 1.0e-4f, "rear-centre fronts not silent");
    }

    // Interior blend: at the listener position all four speakers are equal.
    {
        const auto g = panGains (0.7f, 0.4f, 0.0f);
        for (int i = 0; i < 4; ++i)
            CHECK (std::abs (g[static_cast<size_t> (i)] - 0.5f) < 1.0e-4f, "interior gain[%d] = %f", i, g[static_cast<size_t> (i)]);
    }

    // Spread: still power-normalised, and pushes energy into neighbours.
    {
        const auto g = panGains (0.0f, 1.0f, 1.0f);
        float p = 0.0f;
        for (auto v : g) p += v * v;
        CHECK (std::abs (p - 1.0f) < 1.0e-4f, "spread power %f", p);
        CHECK (g[2] > 0.01f && g[3] > 0.01f, "full spread should reach the rears: %f %f", g[2], g[3]);
    }
}

// ---------------------------------------------------------------------------
static void testMotion()
{
    qube::Motion m;
    m.reset();

    // Orbit at phase 0: at the top of the circle (front), radius scaled.
    {
        const auto o = m.offsetFor (qube::Motion::Mode::orbit, 0.0, 0.8f, 0.001);
        CHECK (std::abs (o.dx) < 1.0e-5f && std::abs (o.dy - 0.8f) < 1.0e-5f,
               "orbit(0) = %f,%f", o.dx, o.dy);
    }
    // Orbit at quarter phase: right.
    {
        const auto o = m.offsetFor (qube::Motion::Mode::orbit, 0.25, 0.8f, 0.001);
        CHECK (std::abs (o.dx - 0.8f) < 1.0e-5f && std::abs (o.dy) < 1.0e-4f,
               "orbit(0.25) = %f,%f", o.dx, o.dy);
    }
    // Deterministic: same phase, same result (non-random modes are pure).
    {
        const auto a = m.offsetFor (qube::Motion::Mode::figure8, 0.37, 0.6f, 0.001);
        const auto b = m.offsetFor (qube::Motion::Mode::figure8, 0.37, 0.6f, 0.001);
        CHECK (a.dx == b.dx && a.dy == b.dy, "figure8 not deterministic");
    }
    // Random stays inside the radius and moves.
    {
        m.reset (1234);
        float maxR = 0.0f;
        qube::Motion::Offset last {};
        bool moved = false;
        for (int i = 0; i < 2000; ++i)
        {
            const auto o = m.offsetFor (qube::Motion::Mode::random, i * 0.01, 0.5f, 0.01);
            maxR = std::max (maxR, std::sqrt (o.dx * o.dx + o.dy * o.dy));
            if (i > 10 && (o.dx != last.dx || o.dy != last.dy)) moved = true;
            last = o;
        }
        CHECK (maxR <= 0.5f * 1.45f, "random exceeded radius: %f", maxR);   // sqrt2 corner allowance
        CHECK (moved, "random walk never moved");
    }
}

// ---------------------------------------------------------------------------
// Measure the phase of a sinusoid in a buffer via exact LSQ projection.
static float sinePhase (const std::vector<float>& buf, int from, int to, float freq, float fs)
{
    double sc = 0.0, ss = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double w = 2.0 * M_PI * freq * i / fs;
        sc += buf[static_cast<size_t> (i)] * std::cos (w);
        ss += buf[static_cast<size_t> (i)] * std::sin (w);
    }
    return static_cast<float> (std::atan2 (sc, ss));
}

static void testPhaseQuadrature()
{
    constexpr float fs = 48000.0f;
    for (float freq : { 200.0f, 1000.0f, 5000.0f, 10000.0f })
    {
        qube::PhaseQuadrature q;
        q.reset();
        const int n = 48000;
        std::vector<float> ref (static_cast<size_t> (n)), sh (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            const float x = std::sin (2.0f * kPi * freq * static_cast<float> (i) / fs);
            const auto p = q.process (x);
            ref[static_cast<size_t> (i)] = p.ref;
            sh[static_cast<size_t> (i)]  = p.shifted;
        }
        // Measure over the settled tail.
        const float pr = sinePhase (ref, n / 2, n, freq, fs);
        const float psh = sinePhase (sh, n / 2, n, freq, fs);
        float d = (psh - pr) * 180.0f / kPi;
        while (d > 180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        CHECK (std::abs (d - 90.0f) < 3.0f, "quadrature at %.0f Hz: %f deg", freq, d);
    }
}

// ---------------------------------------------------------------------------
static void testUhj()
{
    // A left-positioned source must land louder in L than R; front-centre must
    // be mono-compatible (L+R carries the signal, no gross cancellation).
    auto renderUhj = [] (const std::array<float, 4>& gains, float freq, std::vector<float>& L, std::vector<float>& R)
    {
        qube::UhjEncoder enc;
        enc.reset();
        constexpr int n = 24000;
        std::vector<float> spk[4];
        for (auto& s : spk) s.assign (n, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const float x = std::sin (2.0f * kPi * freq * static_cast<float> (i) / 48000.0f);
            for (int s = 0; s < 4; ++s)
                spk[s][static_cast<size_t> (i)] = x * gains[static_cast<size_t> (s)];
        }
        L.assign (n, 0.0f); R.assign (n, 0.0f);
        const float* sp[4] = { spk[0].data(), spk[1].data(), spk[2].data(), spk[3].data() };
        enc.process (sp, L.data(), R.data(), n);
    };

    auto energy = [] (const std::vector<float>& b)
    {
        double e = 0.0;
        for (size_t i = b.size() / 2; i < b.size(); ++i) e += b[i] * b[i];
        return e;
    };

    std::vector<float> L, R;

    // Hard left (FL only).
    renderUhj ({ 1.0f, 0.0f, 0.0f, 0.0f }, 500.0f, L, R);
    CHECK (energy (L) > energy (R) * 2.0, "UHJ left source: L %f R %f", energy (L), energy (R));

    // Front centre: mono sum survives.
    renderUhj ({ 0.7071f, 0.7071f, 0.0f, 0.0f }, 500.0f, L, R);
    std::vector<float> mono (L.size());
    for (size_t i = 0; i < L.size(); ++i) mono[i] = 0.5f * (L[i] + R[i]);
    CHECK (energy (mono) > 0.25 * energy (L), "UHJ mono compatibility: mono %f L %f", energy (mono), energy (L));
}

// ---------------------------------------------------------------------------
static void testBinaural()
{
    qube::BinauralRenderer b;
    b.prepare (48000.0);

    // FL speaker (index 0): the RIGHT ear (index 1) is contralateral -> it
    // gets the ITD delay and the head-shadow HF cut.
    CHECK (b.delaySamplesFor (0, 1) > 0, "FL right-ear ITD = %d", b.delaySamplesFor (0, 1));
    CHECK (b.delaySamplesFor (0, 0) == 0, "FL left-ear delay = %d", b.delaySamplesFor (0, 0));
    CHECK (b.hfGainFor (0, 0) > b.hfGainFor (0, 1), "FL shadow: L hf %f should exceed R hf %f",
           b.hfGainFor (0, 0), b.hfGainFor (0, 1));
    // Rear speakers darker than fronts on the ipsilateral ear (front/back cue).
    CHECK (b.hfGainFor (0, 0) > b.hfGainFor (2, 0), "rear should be darker: front %f rear %f",
           b.hfGainFor (0, 0), b.hfGainFor (2, 0));

    // Impulse through FL reaches the left ear earlier and louder.
    constexpr int n = 256;
    std::vector<float> spk0 (n, 0.0f), z (n, 0.0f), L (n), R (n);
    spk0[0] = 1.0f;
    const float* sp[4] = { spk0.data(), z.data(), z.data(), z.data() };
    b.process (sp, L.data(), R.data(), n);
    int firstL = -1, firstR = -1;
    for (int i = 0; i < n; ++i)
    {
        if (firstL < 0 && std::abs (L[static_cast<size_t> (i)]) > 1.0e-4f) firstL = i;
        if (firstR < 0 && std::abs (R[static_cast<size_t> (i)]) > 1.0e-4f) firstR = i;
    }
    CHECK (firstL >= 0 && firstR > firstL, "ITD arrival: L %d R %d", firstL, firstR);
}

// ---------------------------------------------------------------------------
static void testRoomVerb()
{
    qube::RoomVerb rv;
    rv.prepare (48000.0, 512);
    rv.setParams (0.5f, 0.5f);

    constexpr int blocks = 300;                 // ~3.2 s
    std::vector<float> in (512, 0.0f);
    std::vector<float> out[4];
    for (auto& o : out) o.assign (512, 0.0f);

    double earlyEnergy = 0.0, lateEnergy = 0.0;
    for (int bl = 0; bl < blocks; ++bl)
    {
        std::fill (in.begin(), in.end(), 0.0f);
        if (bl == 0) in[0] = 1.0f;
        for (auto& o : out) std::fill (o.begin(), o.end(), 0.0f);
        rv.process (in.data(), { out[0].data(), out[1].data(), out[2].data(), out[3].data() }, 1.0f, 512);
        double e = 0.0;
        for (const auto& o : out)
            for (float v : o)
            {
                CHECK (std::isfinite (v), "room NaN at block %d", bl);
                e += v * v;
                if (! std::isfinite (v)) return;
            }
        if (bl < 20) earlyEnergy += e;
        if (bl >= blocks - 100) lateEnergy += e;
    }
    CHECK (earlyEnergy > 1.0e-6, "room produced no early energy");
    CHECK (lateEnergy < earlyEnergy * 0.05, "room tail not decaying: early %g late %g", earlyEnergy, lateEnergy);
}

// ---------------------------------------------------------------------------
static void testEngine()
{
    qube::SpatialEngine e;
    e.prepare (48000.0, 512);

    qube::EngineParams p;
    p.roomMix = 0.0f;
    p.distAmount = 0.0f;
    p.airAbsorb = 0.0f;
    p.spread = 0.0f;
    p.outputMode = 1;   // quad
    qube::TransportInfo tr;

    std::vector<float> inL (512), inR (512);
    std::vector<float> out[4];
    for (auto& o : out) o.assign (512, 0.0f);
    float* outPtr[4] = { out[0].data(), out[1].data(), out[2].data(), out[3].data() };

    auto runBlocks = [&] (int count)
    {
        for (int bl = 0; bl < count; ++bl)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float x = std::sin (2.0f * kPi * 440.0f * static_cast<float> (bl * 512 + i) / 48000.0f);
                inL[static_cast<size_t> (i)] = x;
                inR[static_cast<size_t> (i)] = x;
            }
            const float* inPtr[2] = { inL.data(), inR.data() };
            e.process (inPtr, 2, outPtr, 4, 512, p, tr);
        }
    };

    // Hard left-front: FL should dominate FR strongly after smoothing settles.
    p.posX = -1.0f; p.posY = 1.0f;
    runBlocks (40);
    auto chEnergy = [&] (int c)
    {
        double s = 0.0;
        for (float v : out[c]) s += v * v;
        return s;
    };
    (void) chEnergy;
    // Measure on a fresh block after settle.
    runBlocks (1);
    double eFL = 0, eFR = 0, eRL = 0, eRR = 0;
    for (int i = 0; i < 512; ++i)
    {
        eFL += out[0][static_cast<size_t> (i)] * out[0][static_cast<size_t> (i)];
        eFR += out[1][static_cast<size_t> (i)] * out[1][static_cast<size_t> (i)];
        eRL += out[2][static_cast<size_t> (i)] * out[2][static_cast<size_t> (i)];
        eRR += out[3][static_cast<size_t> (i)] * out[3][static_cast<size_t> (i)];
    }
    CHECK (eFL > 100.0 * std::max (1.0e-12, eFR), "hard-left FL %g vs FR %g", eFL, eFR);
    CHECK (eFL > 100.0 * std::max (1.0e-12, eRR), "hard-left FL %g vs RR %g", eFL, eRR);
    (void) eRL;

    // All finite in every output mode, including mode switches (crossfade).
    for (int m = 0; m <= 4; ++m)
    {
        p.outputMode = m;
        p.roomMix = 0.4f;
        p.doppler = 0.5f;
        p.motionMode = 1;   // orbit
        p.motionRateHz = 2.0f;
        runBlocks (20);
        for (int c = 0; c < 4; ++c)
            for (float v : out[c])
                CHECK (std::isfinite (v), "non-finite output in mode %d", m);
    }

    // Stereo bus: quad request must fall back to binaural, not silence.
    {
        std::vector<float> o2[2] { std::vector<float> (512), std::vector<float> (512) };
        float* o2p[2] = { o2[0].data(), o2[1].data() };
        p.outputMode = 1;   // quad requested
        p.roomMix = 0.0f; p.motionMode = 0; p.doppler = 0.0f;
        const float* inPtr[2] = { inL.data(), inR.data() };
        for (int bl = 0; bl < 10; ++bl)
            e.process (inPtr, 2, o2p, 2, 512, p, tr);
        CHECK (e.lastRenderMode() == qube::RenderMode::binaural, "stereo-bus fallback mode %d",
               static_cast<int> (e.lastRenderMode()));
        double eo = 0.0;
        for (float v : o2[0]) eo += v * v;
        CHECK (eo > 1.0e-4, "stereo-bus output silent");
    }
}

// ---------------------------------------------------------------------------
int main()
{
    testPanLaw();
    testMotion();
    testPhaseQuadrature();
    testUhj();
    testBinaural();
    testRoomVerb();
    testEngine();

    if (failures == 0)
    {
        std::printf ("qube_dsp_tests: all checks passed\n");
        return 0;
    }
    std::printf ("qube_dsp_tests: %d FAILURE(S)\n", failures);
    return 1;
}
