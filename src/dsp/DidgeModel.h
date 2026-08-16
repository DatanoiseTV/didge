/*
  Didge — physically modeled didgeridoo
  Copyright (C) 2026 DatanoiseTV

  This program is free software: you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version, and distributed WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/>.
  You must retain this notice and the attribution to DatanoiseTV in any
  redistributed or derivative version.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Framework-free physical model cores: fractional delay line, the segmented
// bore waveguide with its impedance-based tuner, the vocal tract, and the
// one-mass lip valve. The DidgeEngine composes these into an instrument.
namespace didge
{

inline constexpr float kSpeedOfSound = 343.0f;   // m/s at ~20 C
inline constexpr float kAirDensity   = 1.2f;     // kg/m^3

// ---------------------------------------------------------------------------
// Fractional delay line (linear interpolation). Power-of-two ring buffer.
// read(d) returns the sample written d writes ago (d >= 1); call read()
// before write() each sample for a d-sample loop delay.
// ---------------------------------------------------------------------------
class FracDelay
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 8;
        while (size < maxDelaySamples + 4) size <<= 1;
        buf.assign (static_cast<size_t> (size), 0.0f);
        mask = size - 1;
        w = 0;
    }

    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }

    void write (float x)
    {
        buf[static_cast<size_t> (w)] = x;
        w = (w + 1) & mask;
    }

    float read (float delaySamples) const
    {
        const int   di   = static_cast<int> (delaySamples);
        const float frac = delaySamples - static_cast<float> (di);
        // Wrap the INTEGER index (adding the buffer size before masking keeps
        // the intermediate non-negative); never index with the float.
        const int i0 = (w - di + (mask + 1)) & mask;
        const int i1 = (i0 - 1 + (mask + 1)) & mask;
        const float a = buf[static_cast<size_t> (i0)];
        const float b = buf[static_cast<size_t> (i1)];
        return a + frac * (b - a);
    }

    float maxDelay() const { return static_cast<float> (mask - 2); }

private:
    std::vector<float> buf;
    int mask = 7;
    int w = 0;
};

// ---------------------------------------------------------------------------
// Bore profile — radius along the tube from mouthpiece to bell. Real
// didgeridoos are termite-hollowed eucalyptus: roughly conical with a late
// flare and an irregular wall. The irregularity is a fixed seeded wobble so
// "texture" is deterministic and preset-stable.
// ---------------------------------------------------------------------------
// Which family of bore profile. This is not cosmetic: the profile decides the
// resonance series, and the series is most of what separates one instrument
// from another. A cylinder resonates at odd multiples only (1:3:5), a cone at
// every multiple (1:2:3) like a saxophone, and a horn that flares late behaves
// like brass. The natural profile is the irregular termite-hollowed tube.
// Brass and horn bores differ mainly in two numbers: how much of the length
// runs parallel before the bell starts, and how sharply the bell then opens.
// A trombone is about half cylindrical, a trumpet a third, a flugelhorn barely
// any; the bell itself opens slowly at first and then very fast, which a power
// law with a large exponent reproduces well enough for a playable model. Bore
// width matters too — a narrow tube has a high characteristic impedance and
// couples hard to the lips, which is why a trumpet is brighter and more
// resistant than a tuba of the same sounding length.
enum class BoreProfile
{
    natural = 0, cylinder, cone, flared, horn,
    trumpet, trombone, flugelhorn, frenchHorn, tuba, alphorn, contrabass
};

// cylFrac: parallel fraction before the bell. flarePow: bell exponent, larger
// opens later and faster. mouthMul / bellMul scale the two ends, so the family
// covers small bright instruments through to very large ones.
struct BoreGeometry { float cylFrac, flarePow, mouthMul, bellMul; bool cup; };

inline BoreGeometry boreGeometryFor (BoreProfile p, float flare)
{
    switch (p)
    {
        case BoreProfile::cylinder:   return { 1.00f, 1.0f,  1.00f, 0.06f, false };
        case BoreProfile::cone:       return { 0.00f, 1.0f,  1.00f, 1.00f, false };
        case BoreProfile::flared:     return { 0.00f, 0.55f + 0.5f * flare, 1.00f, 1.00f, false };
        case BoreProfile::horn:       return { 0.25f + 0.30f * (1.0f - flare), 1.6f, 1.00f, 1.00f, false };
        case BoreProfile::trumpet:    return { 0.35f, 4.0f,  0.62f, 0.85f, true };
        case BoreProfile::trombone:   return { 0.52f, 3.6f,  0.72f, 1.05f, true };
        case BoreProfile::flugelhorn: return { 0.12f, 2.2f,  0.78f, 1.00f, true };
        case BoreProfile::frenchHorn: return { 0.18f, 3.2f,  0.58f, 1.25f, true };
        case BoreProfile::tuba:       return { 0.10f, 2.4f,  1.30f, 1.45f, true };
        case BoreProfile::alphorn:    return { 0.05f, 1.15f, 1.10f, 1.20f, false };
        case BoreProfile::contrabass: return { 0.08f, 2.0f,  1.55f, 1.60f, true };
        case BoreProfile::natural:
        default:                      return { 0.00f, 1.0f + 3.0f * flare, 1.00f, 1.00f, false };
    }
}

// Per-profile pitch calibration, for lips.
//
// The linearised solver places the bore so its threshold oscillation lands on
// the note; but a note is played far past threshold, and how far the nonlinear
// sounding pitch sits from the threshold prediction depends on the bore shape.
// For the natural bore it is a few cents and the frequency-band learner mops it
// up. For the brass profiles it is tens to over a hundred cents, and the same
// on the first note before the learner has heard anything -- a trumpet an
// unplayable whole-tone sharp. Measured: with the tune control the sounding
// pitch tracks one-for-one (gain ~0.95, perfectly monotonic), so the whole
// error is a constant per-profile offset with a gentle slope across the range,
// not a broken plant. So it is corrected feed-forward here, measured at two
// reference notes (MIDI 34 and 54) and interpolated, which lands every brass
// first-note within ~15 cents; the learner takes it from there.
//
// Reeds are not covered: a single reed on a conical bore and any double reed
// jump registers (a non-monotonic plant), which a feed-forward offset cannot
// fix and which is tracked separately. The natural bore is left at zero so its
// existing prior and learner are untouched.
struct ProfilePitchCal { float loCents, hiCents; };   // measured at note 34, 54

inline float profilePitchCents (int profile, int midiNote)
{
    // Sharpness (cents) of each profile's first note, lips, default knobs.
    static const ProfilePitchCal cal[] = {
        {    0.0f,    0.0f },   // natural
        {  106.0f,   80.3f },   // cylinder
        {  -13.3f,  -11.0f },   // cone
        {  -14.9f,   -8.9f },   // flared
        {   82.4f,   65.3f },   // horn
        {  147.0f,   93.8f },   // trumpet
        {  129.9f,   72.3f },   // trombone
        {  116.2f,   91.3f },   // flugelhorn
        {  273.7f,  223.6f },   // frenchHorn
        {  -45.1f,   14.9f },   // tuba
        {  -53.6f,  -16.8f },   // alphorn
        {   -3.8f,   -4.6f },   // contrabass
    };
    const int n = static_cast<int> (sizeof (cal) / sizeof (cal[0]));
    const auto& c = cal[profile >= 0 && profile < n ? profile : 0];
    const float t = (static_cast<float> (midiNote) - 34.0f) / (54.0f - 34.0f);
    const float tc = std::max (-0.5f, std::min (1.5f, t));   // mild extrapolation
    return c.loCents + (c.hiCents - c.loCents) * tc;
}

// Wall material. Real wall losses grow with frequency (the viscous and thermal
// boundary layer scales with the square root of it) and rough, porous surfaces
// lose more than smooth hard ones. So the material sets both a broadband loss
// and a high-frequency corner: wood is dark and short, metal is bright and
// rings on.
enum class BoreMaterial { wood = 0, bamboo, brass, steel, glass };

struct BoreShape
{
    float bell    = 0.4f;   // 0..1 -> bell radius 18..80 mm
    float flare   = 0.5f;   // 0..1 -> flare exponent (1 = cone, higher = late bell)
    float texture = 0.3f;   // 0..1 -> wall irregularity depth
    float wallDamp = 0.3f;  // 0..1 -> wall losses (hard wood .. soft/leaky)
    float diameter = 0.5f;  // 0..1 -> overall bore width, 0.5x .. 2x
    int   profile  = 0;     // BoreProfile
    int   material = 0;     // BoreMaterial

    // Width scale. Exponential, so the control is symmetric in ratio and the
    // centre is the instrument the rest of the model was calibrated on.
    float widthScale() const
    {
        return std::pow (2.0f, 2.0f * (std::max (0.0f, std::min (1.0f, diameter)) - 0.5f));
    }

    bool differsFrom (const BoreShape& o) const
    {
        auto d = [] (float a, float b) { return std::abs (a - b) > 1.0e-6f; };
        return d (bell, o.bell) || d (flare, o.flare)
            || d (texture, o.texture) || d (wallDamp, o.wallDamp)
            || d (diameter, o.diameter)
            || profile != o.profile || material != o.material;
    }
};

// ---------------------------------------------------------------------------
// Linearised lip-valve load, used to predict the playing frequency before a
// note sounds. At the operating point the Bernoulli slit flow
//   u = w * y * sqrt(2|dp|/rho)
// and the lip ODE  (k - m w^2 + j w r) dy = -A dp_e  combine into a valve
// admittance  Y(w) = -du/dp_e = A*By/D(w) + Bp,  which is what the bore sees.
// Re(Y) only goes negative above the lip resonance — the signature of an
// outward-striking valve, and the reason a brass/didgeridoo embouchure sounds
// slightly above the bore's impedance peak rather than exactly on it.
// ---------------------------------------------------------------------------
// Effective lip parameters. Geometry follows the validated brass reference
// (Table 2), but the mass does not: that reference models a trombonist's
// tight, light lip (0.178 g resonating at 426 Hz), whereas a didgeridoo drone
// is played an octave and a half lower with a loose, heavy embouchure.
//
// What must stay bounded is the *static* opening, A*p/k — blow that past a
// couple of millimetres and the lips never close, the mouth end stops acting
// like a valve, and the pitch detaches from the bore. Holding the mass fixed
// makes k = m*w0^2 fall as f^2, so the low notes blow wide open. So the
// stiffness is the fixed quantity here and the mass follows from the
// resonance, m = k/w0^2 — which also matches the physical picture, since low
// notes really are played with more lip tissue in motion.
#ifndef DIDGE_KREF
#define DIDGE_KREF 420.0f
#endif
inline constexpr float kLipStiffness = DIDGE_KREF;  // N/m

// Most damping an outward-striking valve can carry and still oscillate on this
// bore, measured: at a damping ratio of 0.167 the drone is full strength, and
// by 0.19 it has collapsed by thirty decibels and the pitch runs away. The
// drive available to the valve falls as the square of the damping while
// blowing pressure only helps as its square root, so no amount of breath
// recovers it -- swept to the engine's maximum it does not come back. The
// control is therefore held here rather than being allowed to switch the
// instrument off. See the note on lip Q in the README: this ceiling is set by
// the bore's losses, and lifting it is the same problem as making the wall
// material more audible.
inline constexpr float kMaxLipZeta = 0.160f;
inline constexpr float kLipArea  = 1.0e-4f;      // m^2, projected lip surface
inline constexpr float kLipWidth = 1.2e-2f;      // m, slit width

// Effective mass for a valve resonance, from its stiffness.
inline float valveMassFor (float hz, float stiffness)
{
    const float w = 6.2831853f * std::max (8.0f, hz);
    return stiffness / (w * w);
}
inline float lipMassFor (float hz) { return valveMassFor (hz, kLipStiffness); }

// ---------------------------------------------------------------------------
// Excitation type.
//
// Every wind instrument is a resonator plus a device that turns steady breath
// into an oscillation, and there are only a few ways to build that device.
// The one that matters most is the direction the pressure across the valve
// pushes it, because it decides which side of a bore resonance the instrument
// sounds on:
//
//   Outward-striking (blown open, sign +1). Mouth pressure forces the valve
//   further open, so it opens when the bore pressure falls. Re(Y) goes
//   negative only ABOVE the valve's mechanical resonance, and the instrument
//   sounds slightly above the bore's impedance peak. Lips do this. The player
//   chooses the note by setting the lip resonance near it, which is why brass
//   players can play a whole harmonic series on one tube.
//
//   Inward-striking (blown closed, sign -1). Mouth pressure forces the valve
//   shut, and past a threshold it stays shut -- the beating pressure. It
//   sounds BELOW its own resonance, and because a cane reed resonates far
//   above any note it plays, the bore alone decides the pitch. That is why a
//   clarinettist does not choose the register with their embouchure the way a
//   trumpeter does.
//
// The free reed is the odd one out: it swings through its slot rather than
// against a seat, is barely damped, and its own resonance is so sharp that it
// sets the pitch and the pipe merely reinforces it. An accordion reed sounds
// at very nearly the same frequency with or without a pipe attached.
//
// The air jet is not a valve at all. See JetDrive.
//
// How strongly the player's vocal tract loads the exciter also depends on the
// type, and by a lot. A didgeridoo or brass embouchure is a wide, low-impedance
// aperture opening straight into the mouth, which is why Tarnopolsky et al.
// could measure the tract dominating the bore by more than an order of
// magnitude. A cane reed sits in a mouthpiece behind a slit a fraction of a
// millimetre high; Chen, Smith & Wolfe (JASA 126, 1511, 2009) found clarinet
// players need a tract impedance exceeding the bore's to bend a note or reach
// the altissimo, and that only advanced players manage it. Given a reed's
// nearly frequency-flat negative resistance, leaving the coupling at the
// didgeridoo value lets the tract seize the pitch outright: measured here, a
// single reed above D3 stopped tracking the keyboard and sat on a tract
// resonance instead, the same three frequencies for every note asked for.
//
// Sources for the numbers below: Fletcher & Rossing, The Physics of Musical
// Instruments, ch. 13-15 (valve classification, reed and lip parameters);
// Dalmont, Gilbert & Ollivier, JASA 118, 3294 (2005) for single-reed beating
// pressures around 5-8 kPa; Facchinetti, Boutillon & Constantinescu, JASA 114,
// 3345 (2003) for clarinet reed resonances near 2.2 kHz; St. Hilaire, Wilson &
// Beavers, JFM 91, 693 (1979) for free-reed behaviour.
// ---------------------------------------------------------------------------
enum class Exciter { lips = 0, singleReed, doubleReed, freeReed, airJet };
inline constexpr int kNumExciters = 5;

struct ExciterSpec
{
    float sign;        // +1 blown open (lips), -1 blown closed (cane reeds)
    float stiffness;   // N/m
    float area;        // m^2 the pressure difference acts on
    float width;       // m, slit width the flow passes through
    float dampScale;   // multiplies the player's damping control
    float restScale;   // maps the player's aperture control to metres
    float restBias;    // m, aperture at the bottom of that control
    float absHz;       // >0: resonance fixed here, whatever the note
    float ratio;       // else: resonance as a fraction of the sounding pitch
    bool  jet;         // non-reed: a jet across an open mouth, no valve at all
    float pressScale;  // breath control -> mouth pressure, relative to lips
    float trimDb;      // level trim, so switching type does not jump
    float trimCents;   // how flat this exciter plays before the learner runs
    bool  pitchFromValve; // apply that trim to the valve, not to the bore
    float tractCoupling;  // how strongly the player's mouth loads this exciter
};

// Beating pressure: the mouth pressure at which an inward-striking valve is
// forced shut and the instrument stops speaking. Only meaningful for sign < 0.
inline float beatingPressure (const ExciterSpec& s, float restOpening)
{
    return s.sign < 0.0f && s.area > 0.0f ? s.stiffness * restOpening / s.area : 0.0f;
}

// Oscillation threshold for an inward-striking valve. Setting the quasi-static
// Re(Y) = -A*By/k + Bp to zero reduces exactly to a third of the beating
// pressure, independent of every other parameter -- the classical result for a
// reed on a lossless resonator (Dalmont, Gilbert & Ollivier). Losses push the
// real threshold a little higher. It is worth stating here because it is the
// one number that decides whether a reed setting is playable: below it the
// instrument is silent, above the beating pressure it is choked.
inline float reedThresholdPressure (const ExciterSpec& s, float restOpening)
{
    return beatingPressure (s, restOpening) / 3.0f;
}

inline const ExciterSpec& exciterSpec (Exciter e)
{
    static const ExciterSpec table[kNumExciters] = {
        // Lips. The reference embouchure this model was built and tuned on.
        // sign   k              area      width      damp
        { +1.0f, kLipStiffness, kLipArea, kLipWidth, 1.0f,
        // restScale restBias  absHz  ratio  jet   press  trim
           1.8e-3f, -0.6e-3f,  0.0f,  0.90f, false, 1.00f,  0.0f,   0.0f, false, 1.00f },

        // Single cane reed, clarinet/saxophone. Effective stiffness per unit
        // area is about 8 MPa/m over roughly 1.4 cm^2, giving k = 1120 N/m and,
        // at its 2.2 kHz resonance, an effective mass of six milligrams -- both
        // in the measured range. A 1 mm tip opening then beats shut near 7 kPa,
        // which is where real clarinets stop speaking. Reeds want more breath
        // than lips do, hence the pressure scale.
        { -1.0f, 1120.0f, 1.4e-4f, 1.3e-2f, 3.0f,
           1.3e-3f,  0.25e-3f, 2200.0f, 0.0f, false, 1.90f,  3.6f, 54.0f, false, 0.15f },

        // Double reed, oboe/bassoon. Two blades beating against each other:
        // stiffer, much narrower, and damped hard enough by the lips that the
        // reed resonance is a broad hump rather than a peak. The narrow slit is
        // most of why a double reed is so much brighter than a single one, and
        // it is the hardest of these to blow, as it is in life.
        { -1.0f, 1800.0f, 1.1e-4f, 1.0e-2f, 5.0f,
           0.9e-3f,  0.18e-3f, 2400.0f, 0.0f, false, 2.40f, 12.5f, 46.0f, false, 0.10f },

        // Free reed, harmonica/accordion/khaen. A thin metal tongue that
        // nothing damps but the air, so its Q runs into the tens rather than
        // single figures. That sharp resonance sets the pitch and the pipe
        // follows it -- the reverse of every other exciter here. The soft
        // spring puts the moving mass between 0.02 and 0.7 g over the playing
        // range, which is what a real reed tongue weighs, and it speaks on a
        // fraction of the breath the cane reeds need.
        { -1.0f, 65.0f, 0.25e-4f, 5.0e-3f, 0.16f,
           0.7e-3f,  0.20e-3f,  0.0f, 1.02f, false, 0.50f, 12.0f, 87.0f, true,  0.22f },

        // Air jet, flute/recorder/panpipe. No valve at all: see JetDrive.
        {  0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
           1.0e-3f,  0.0f,      0.0f, 0.0f, true,  0.55f, 11.0f,  0.0f, false, 0.70f },
    };
    const int i = static_cast<int> (e);
    return table[i >= 0 && i < kNumExciters ? i : 0];
}

inline const char* exciterName (Exciter e)
{
    switch (e)
    {
        case Exciter::singleReed: return "Single Reed";
        case Exciter::doubleReed: return "Double Reed";
        case Exciter::freeReed:   return "Free Reed";
        case Exciter::airJet:     return "Air Jet";
        case Exciter::lips:
        default:                  return "Lips";
    }
}

struct LipLoad
{
    float mass    = 2.4e-3f;   // set via massForResonance() before solving
    float area    = kLipArea;
    float width   = kLipWidth;
    float zeta    = 0.125f;    // damping ratio
    float restOpening = 5.0e-4f;  // m
    float mouthPressure = 2500.0f; // Pa
    float sign    = +1.0f;     // +1 blown open, -1 blown closed
    float stiffness = kLipStiffness;

    // Steady operating point. The bore passes DC (it is open at the bell), so
    // the mean pressure drop across the valve is the mouth pressure and the
    // mean opening is the static spring balance. An inward-striking valve
    // subtracts instead of adding, so blowing harder closes it, and past the
    // beating pressure there is no opening left at all.
    float staticOpening() const
    {
        return std::max (2.0e-5f,
                         restOpening + sign * area * mouthPressure / stiffness);
    }

    // Valve admittance for a candidate resonance. Stiffness is fixed, so the
    // resonance selects the mass — the same mapping the running valve uses,
    // which is what makes the solver's prediction meaningful.
    //
    // The striking direction enters exactly once, on the term that couples the
    // valve's motion back into the flow. That single sign is what moves the
    // sounding pitch from above the bore resonance to below it.
    std::complex<float> admittance (float omega, float fLipHz) const
    {
        const float k   = stiffness;
        const float m   = valveMassFor (fLipHz, k);
        const float dp0 = std::max (1.0f, mouthPressure);
        const float By  = width * std::sqrt (2.0f * dp0 / kAirDensity);
        const float Bp  = width * staticOpening() / std::sqrt (2.0f * kAirDensity * dp0);
        const float r   = 2.0f * zeta * std::sqrt (m * k);
        const std::complex<float> D (k - m * omega * omega, omega * r);
        return sign * area * By / D + Bp;
    }
};

// ---------------------------------------------------------------------------
// Segmented cylindrical waveguide bore with Kelly-Lochbaum scattering
// junctions, a frequency-dependent open-end (bell) reflection, and a
// transfer-matrix tuner that places the model's own first impedance peak on
// the requested fundamental.
// ---------------------------------------------------------------------------
class Bore
{
public:
    static constexpr int kSegments = 16;
    static constexpr float kMouthRadius = 0.014f;   // m, ~28 mm mouthpiece bore

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        // Longest supported bore ~3.2 m (A0 territory) at 192 kHz.
        const int maxSeg = static_cast<int> ((3.2f / kSegments) / kSpeedOfSound * 192000.0f) + 8;
        for (int i = 0; i < kSegments; ++i)
        {
            fwd[i].prepare (maxSeg);
            bwd[i].prepare (maxSeg);
        }
        clear();
    }

    void clear()
    {
        for (int i = 0; i < kSegments; ++i)
        {
            fwd[i].clear(); bwd[i].clear();
            fPrev[i] = bPrev[i] = 0.0f;
            fLp[i] = bLp[i] = 0.0f;
            pAmp[i] = uAmp[i] = 0.0f;
        }
        uMean = 0.0f;
        bellLpState = 0.0f;
        bellLpState2 = 0.0f;
    }

    // Recompute radii, scattering targets and losses for a shape. Cheap; call
    // on shape-parameter changes. Junction coefficients then glide to their
    // targets per sample (see step()).
    void setShape (const BoreShape& s)
    {
        shape = s;

        // Overall width. Scaling every radius together keeps the profile's
        // shape and changes the size of the instrument. Two real effects pull
        // against each other here. The characteristic impedance goes as 1/r^2,
        // so a narrow tube stands a much larger pressure up against the
        // exciter, drives the wave further into the nonlinear regime and comes
        // out brighter; while the viscous and thermal boundary layer at the
        // wall is a fixed thickness whatever the bore, so loss per unit length
        // goes as 1/r and works the other way.
        //
        // Measured across this control, the impedance wins and by a long way:
        // the spectral centroid runs from about 490 Hz at the narrow end to
        // 160 Hz at the wide one. That is the right answer -- a narrow-bore
        // trumpet is the bright, brilliant one and a large-bore instrument the
        // broad, dark one -- but it is the opposite of what the loss term alone
        // would suggest, so it is worth stating that it was measured.
        const float dia = s.widthScale();
        const float rBell = (kMouthRadius + (0.080f - 0.018f) * s.bell) * dia;
        const auto prof = static_cast<BoreProfile> (s.profile);
        const auto geo = boreGeometryFor (prof, s.flare);
        const float rThroat = kMouthRadius * geo.mouthMul * dia;
        const float rEnd = std::max (rThroat * 1.02f, rBell * geo.bellMul);

        // A brass mouthpiece is not part of the taper: it is a wide cup
        // narrowing to a very tight throat before the bore proper. That
        // constriction against the cup volume is a Helmholtz resonator, and
        // its resonance is a large part of why brass sounds like brass. It is
        // also the one place the bore is not monotonic, so it needs its own
        // two segments -- and they are centimetres long where the bore's are
        // tens, hence the per-segment length scales.
        const int first = geo.cup ? 2 : 0;
        if (geo.cup)
        {
            // A tuba mouthpiece is not a trumpet's: cup and throat both scale
            // with the instrument, and using a trumpet-sized throat on a wide
            // bore is a severe mismatch that chokes the low instruments.
            radius[0] = kCupRadius * geo.mouthMul * dia;
            radius[1] = kThroatRadius * geo.mouthMul * dia;
            segScale[0] = kCupLenScale;
            segScale[1] = kThroatLenScale;
        }

        const int nBore = kSegments - first;
        for (int i = first; i < kSegments; ++i)
        {
            const float t = static_cast<float> (i - first) / static_cast<float> (nBore - 1);
            // Parallel lead pipe, then the bell opens over what is left.
            const float u = geo.cylFrac >= 0.999f
                          ? 0.0f
                          : std::max (0.0f, (t - geo.cylFrac)) / (1.0f - geo.cylFrac);
            float r = rThroat + (rEnd - rThroat) * std::pow (u, geo.flarePow);
            r *= 1.0f + s.texture * 0.22f * wobbleTable (i);
            radius[i] = r;
            segScale[i] = 1.0f;
        }
        for (int i = 0; i < kSegments; ++i)
        {
            radius[i] = std::max (0.0015f, radius[i]);
            area[i]   = 3.14159265f * radius[i] * radius[i];
        }
        scaleSum = 0.0f;
        for (int i = 0; i < kSegments; ++i) scaleSum += segScale[i];
        for (int i = 0; i < kSegments - 1; ++i)
        {
            const float z1 = 1.0f / area[i];
            const float z2 = 1.0f / area[i + 1];
            kTarget[i] = (z2 - z1) / (z2 + z1);
        }

        // Wall losses. The material sets how much is lost overall and how
        // sharply the loss rises with frequency; wallDamp trims around it.
        // Per-direction per-segment scalars, so the full round trip (2N
        // passes) lands on the wanted loop gain.
        // Material is specified by what it does over a whole round trip at a
        // reference frequency, then the per-traversal filter is solved to
        // deliver it. Specifying the filter directly does not work: thirty-two
        // traversals accumulate, so a one-pole at a musical corner strangles
        // the loop, while a one-zero gentle enough to be safe only removes
        // about 2 dB at 2 kHz and the material is then inaudible.
        static constexpr float matLoss[] = { 0.9950f, 0.9962f, 0.9975f, 0.9982f, 0.9987f };
        static constexpr float matHfDb[] = { -2.0f,  -1.3f,  -0.55f,  -0.3f,   -0.15f };
        const int mi = std::max (0, std::min (4, s.material));

        // Both the broadband loss and its high-frequency slope come from the
        // wall boundary layer, whose thickness does not depend on the bore, so
        // both scale with 1/radius.
        const float lossScale = std::max (0.35f, std::min (2.8f, 1.0f / dia));
        const float roundTrip = std::max (0.90f,
                                          1.0f - (1.0f - (matLoss[mi] - 0.12f * s.wallDamp))
                                                 * lossScale);
        gSeg = std::pow (roundTrip, 1.0f / (2.0f * kSegments));

        const float hfDb = matHfDb[mi] * (1.0f + 1.4f * s.wallDamp) * lossScale;
        wallLp = solveWallPole (hfDb);

        // Bell radiation / reflection cutoff.
        //
        // A brass bell is a horn, and a horn has a cutoff frequency: below it a
        // wave meets the flare, turns and reflects back down the bore; above it
        // the wave escapes and radiates. That cutoff is not set by the bell's
        // width alone but by how fast it flares -- Benade's horn cutoff rises
        // with the flare rate -- which is why a trumpet, whose bell opens
        // sharply, is bright while a tuba's slow-flaring bell of the same mouth
        // radius is dark. So the cutoff carries a flare term: geo.flarePow runs
        // from 1 for a straight cone to 4 for the trumpet.
        const float flareRate = geo.cup ? geo.flarePow : std::max (1.0f, geo.flarePow);
        const float fc = kSpeedOfSound / (6.2831853f * rBell) * 0.85f
                       * (0.55f + 0.40f * flareRate);

        // How SHARP the reflectance is depends on the flare, and this is the
        // whole difference between a brass bell and an open tube. A trumpet's
        // bell flares hard: it is a horn, its reflectance holds near total below
        // the cutoff and falls steeply above -- over well under an octave -- and
        // that sharp edge is what builds the strong high partials that, radiated,
        // become the brass formant. A didgeridoo has almost no bell; its open end
        // reflects gently, like a plain pipe, and radiates freely, so its wave
        // travels out rather than standing hard. A single reflectance order
        // cannot be both. So the reflection blends between one pole (gentle, open
        // pipe) and two (sharp, horn) with the flare rate: cones and rough tubes
        // stay open, the brass bells snap shut below their cutoff.
        bellSharp = std::max (0.0f, std::min (1.0f, (flareRate - 2.0f) / 2.0f));

        // Per-pole corner raised so the two-pole cascade's own -3 dB still lands
        // on the geometric cutoff.
        const float fcPole = fc / 0.644f;
        bellLpCoeff = 1.0f - std::exp (-6.2831853f * fcPole / fs);
    }

    // Set the acoustic length so the model's first *impedance peak* lands on
    // f0Target. This is the passive resonance, not the sounding pitch — use
    // tuneForPlayed to put the note itself in tune.
    void tuneTo (float f0Target)
    {
        f0Requested = f0Target;
        float L = kSpeedOfSound / (4.0f * f0Target);   // closed-open quarter-wave guess
        float f = firstImpedancePeak (L, f0Target);
        // Two secant-style corrections; the map L -> f0 is close to 1/L.
        for (int it = 0; it < 2 && f > 1.0f; ++it)
        {
            L *= f / f0Target;
            f = firstImpedancePeak (L, f0Target);
        }
        tunedF0 = f;
        segLenTarget = std::max (1.5f, (L / scaleSum) / kSpeedOfSound * fs);
        updateNonlinearity();
        tootF = nextImpedancePeak (L, f * 1.45f, f * 4.2f);
    }

    // Build the bore so the instrument SOUNDS f0Target when blown with the
    // given embouchure. The valve sustains only above the impedance peak, so
    // the bore comes out slightly longer than a naive quarter-wave — that
    // offset is solved for, not assumed. lipRatio is the nominal embouchure
    // (lip resonance as a fraction of the sounding pitch); the player's
    // tension control then bends around this calibration.
    // absHz > 0 pins the valve resonance there instead of tracking the note.
    // A cane reed resonates near 2 kHz whatever it is playing, which is
    // precisely why a reed instrument's pitch comes from its bore alone.
    void tuneForPlayed (float f0Target, const LipLoad& lip, float lipRatio,
                        float absHz = 0.0f)
    {
        f0Requested = f0Target;
        const float fLip = absHz > 0.0f ? absHz : f0Target * lipRatio;

        // Start from the passive-peak tuning and shorten/lengthen until the
        // solved sounding pitch matches. played ~ 1/L, so scaling the segment
        // length by played/target converges in a few passes.
        tuneTo (f0Target);
        float seg = segLenTarget;
        float played = 0.0f;
        for (int it = 0; it < 6; ++it)
        {
            played = playedFrequency (fLip, lip, f0Target * 0.75f, f0Target * 1.45f, seg,
                                      absHz > 0.0f);
            if (played <= 0.0f) break;
            const float err = played / f0Target;
            if (std::abs (err - 1.0f) < 1.0e-4f) break;
            seg = std::max (1.5f, seg * err);
        }
        if (played > 0.0f)
        {
            segLenTarget = seg;
            updateNonlinearity();
            tunedF0 = f0Target;
        }
        // else: keep the passive-peak tuning; the nonlinear model still speaks.

        // Second register: the next phase-closing solution when the embouchure
        // tightens. Reported to the UI and used as the toot target.
        // Second register. With lips the player firms up to reach it; with a
        // fixed reed resonance nothing about the exciter changes, so the bore's
        // own next mode is what answers -- which is why a clarinet overblows a
        // twelfth where a brass player can pick any harmonic.
        const float fTight = playedFrequency (absHz > 0.0f ? absHz
                                                           : f0Target * 2.0f * lipRatio,
                                              lip,
                                              f0Target * 1.45f, f0Target * 4.0f, segLenTarget);
        tootF = fTight > 0.0f ? fTight : tunedF0 * 2.2f;
    }

    // Which lip resonance sounds fTarget on the bore as currently tuned?
    // Solves the same phase condition with the frequency fixed and the lip
    // stiffness free — used to place the toot register on the played note.
    // Returns 0 if fTarget is not in a sustaining regime for this bore.
    float lipResonanceForPlayed (float fTarget, const LipLoad& lip) const
    {
        const float w = 6.2831853f * fTarget;
        const auto Z = impedanceAt (fTarget);
        auto loop = [&] (float fLip) { return Z * lip.admittance (w, fLip); };

        constexpr int steps = 96;
        const float lo = 0.30f * fTarget, hi = 1.05f * fTarget;
        float prevF = lo;
        auto prev = loop (lo);
        float bestA = 0.0f, bestB = 0.0f, bestGain = 0.0f;
        for (int i = 1; i <= steps; ++i)
        {
            const float f = lo + (hi - lo) * static_cast<float> (i) / steps;
            const auto g = loop (f);
            if ((prev.imag() < 0.0f) != (g.imag() < 0.0f))
            {
                const float re = 0.5f * (prev.real() + g.real());
                if (re < bestGain) { bestGain = re; bestA = prevF; bestB = f; }
            }
            prevF = f; prev = g;
        }
        if (bestB <= 0.0f) return 0.0f;

        float a = bestA, b = bestB, ea = loop (a).imag();
        for (int i = 0; i < 32; ++i)
        {
            const float mid = 0.5f * (a + b);
            const float em = loop (mid).imag();
            if ((ea < 0.0f) != (em < 0.0f)) b = mid;
            else { a = mid; ea = em; }
        }
        return 0.5f * (a + b);
    }

    // Continuous pitch bend, applied as a length change rather than a retune.
    // The sounding pitch of a tube goes as 1/length, so scaling the delay is
    // exact and costs nothing, where re-running the impedance solver every
    // block would not be. Physically this is a trombone slide.
    void setLengthScale (float scale)
    {
        lengthScale = std::max (0.25f, std::min (4.0f, scale));
    }

    float droneFrequency() const { return tunedF0; }
    float tootFrequency()  const { return tootF; }

    // Standing-wave state along the bore, for visualisation. Pressure is
    // p = f + b and volume flow is u = (f - b) * area / (rho*c), both taken at
    // the segment boundaries; these are the actual waveguide variables, so a
    // display built on them shows the real node and antinode positions rather
    // than an assumed mode shape. Tracked as peak envelopes because the UI
    // samples far more slowly than the wave oscillates.
    float segmentPressure (int i) const { return pAmp[i]; }
    float segmentFlow (int i)     const { return uAmp[i]; }
    float meanFlow()              const { return uMean; }

    // Instantaneous travelling-wave components at each segment boundary, valid
    // between beginStep() and the next beginStep(). Envelopes cannot show a
    // wave moving: they are amplitudes, and a display built on them can only
    // pulse everything in step. These are the actual waveguide variables, so
    // p = f + b and the particle velocity is (f - b)/(rho*c). Demodulating them
    // recovers the phase at each position, which is the part that makes a wave
    // travel rather than stand.
    float forwardWave (int i)  const { return fOut[i]; }
    float backwardWave (int i) const { return bOut[i]; }

    // Input impedance in SI units at the current tuning.
    std::complex<float> impedanceAt (float freq) const
    {
        return inputImpedanceNorm (freq, segLenTarget) * mouthImpedance();
    }

    // Frequency the coupled lip+bore loop will sound, for a given lip
    // resonance, searched in [fLo, fHi].
    //
    // Self-sustained oscillation needs the round trip to close in phase:
    // Im(Z*Y) = 0 with Re(Z*Y) < 0. Since Re(Y) only goes negative above the
    // lip resonance, and Z must therefore be on its capacitive flank, the
    // sounding pitch always lands slightly ABOVE a bore impedance peak — which
    // is why the bore is built a little long (see tuneForPlayed).
    // preferLowest picks the lowest sustaining regime in the window instead of
    // the strongest. With lips the strongest is right, because the player's own
    // resonance is what selects a register and the solver should follow it.
    // A cane reed selects nothing: its resonance sits far above every note, so
    // several bore modes satisfy the loop at once and the strongest is not
    // reliably the lowest. Without this a reed jumps to its second register at
    // some notes and not others, which is not a register change but a solver
    // artefact -- measured as a 66 cent leap between two adjacent semitones.
    float playedFrequency (float fLipHz, const LipLoad& lip,
                           float fLo, float fHi, float segLenSamples,
                           bool preferLowest = false) const
    {
        auto loop = [&] (float f)
        {
            const auto Z = inputImpedanceNorm (f, segLenSamples) * mouthImpedance();
            return Z * lip.admittance (6.2831853f * f, fLipHz);
        };

        constexpr int steps = 96;
        float prevF = fLo;
        auto prev = loop (fLo);
        float bestA = 0.0f, bestB = 0.0f, bestGain = 0.0f;
        for (int i = 1; i <= steps; ++i)
        {
            const float f = fLo + (fHi - fLo) * static_cast<float> (i) / steps;
            const auto g = loop (f);
            if ((prev.imag() < 0.0f) != (g.imag() < 0.0f))
            {
                const float re = 0.5f * (prev.real() + g.real());
                if (re < bestGain)
                {
                    bestGain = re; bestA = prevF; bestB = f;
                    if (preferLowest) break;
                }
            }
            prevF = f; prev = g;
        }
        if (bestB <= 0.0f)
            return 0.0f;   // no sustaining regime in this window

        float a = bestA, b = bestB;
        float ea = loop (a).imag();
        for (int i = 0; i < 32; ++i)
        {
            const float mid = 0.5f * (a + b);
            const float em = loop (mid).imag();
            if ((ea < 0.0f) != (em < 0.0f)) b = mid;
            else { a = mid; ea = em; }
        }
        return 0.5f * (a + b);
    }
    float segmentRadius (int i) const { return radius[i]; }
    float mouthArea() const { return area[0]; }
    float mouthImpedance() const { return kAirDensity * kSpeedOfSound / area[0]; }

    // Per-sample processing is split so the lip valve can read this sample's
    // incoming wave, solve the coupled flow, and only then inject its result:
    // beginStep() -> (lip solve) -> finishStep().
    // beginStep returns the backward wave arriving at the mouth.
    float beginStep()
    {
        // Glide delay length and junction coefficients toward their targets.
        segLen += (segLenTarget * lengthScale - segLen) * lenSmooth;
        for (int i = 0; i < kSegments - 1; ++i)
            kJunc[i] += (kTarget[i] - kJunc[i]) * juncSmooth;

        const float len = std::min (segLen, fwd[0].maxDelay());
        const float maxD = fwd[0].maxDelay();

        // Nonlinear (finite-amplitude) propagation.
        //
        // A didgeridoo is a long tube driven hard, which is exactly the regime
        // where sound no longer propagates linearly: local speed is c + beta*v,
        // so the compression half of each cycle catches up with the rarefaction
        // and the wave steepens toward a shock as it travels. This is what
        // makes brass instruments turn brassy as they get louder, and Silva et
        // al. measured the spectral centroid rising roughly threefold when it
        // is included. Modelled linearly, the same instrument sounds dull and
        // synthetic no matter how the excitation is tuned.
        //
        // Here it is applied as an amplitude-dependent delay: the time to cross
        // a segment is L/(c + beta*v) ~ (L/c)(1 - beta*p/(rho*c^2)), and the
        // effect accumulates over all sixteen segments. Using the previous
        // sample's value keeps the loop delay-free and stable.
        // The modulation is bounded to a fraction of the segment delay. Left
        // unbounded it is a feedback loop through a delay line whose length it
        // is itself setting, and at high drive that runs away: the drone
        // collapses into high-frequency noise instead of getting brassier.
        const float nlMax = 0.25f * len;
        for (int i = 0; i < kSegments; ++i)
        {
            const float df = std::max (-nlMax, std::min (nlMax, nlCoeff * fPrev[i]));
            const float db = std::max (-nlMax, std::min (nlMax, nlCoeff * bPrev[i]));
            const float li = len * segScale[i];
            const float lf = std::max (1.0f, std::min (maxD, li - df));
            const float lb = std::max (1.0f, std::min (maxD, li - db));
            // Wall loss: a broadband scalar plus a one-zero that trims the top,
            // since boundary-layer losses climb with frequency and a rough
            // wall loses more than a polished one.
            fLp[i] += wallLp * (fwd[i].read (lf) * gSeg - fLp[i]) + 1.0e-20f;
            bLp[i] += wallLp * (bwd[i].read (lb) * gSeg - bLp[i]) + 1.0e-20f;
            fOut[i] = fLp[i];   // right end of segment i
            bOut[i] = bLp[i];   // left end of segment i
            fPrev[i] = fOut[i];
            bPrev[i] = bOut[i];
        }
        return bOut[0];
    }

    // mouthIn is the forward pressure wave injected at the mouth; returns the
    // bell's radiated output.
    float finishStep (float mouthIn)
    {
        float fIn[kSegments], bIn[kSegments];
        fIn[0] = mouthIn;
        for (int i = 0; i < kSegments - 1; ++i)
        {
            const float k = kJunc[i];
            const float f1 = fOut[i];
            const float b2 = bOut[i + 1];
            bIn[i]     = k * f1 + (1.0f - k) * b2;          // into segment i, leftward
            fIn[i + 1] = (1.0f + k) * f1 - k * b2;          // into segment i+1, rightward
        }

        // Bell: reflection is an inverted second-order lowpass (two cascaded
        // poles); what is not reflected radiates. pRad = incident + reflected =
        // the complementary highpass, now with the sharper skirt of a real bell.
        const float incident = fOut[kSegments - 1];
        bellLpState  += bellLpCoeff * (incident - bellLpState)  + 1.0e-18f;
        bellLpState2 += bellLpCoeff * (bellLpState - bellLpState2) + 1.0e-18f;
        const float bellOut = bellLpState + bellSharp * (bellLpState2 - bellLpState);
        const float reflected = -0.995f * bellOut;
        bIn[kSegments - 1] = reflected;

        // Standing-wave envelopes, before the writes overwrite the scratch.
        for (int i = 0; i < kSegments; ++i)
        {
            const float p = fOut[i] + bOut[i];
            const float u = (fOut[i] - bOut[i]) * area[i] / (kAirDensity * kSpeedOfSound);
            const float ap = std::abs (p), au = std::abs (u);
            pAmp[i] += (ap > pAmp[i] ? envAttack : envRelease) * (ap - pAmp[i]);
            uAmp[i] += (au > uAmp[i] ? envAttack : envRelease) * (au - uAmp[i]);
        }
        uMean += 0.0008f * ((fOut[0] - bOut[0]) * area[0]
                            / (kAirDensity * kSpeedOfSound) - uMean);

        for (int i = 0; i < kSegments; ++i)
        {
            fwd[i].write (fIn[i]);
            bwd[i].write (bIn[i]);
        }
        return incident + reflected;
    }

    // beta * segmentLength / (rho * c^2), in delay-samples per pascal. beta =
    // (gamma+1)/2 for air. DIDGE_NL scales it so the effect can be measured
    // against a linear reference.
#ifndef DIDGE_NL
#define DIDGE_NL 5.0f
#endif
    void updateNonlinearity()
    {
        constexpr float beta = 1.2f;
        const float rhoC2 = kAirDensity * kSpeedOfSound * kSpeedOfSound;
        nlCoeff = DIDGE_NL * beta * segLenTarget / rhoC2;
    }

    void setSmoothing (double sampleRate)
    {
        // Envelope followers for the visualisation: quick to rise, slow to
        // fall, so the display tracks the wave without flickering.
        envAttack  = 1.0f - std::exp (-1.0f / (0.004f * static_cast<float> (sampleRate)));
        envRelease = 1.0f - std::exp (-1.0f / (0.090f * static_cast<float> (sampleRate)));
        // ~25 ms glide for lengths (note changes), ~12 ms for junctions
        // (vowel-speed shape morphs are not expected on the bore; this covers
        // bell/flare automation without zipper).
        lenSmooth  = 1.0f - std::exp (-1.0f / (0.025f * static_cast<float> (sampleRate)));
        juncSmooth = 1.0f - std::exp (-1.0f / (0.012f * static_cast<float> (sampleRate)));
    }

    // Snap glides (used at note-on from silence and in offline tests).
    void snapToTargets()
    {
        segLen = segLenTarget * lengthScale;
        for (int i = 0; i < kSegments - 1; ++i)
            kJunc[i] = kTarget[i];
    }

private:
    // One-pole coefficient giving `targetDb` of loss over a full round trip
    // (2 * kSegments traversals) at kWallRefHz. Bisection: the magnitude is
    // monotonic in the coefficient, and this runs only when the bore changes.
    float solveWallPole (float targetDb) const
    {
        // Referenced where this instrument actually keeps its energy. At 2 kHz the
        // spectrum is already 40 dB down, so a filter specified up there changes
        // almost nothing audible however aggressive it is.
        constexpr float kWallRefHz = 900.0f;
        const float w = 6.2831853f * std::min (kWallRefHz, 0.45f * fs) / fs;
        const float cosw = std::cos (w);
        const float wantPer = std::pow (10.0f, targetDb / (20.0f * 2.0f * kSegments));

        auto mag = [&] (float a)
        {
            const float b = 1.0f - a;
            return a / std::sqrt (std::max (1.0e-12f, 1.0f + b * b - 2.0f * b * cosw));
        };

        float lo = 0.02f, hi = 1.0f;
        for (int i = 0; i < 40; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            if (mag (mid) < wantPer) lo = mid; else hi = mid;
        }
        return 0.5f * (lo + hi);
    }

    // Deterministic per-segment wall irregularity in [-1, 1], smoothed.
    static float wobbleTable (int i)
    {
        struct Table
        {
            float v[kSegments];
            Table()
            {
                std::uint32_t s = 0x9e3779b9u;
                float prev = 0.0f;
                for (auto& e : v)
                {
                    s = s * 1664525u + 1013904223u;
                    const float raw = (static_cast<float> ((s >> 8) & 0xffff) / 32768.0f) - 1.0f;
                    prev = 0.55f * prev + 0.45f * raw;   // neighbouring segments correlate
                    e = prev;
                }
            }
        };
        static const Table table;
        return table.v[i];
    }

    // Z_in of the *discrete* loop (same delays, junction coefficients, losses
    // and bell filter as step()) via reflectance back-propagation: across a
    // junction  G' = (k + G) / (1 + k G),  through a segment
    // G' = D^2 G with D = gSeg * e^{-jw*len*Ts}. Normalised by Zc; multiply by
    // mouthImpedance() for SI units.
    std::complex<float> inputImpedanceNorm (float freq, float segLenSamples) const
    {
        using cf = std::complex<float>;
        const float w = 6.2831853f * freq / fs;
        // Same per-traversal loss the running waveguide applies, so the tuner
        // is describing the loop that actually sounds.
        const cf z1w = std::polar (1.0f, -w);
        const cf lpW = wallLp / (1.0f - (1.0f - wallLp) * z1w);
        auto Dof = [&] (int i)
        {
            const cf d = std::polar (gSeg, -w * segLenSamples * segScale[i]) * lpW;
            return d * d;
        };

        // Bell reflection: -0.995 * a second-order (two-pole) lowpass, matching
        // the running model in finishStep so the tuner describes the loop that
        // actually sounds.
        const cf z1 = std::polar (1.0f, -w);
        const cf lp = bellLpCoeff / (1.0f - (1.0f - bellLpCoeff) * z1);
        cf G = -0.995f * (lp + bellSharp * (lp * lp - lp));

        G *= Dof (kSegments - 1);                   // through last segment
        for (int i = kSegments - 2; i >= 0; --i)
        {
            const float k = kTarget[i];
            G = (k + G) / (1.0f + k * G);           // across junction i
            G *= Dof (i);                           // through segment i
        }
        return (1.0f + G) / (1.0f - G);
    }

    float inputImpedanceMag (float freq, float segLenSamples) const
    {
        return std::abs (inputImpedanceNorm (freq, segLenSamples));
    }

    float firstImpedancePeak (float lengthMeters, float fHint)
    {
        const float segSamples = std::max (1.5f, (lengthMeters / scaleSum) / kSpeedOfSound * fs);
        return peakScan (segSamples, std::max (20.0f, fHint * 0.55f), fHint * 1.6f);
    }

    float nextImpedancePeak (float lengthMeters, float fLo, float fHi)
    {
        const float segSamples = std::max (1.5f, (lengthMeters / scaleSum) / kSpeedOfSound * fs);
        return peakScan (segSamples, fLo, fHi);
    }

    float peakScan (float segSamples, float fLo, float fHi)
    {
        // First strict local maximum of |Z| on the grid. The scan may start
        // on the falling flank of a lower peak, so a simple rising flag is
        // not enough — require z[i-1] < z[i] >= z[i+1].
        constexpr int steps = 160;
        float zGrid[steps + 1];
        for (int i = 0; i <= steps; ++i)
        {
            const float f = fLo + (fHi - fLo) * static_cast<float> (i) / steps;
            zGrid[i] = inputImpedanceMag (f, segSamples);
        }
        int best = -1;
        for (int i = 1; i < steps; ++i)
            if (zGrid[i] > zGrid[i - 1] && zGrid[i] >= zGrid[i + 1]) { best = i; break; }
        if (best < 0)   // no interior maximum: fall back to the grid maximum
        {
            best = 0;
            for (int i = 1; i <= steps; ++i)
                if (zGrid[i] > zGrid[best]) best = i;
        }
        float bestF = fLo + (fHi - fLo) * static_cast<float> (best) / steps;
        // Parabolic refinement around the winning bin.
        const float df = (fHi - fLo) / steps;
        const float zm = inputImpedanceMag (bestF - df, segSamples);
        const float z0 = inputImpedanceMag (bestF, segSamples);
        const float zp = inputImpedanceMag (bestF + df, segSamples);
        const float denom = zm - 2.0f * z0 + zp;
        if (std::abs (denom) > 1.0e-9f)
        {
            float d = 0.5f * (zm - zp) / denom;
            d = std::max (-1.0f, std::min (1.0f, d));
            bestF += d * df;
        }
        return bestF;
    }

    float fs = 48000.0f;
    BoreShape shape;

    FracDelay fwd[kSegments], bwd[kSegments];
    float radius[kSegments] {}, area[kSegments] {};
    float segScale[kSegments] {};
    float scaleSum = static_cast<float> (kSegments);

    // Mouthpiece geometry, in metres and in units of a nominal bore segment.
    static constexpr float kCupRadius     = 0.0085f;
    static constexpr float kThroatRadius  = 0.0021f;
    static constexpr float kCupLenScale   = 0.22f;
    static constexpr float kThroatLenScale = 0.07f;
    float kTarget[kSegments - 1] {}, kJunc[kSegments - 1] {};
    float fOut[kSegments] {}, bOut[kSegments] {};   // beginStep -> finishStep scratch
    float fPrev[kSegments] {}, bPrev[kSegments] {}; // last read, for the nonlinear delay
    float nlCoeff = 0.0f;

    // Visualisation envelopes of the standing wave.
    float fLp[kSegments] {}, bLp[kSegments] {};   // wall-loss filter state
    float wallLp = 1.0f;
    float pAmp[kSegments] {}, uAmp[kSegments] {};
    float uMean = 0.0f;
    float envAttack = 0.02f, envRelease = 0.0015f;

    float segLen = 8.0f, segLenTarget = 8.0f, lengthScale = 1.0f;
    float gSeg = 0.999f;
    float bellLpCoeff = 0.1f, bellLpState = 0.0f, bellLpState2 = 0.0f, bellSharp = 0.0f;
    float lenSmooth = 0.01f, juncSmooth = 0.01f;

    float tunedF0 = 73.4f, tootF = 190.0f, f0Requested = 73.4f;
};

// ---------------------------------------------------------------------------
// Vocal tract — 8-section Kelly-Lochbaum tube from glottis to lips, with
// morphable vowel area functions. The glottis end is a mostly-closed
// (high-impedance) termination where lung pressure is injected; the lip end
// hands its waves to the lip valve. Coupled in series with the bore, the
// tract's impedance minima become the formants of the radiated drone.
// ---------------------------------------------------------------------------
class VocalTract
{
public:
    static constexpr int kSections = 8;
    static constexpr float kLength = 0.175f;        // m, glottis to lips

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        const int maxSeg = static_cast<int> ((kLength / kSections) / kSpeedOfSound * 192000.0f) + 6;
        for (int i = 0; i < kSections; ++i)
        {
            fwd[i].prepare (maxSeg);
            bwd[i].prepare (maxSeg);
        }
        segLen = std::max (1.05f, (kLength / kSections) / kSpeedOfSound * fs);
        kSmooth = 1.0f - std::exp (-1.0f / (0.014f * fs));
        setGlottisCorner (320.0f);
        clear();
        setVowel (0.5f, 0.5f);
        for (int i = 0; i < kSections - 1; ++i) kJunc[i] = kTarget[i];
        zLipSm = zLipTarget;
    }

    void clear()
    {
        for (int i = 0; i < kSections; ++i) { fwd[i].clear(); bwd[i].clear(); }
        glottisX1 = glottisY1 = 0.0f;
    }

    // vowelX: 0..1 morphs u -> o -> a -> e -> i. vowelY: 0..1 jaw/mouth
    // openness (0 = nearly closed hum, 1 = wide open).
    void setVowel (float vowelX, float vowelY)
    {
        // Area tables in cm^2, glottis -> mouth. The tract ends BEHIND the
        // lips (the lip valve is its own model), so the final section is the
        // mouth cavity; vowel colour comes from where the tongue constricts.
        //
        // The constrictions are deliberately severe — down to a few square
        // millimetres against a wide cavity. A gentler set of areas leaves the
        // tube acoustically near-uniform, so every vowel resonates at the same
        // half-wave mode of its own length (measured: 984 Hz for all five) and
        // the tongue control does nothing. It is the ratio between the narrow
        // and wide parts that moves a formant, not the average area.
        //
        // Position of the constriction is what distinguishes them: back for
        // "ah", mid-palate for "ee", front for "oo".
        static constexpr float U[kSections] = { 6.5f, 7.5f, 7.0f, 5.5f, 3.2f, 1.6f, 1.1f, 1.6f };
        static constexpr float O[kSections] = { 5.5f, 6.5f, 6.5f, 5.5f, 3.6f, 2.2f, 1.8f, 2.4f };
        static constexpr float A[kSections] = { 2.0f, 1.3f, 1.1f, 1.8f, 3.6f, 6.0f, 7.5f, 7.5f };
        static constexpr float E[kSections] = { 3.2f, 2.2f, 1.7f, 2.1f, 3.2f, 4.6f, 4.6f, 3.8f };
        static constexpr float I[kSections] = { 6.0f, 7.5f, 7.0f, 3.4f, 1.3f, 1.0f, 1.8f, 3.2f };
        static constexpr const float* anchors[5] = { U, O, A, E, I };

        const float pos = std::max (0.0f, std::min (1.0f, vowelX)) * 4.0f;
        const int   a0  = std::min (3, static_cast<int> (pos));
        const float mix = pos - static_cast<float> (a0);

        const float open = 0.35f + 1.75f * std::max (0.0f, std::min (1.0f, vowelY));

        float areaCm[kSections];
        for (int i = 0; i < kSections; ++i)
        {
            float a = anchors[a0][i] + mix * (anchors[a0 + 1][i] - anchors[a0][i]);
            // Openness scales the oral half progressively toward the lips.
            const float w = static_cast<float> (i) / static_cast<float> (kSections - 1);
            a *= std::pow (open, w);
            areaCm[i] = std::max (0.4f, a);
        }

        for (int i = 0; i < kSections - 1; ++i)
        {
            const float z1 = 1.0f / areaCm[i];
            const float z2 = 1.0f / areaCm[i + 1];
            kTarget[i] = (z2 - z1) / (z2 + z1);
        }
        // Characteristic impedance at the lip end, SI (areas cm^2 -> m^2).
        zLipTarget = kAirDensity * kSpeedOfSound / (areaCm[kSections - 1] * 1.0e-4f);
        for (int i = 0; i < kSections; ++i)
            lastAreaCm[i] = areaCm[i];
    }

    float lipEndImpedance() const { return zLipSm; }
    float sectionAreaCm (int i) const { return lastAreaCm[i]; }

    // Glottis transition frequency: below it the airway looks open to the
    // lungs, above it the narrow glottis reflects. See setGlottisCorner.
    void setGlottisCorner (float hz)
    {
        const float t = std::tan (3.14159265f * std::max (20.0f, hz) / fs);
        glottisA = (1.0f - t) / (1.0f + t);
    }

    // Split like Bore: beginStep() returns the forward wave arriving at the
    // lips; finishStep() injects lung pressure and the lips' reflection.
    float beginStep()
    {
        for (int i = 0; i < kSections - 1; ++i)
            kJunc[i] += (kTarget[i] - kJunc[i]) * kSmooth;
        zLipSm += (zLipTarget - zLipSm) * kSmooth;

        for (int i = 0; i < kSections; ++i)
        {
            fOut[i] = fwd[i].read (segLen) * kSectionLoss;
            bOut[i] = bwd[i].read (segLen) * kSectionLoss;
        }
        return fOut[kSections - 1];
    }

    void finishStep (float lungInject, float bFromLips)
    {
        // Glottis: a frequency-dependent termination, not a fixed reflection.
        //
        // The lungs are an enormous reservoir, so at low frequency the airway
        // looks open (reflection -> -1, a pressure release) and steady breath
        // passes straight through. The narrow glottis is inertive, so above a
        // few hundred hertz it looks closed (reflection -> +1) and the tract
        // resonances survive to make formants — the "partially closed glottis"
        // Tarnopolsky et al. identify as what an experienced player does.
        //
        // A fixed reflective glottis gets this badly wrong: it makes the tract
        // input impedance huge at the drone frequency too, which chokes the
        // oscillation outright (measured: fundamental 45 dB down, drone dead).
        // A first-order allpass is exactly -1 at DC and +1 at Nyquist, is
        // lossless, and puts the crossover where it belongs.
        float fIn[kSections], bIn[kSections];
        const float gIn = bOut[0];
        const float gOut = glottisA * gIn - glottisX1 + glottisA * glottisY1;
        glottisX1 = gIn;
        glottisY1 = gOut;
        fIn[0] = kGlottisLoss * gOut + lungInject;

        for (int i = 0; i < kSections - 1; ++i)
        {
            const float k = kJunc[i];
            const float f1 = fOut[i];
            const float b2 = bOut[i + 1];
            bIn[i]     = k * f1 + (1.0f - k) * b2;
            fIn[i + 1] = (1.0f + k) * f1 - k * b2;
        }
        bIn[kSections - 1] = bFromLips;

        for (int i = 0; i < kSections; ++i)
        {
            fwd[i].write (fIn[i]);
            bwd[i].write (bIn[i]);
        }
    }

    // The glottis must be held nearly shut for the tract to resonate at all.
    // Tarnopolsky et al. (Nature 436, 39, 2005) found that strong formants
    // "require the glottis to be partially closed to enhance reflection and to
    // prevent the resonant high-frequency components being absorbed in the
    // resistive impedance of the lungs", and identified exactly this as what
    // separates an experienced didgeridoo player from a novice. An open,
    // absorptive glottis leaves the tract too damped to shape anything.
#ifndef DIDGE_GLOTTIS
#define DIDGE_GLOTTIS 0.92f
#endif
    static constexpr float kGlottisRefl  = DIDGE_GLOTTIS;
    // Slight loss at the glottis so the tract has a finite Q instead of
    // ringing forever; the allpass itself is lossless.
#ifndef DIDGE_GLOSS
#define DIDGE_GLOSS 0.985f
#endif
    static constexpr float kGlottisLoss  = DIDGE_GLOSS;
#ifndef DIDGE_TLOSS
#define DIDGE_TLOSS 0.9995f
#endif
    static constexpr float kSectionLoss  = DIDGE_TLOSS;

private:
    float fs = 48000.0f;
    FracDelay fwd[kSections], bwd[kSections];
    float kTarget[kSections - 1] {}, kJunc[kSections - 1] {};
    float lastAreaCm[kSections] {};
    float fOut[kSections] {}, bOut[kSections] {};   // beginStep -> finishStep scratch
    float segLen = 3.0f;
    float kSmooth = 0.01f;
    float glottisA = 0.96f, glottisX1 = 0.0f, glottisY1 = 0.0f;
    float zLipTarget = 4.0e6f, zLipSm = 4.0e6f;
};

// ---------------------------------------------------------------------------
// One-mass outward-striking lip valve, after the validated Menguy-Gilbert /
// Bilbao brass exciter (F. Silva et al., "Time-domain simulation of brass
// instruments", eqs 45-49). The top lip is a plate of mass m on a spring k
// and damper r; the aeroacoustic force A*(p_mouth - p_entry) blows it open.
// Bernoulli slit flow + mass conservation give the tube-entry pressure p_e in
// closed form from the incoming wave and the opening, so no separate
// flow-vs-impedance quadratic is needed. Integrated with the unconditionally
// stable Newmark scheme (beta=1/4, eta=1/2) and a fixed point on p_e(y).
//
// The lips play at/just above their own resonance and lock to whichever bore
// impedance peak sits nearby — set the resonance and the register follows,
// exactly like a real embouchure.
// ---------------------------------------------------------------------------
class LipValve
{
public:
    void prepare (double sampleRate)
    {
        dt = 1.0f / static_cast<float> (sampleRate);
        setResonance (70.0f);
        reset();
    }

    void reset()
    {
        y = yEq;
        vel = 0.0f;
        acc = 0.0f;
    }

    // Switch excitation type: lips, one of the cane reeds, or a free reed.
    // Stiffness, area, slit width and striking direction all change together,
    // so this is applied as a unit rather than as separate controls.
    void setSpec (const ExciterSpec& s)
    {
        sign  = s.sign;
        k     = s.stiffness;
        aVal  = s.area;
        wVal  = s.width;
        setResonance (fRes);
    }

    // Valve mechanical resonance (Hz). Stiffness is fixed per exciter type, so
    // the resonance selects the effective mass; for lips a higher resonance is
    // a tighter, lighter embouchure and a higher register.
    void setResonance (float hz)
    {
        fRes = hz;
        mLip = valveMassFor (hz, k);
        updateDamping();
    }
    void setDamping (float zeta) { zeta_ = std::max (0.02f, zeta); updateDamping(); }

    // Rest (equilibrium) opening in metres. Negative = lips pressed together;
    // the mouth pressure must blow them apart before any flow, giving a real
    // pressure threshold and a hard buzz.
    void setRestOpening (float m) { yEq = m; }

    float opening() const         { return std::max (0.0f, y); }
    void  kickVelocity (float dv) { vel += dv; }   // stability probes / tests

    struct Result
    {
        float flow;      // volume flow through the lips (m^3/s), mouth -> bore
        float deltaP;    // pressure across the lips (Pa)
    };

    // Solve the valve against the tract and the bore in series.
    //
    //   drive = 2 * (tract forward wave) - 2 * (bore backward wave) + lung DC
    //   zSum  = Z_tract + Z_bore
    //
    // The pressure across the lips is then dp = drive - zSum * u, and Bernoulli
    // slit flow gives u = sgn(dp) * w * y * sqrt(2|dp|/rho); the two together
    // are a quadratic in sqrt(|dp|) with a closed-form root.
    //
    // Putting the tract impedance in this series sum is what produces the
    // didgeridoo's formants. Tarnopolsky et al. (Nature 436, 39, 2005) measured
    // that the peaks of the radiated spectrum line up with the MINIMA of the
    // vocal-tract impedance: where the tract impedance is low, flow through the
    // lips is free and that frequency radiates; where it is high, flow is
    // choked. A valve that only sees the bore — as this one first did — cannot
    // reproduce that at all, and sounds like a buzzing pipe instead of an
    // instrument being shaped by a mouth.
    Result step (float drive, float zSum)
    {
        // Newmark predictors (eq 50).
        const float yPred = y + dt * vel + (1.0f - 2.0f * kBeta) * (dt * dt * 0.5f) * acc;
        const float vPred = vel + (1.0f - kEta) * dt * acc;

        const float denom = mLip + r * kEta * dt + k * kBeta * dt * dt;

        // Fixed point: y_{n+1} depends on the force, which depends on dp(y).
        float yN = y;
        for (int it = 0; it < 6; ++it)
        {
            // The striking direction: a positive pressure across the lips
            // drives them apart, and across a cane reed drives it shut.
            const float f = sign * aVal * solveDeltaP (yN, drive, zSum);
            const float yNext = yPred
                + kBeta * dt * dt * (f - r * vPred - k * (yPred - yEq)) / denom;
            const float diff = std::abs (yNext - yN);
            yN = yNext;
            if (diff < 1.0e-14f) break;
        }

        acc = (yN - yPred) / (kBeta * dt * dt);
        vel = vPred + kEta * dt * acc;
        y   = yN;

        // Guards against runaway states (retunes from silence, denormals).
        y   = std::max (-6.0e-3f, std::min (10.0e-3f, y));
        vel = std::max (-60.0f,   std::min (60.0f,   vel));

        const float dp = solveDeltaP (y, drive, zSum);
        float flow = 0.0f;
        if (y > 0.0f)
            flow = (dp >= 0.0f ? 1.0f : -1.0f) * wVal * y
                 * std::sqrt (2.0f * std::abs (dp) / kAirDensity);
        // Physical didgeridoo flows stay well under a litre per second; a
        // larger value is a numerical excursion, not a breath.
        flow = std::max (-3.0e-3f, std::min (3.0e-3f, flow));
        return { flow, dp };
    }

    float valveArea() const { return aVal; }

private:
    void updateDamping()
    {
        // r = 2 * zeta * sqrt(m k). Reference default zeta = 1/8 (r=sqrt(mk)/4).
        r = 2.0f * zeta_ * std::sqrt (mLip * k);
    }

    // Pressure across the lips, solving Bernoulli slit flow against the series
    // impedance of tract and bore:
    //     dp = drive - zSum * u,   u = sgn(dp) * a * sqrt(|dp|),  a = w*y*sqrt(2/rho)
    // Substituting s = sqrt(|dp|) gives s^2 + zSum*a*s - |drive| = 0, whose
    // positive root is closed form. Closed lips pass no flow, so dp = drive.
    float solveDeltaP (float yOpen, float drive, float zSum) const
    {
        if (yOpen <= 0.0f)
            return drive;
        const float a = wVal * yOpen * std::sqrt (2.0f / kAirDensity);
        const float za = zSum * a;
        const float s = 0.5f * (-za + std::sqrt (za * za + 4.0f * std::abs (drive)));
        const float dp = s * s;
        return drive >= 0.0f ? dp : -dp;
    }

    static constexpr float kBeta = 0.25f;
    static constexpr float kEta  = 0.5f;

    float dt = 1.0f / 48000.0f;
    float y = 5.0e-4f, vel = 0.0f, acc = 0.0f;
    float mLip = 2.4e-3f;
    float k = kLipStiffness, r = 0.0f, zeta_ = 0.125f;
    float yEq = 5.0e-4f;
    float sign = +1.0f, aVal = kLipArea, wVal = kLipWidth, fRes = 70.0f;
};

// ---------------------------------------------------------------------------
// Air-jet drive (flute / recorder / panpipe family).
//
// A flute has no valve. A ribbon of air blown across a sharp edge is deflected
// sideways by the acoustic velocity in the pipe mouth, and because the jet
// takes a finite time to cross the gap, that deflection arrives back at the
// edge delayed -- the delay is the phase shift that closes the feedback loop
// and sustains the oscillation. The jet then feeds more or less of its flow to
// one side of the edge, a saturating nonlinear function of how far it has been
// pushed. This is the McIntyre-Schumacher-Woodhouse jet drive (JASA 74, 1325,
// 1983), the same lumped model used in Cook's STK flute.
//
// The nonlinearity is the odd cubic q(x) = x - x^3 near the origin: small
// deflections feed proportionally, large ones saturate and fold back. It is
// odd, so like a cane reed it favours the odd harmonics of a stopped pipe,
// which is what this bore is -- closed at the mouth, open at the bell. The
// result is a recorder/panpipe voice, breathy and hollow, not a valve buzz.
//
// The jet transit delay sets which register speaks: about a third of the
// sounding period puts the loop phase on the fundamental. It is derived from
// the note, so the flute tracks the keyboard through the same bore the reeds
// and lips use.
// ---------------------------------------------------------------------------
class JetDrive
{
public:
    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        jet.prepare (static_cast<int> (fs / 20.0f) + 8);   // down to ~20 Hz
        reset();
    }
    void reset() { jet.clear(); yJet = 0.0f; }

    // The jet transit time, as a fraction of the sounding period. ~1/3 of a
    // period is the classic value that selects the fundamental; a shorter
    // transit (harder, faster jet) favours the octave, which is how a flute
    // overblows.
    void setFrequency (float hz, float transitFraction)
    {
        const float period = fs / std::max (20.0f, hz);
        tau = std::max (2.0f, std::min (jet.maxDelay(), period * transitFraction));
    }
    void setDrive (float gain) { jetGain = std::max (0.05f, gain); }

    struct Result { float flow; float deltaP; };

    // acPressure: the acoustic pressure returning from the bore mouth (the AC
    // field that deflects the jet -- NOT the steady breath, which only sets how
    // much flow the jet carries). zSum: series mouth+bore impedance. breath: the
    // steady blowing pressure (Pa), which scales the jet velocity and hence both
    // the flow and how hard the nonlinearity is driven.
    Result step (float acPressure, float zSum, float breath)
    {
        // Only the oscillating field deflects the jet, and it arrives back at
        // the edge one transit later. Feeding the steady breath in here is what
        // let the loop run off at its own delay frequency instead of the bore's;
        // the breath belongs in the flow magnitude, below, not in the phase.
        jet.write (acPressure);
        const float deflected = jet.read (tau);

        // The jet has inertia: it follows the deflection through a light lowpass
        // rather than tracking it instantly.
        yJet += 0.5f * (deflected - yJet);

        // How hard the nonlinearity is driven scales with the jet velocity,
        // which scales with sqrt(breath) (Bernoulli). A stronger blow both
        // brightens the tone and, past a point, overblows -- real flute
        // behaviour, and here it stays bounded because q folds back.
        const float uj = std::sqrt (std::max (0.0f, breath) / kBreathRef);
        const float x = std::max (-4.0f, std::min (4.0f, yJet * jetScale * uj));
        const float q = x - (x * x * x) * (1.0f / 3.0f);   // odd, folds at |x|~1

        // Flow into the bore: the jet's mean throughput (set by breath) times
        // the nonlinear selection q, damped by the series impedance it works
        // against.
        const float g = 1.0f / (1.0f + zSum * kJetAdmit);
        float flow = kJetFlow * jetGain * std::max (0.0f, breath) * q * g;
        flow = std::max (-3.0e-3f, std::min (3.0e-3f, flow));
        return { flow, q };
    }

    float excursion() const { return yJet; }

private:
    static constexpr float jetScale  = 9.0e-4f;   // acoustic pressure -> jet offset
    static constexpr float kJetFlow  = 6.0e-7f;   // jet flow scale
    static constexpr float kJetAdmit = 2.0e-6f;   // series-impedance loading
    static constexpr float kBreathRef = 2000.0f;  // breath giving unit jet velocity

    FracDelay jet;
    float fs = 48000.0f;
    float tau = 40.0f, yJet = 0.0f, jetGain = 1.0f;
};

} // namespace didge
