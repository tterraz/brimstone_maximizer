/*
 * Brimstone Maximizer - DSP.
 *
 * Copyright (C) 2026 Brimstone authors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.  See COPYING.
 *
 * Core: the algorithm of Steve Harris' LADSPA "satan_maximiser" (swh-plugins, GPL), also
 * known as "Barry's Satan Maximiser":
 *
 *   env  = |x| if |x| > env, else env + (|x| - env) / decay   (instant attack)
 *   gain = 1 / max(env, knee)
 *   y[n] = x[n - round(decay / 2)] * gain[n]                  (short lookahead)
 *
 * With unit = samples, oversampling 1x, clip off, ceiling 0 dB, drive 0 dB, stereo
 * independent, mix 100 %, gain 0 dB, the output is the original one delayed by a fixed
 * latency (tests/test_dsp.cpp).
 *
 * Signal flow (internal rate = host rate x oversampling):
 *   x -> upsample -> [drive -> maximizer core -> x ceiling] mixed with dry -> gain
 *     -> clip at ceiling -> downsample -> safety clamp -> level match (or delta) -> bypass crossfade
 *
 * Delta: output minus the same chain with the maximizer gain held at its makeup (1 / knee) and
 * no clipping, i.e. the delayed upsampled input x (1 + (drive ceiling / knee - 1) mix) gain,
 * downsampled by its own filter (so the filters cancel): what the maximizer and the clipper
 * change, without the makeup gain (no level match).  Computed only while Delta is on;
 * crossfaded over 10 ms like Bypass, which has priority.
 *
 * Latency: the signal is always delayed by a fixed Lfix internal samples, and the gain by
 * Lfix - round(decay / 2), so that the decay can change without changing the latency.
 * When round(decay / 2) changes, a second detector (envelope + gain history) starts with the
 * new decay and lookahead and the gains are crossfaded over 5 ms: each detector stays
 * consistent (lookahead matched to its decay), so there is neither click nor overshoot.
 *
 * Denormal numbers: in silence the envelope decays to the smallest denormal and stays there
 * (1.4e-45 x (1 - 1/decay) rounds to itself), and denormal arithmetic is slow on x86 when the
 * host leaves FTZ / DAZ off (silence 1.4 to 2.1 times slower, tests/bench_cpu.cpp).  run()
 * switches FTZ / DAZ on and restores the state of the host when it returns (ScopedNoDenormals).
 */

#include "DistrhoPlugin.hpp"
#include "BrimstoneParams.hpp"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>

#if defined(__SSE__) || defined(_M_X64)
# include <xmmintrin.h>
#endif

START_NAMESPACE_DISTRHO

// Flush-to-zero and denormals-are-zero (SSE) for the lifetime of the object, then the previous
// state (it belongs to the host thread).  Nothing on other processors.
class ScopedNoDenormals
{
public:
#if defined(__SSE__) || defined(_M_X64)
    ScopedNoDenormals() : fSaved(_mm_getcsr()) { _mm_setcsr(fSaved | 0x8040u); }   // FTZ bit 15, DAZ bit 6
    ~ScopedNoDenormals() { _mm_setcsr(fSaved); }

private:
    const unsigned fSaved;
#endif
};

static inline float dbToGain(const float db) { return std::pow(10.0f, db * 0.05f); }

static inline float gainToDb(const float g, const float floorDb)
{
    return g > 0.0f ? std::fmax(20.0f * std::log10(g), floorDb) : floorDb;
}

static inline unsigned nextPow2(unsigned n)
{
    unsigned p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

// ------------------------------------------------------------------------------------------------
// 2x half-band FIR stage (Kaiser windowed sinc, beta 8, Taps = 4k + 3), polyphase up/down.
// Group delay: (Taps - 1) / 2 samples at the high rate for up, as many for down.
// The stage at the host Nyquist frequency has 107 taps: flat to 0.01 dB up to 20 kHz at a host
// rate of 44.1 kHz, >= 69 dB of rejection above 24.1 kHz (47 taps, before: -2.1 dB at 20 kHz,
// 19 dB at 24.1 kHz).  The second stage of 4x cuts at 44.1 kHz: 47 taps are enough.

template <int Taps>
class HalfbandStage
{
public:
    static_assert(Taps % 4 == 3, "half-band: odd centre, only one non-zero odd tap");
    static const int kTaps = Taps;
    static const int kCenter = (Taps - 1) / 2;
    static const int kPhase0 = kCenter + 1;   // even taps
    static const int kDelayHigh = 2 * kCenter;   // up + down, samples at the high rate

    static const float* coefficients()
    {
        static float h[kTaps];
        static bool ready = false;
        if (!ready)
        {
            const double beta = 8.0;
            double sum = 0.0;
            for (int n = 0; n < kTaps; ++n)
            {
                const double t = (n - kCenter) * 0.5;
                const double sinc = t == 0.0 ? 1.0 : std::sin(M_PI * t) / (M_PI * t);
                const double r = (n - kCenter) / static_cast<double>(kCenter);
                const double w = besselI0(beta * std::sqrt(std::fmax(0.0, 1.0 - r * r))) / besselI0(beta);
                h[n] = static_cast<float>(0.5 * sinc * w);
                sum += h[n];
            }
            for (int n = 0; n < kTaps; ++n)
                h[n] = static_cast<float>(h[n] / sum);
            ready = true;
        }
        return h;
    }

    void reset()
    {
        std::memset(fUp, 0, sizeof(fUp));
        std::memset(fDown, 0, sizeof(fDown));
        fUpPos = fDownPos = 0;
    }

    // one low-rate sample -> two high-rate samples
    void up(const float x, float* const out)
    {
        const float* const h = coefficients();
        fUpPos = fUpPos == 0 ? kPhase0 - 1 : fUpPos - 1;
        fUp[fUpPos] = fUp[fUpPos + kPhase0] = x;
        const float* const xs = fUp + fUpPos;   // xs[k] = x[n - k]
        float y0 = 0.0f;
        for (int k = 0; k < kPhase0; ++k)
            y0 += h[2 * k] * xs[k];
        out[0] = 2.0f * y0;
        out[1] = 2.0f * h[kCenter] * xs[kCenter / 2];   // the only non-zero odd tap
    }

    // two high-rate samples -> one low-rate sample
    float down(const float* const in)
    {
        const float* const h = coefficients();
        // output at the time of in[0]: delay kCenter high-rate samples
        push(in[0]);
        const float* const us = fDown + fDownPos;   // us[j] = u[m - j]
        float y = 0.0f;
        for (int j = 0; j < kTaps; ++j)
            y += h[j] * us[j];
        push(in[1]);
        return y;
    }

private:
    static double besselI0(const double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    void push(const float u)
    {
        fDownPos = fDownPos == 0 ? kTaps - 1 : fDownPos - 1;
        fDown[fDownPos] = fDown[fDownPos + kTaps] = u;
    }

    float fUp[2 * kPhase0];
    float fDown[2 * kTaps];
    int fUpPos, fDownPos;
};

class Oversampler
{
public:
    void setFactor(const unsigned factor) { fFactor = factor; reset(); }
    unsigned factor() const { return fFactor; }

    void reset()
    {
        fStage0.reset();
        fStage1.reset();
    }

    // latency of up + down, in internal (oversampled) samples
    static unsigned latencyInternal(const unsigned factor)
    {
        if (factor == 2)
            return Stage0::kDelayHigh;
        if (factor == 4)
            return 2 * Stage0::kDelayHigh + Stage1::kDelayHigh;   // stage 0 runs at half the rate
        return 0;
    }

    void up(const float x, float* const out)
    {
        if (fFactor == 1)
        {
            out[0] = x;
        }
        else if (fFactor == 2)
        {
            fStage0.up(x, out);
        }
        else
        {
            float t[2];
            fStage0.up(x, t);
            fStage1.up(t[0], out);
            fStage1.up(t[1], out + 2);
        }
    }

    float down(const float* const in)
    {
        if (fFactor == 1)
            return in[0];
        if (fFactor == 2)
            return fStage0.down(in);
        const float t[2] = { fStage1.down(in), fStage1.down(in + 2) };
        return fStage0.down(t);
    }

private:
    typedef HalfbandStage<107> Stage0;   // host rate <-> 2x: cuts at the host Nyquist frequency
    typedef HalfbandStage<47> Stage1;    // 2x <-> 4x: cuts at the host rate
    unsigned fFactor = 1;
    Stage0 fStage0;
    Stage1 fStage1;
};

// ------------------------------------------------------------------------------------------------

// linear up to t (below the ceiling: the knee), then a tanh shoulder that never exceeds the ceiling
static inline float softClip(const float x, const float ceiling, const float t)
{
    const float a = std::fabs(x);
    if (a <= t)
        return x;
    const float r = ceiling - t;
    const float y = t + r * std::tanh((a - t) / r);
    return x < 0.0f ? -y : y;
}

static inline void follow(float& env, const float a, const float envTr)
{
    if (a > env)
        env = a;
    else
        env = a * envTr + env * (1.0f - envTr);
}

// K-weighting of ITU-R BS.1770 (loudness): high shelf (+4 dB above ~1.7 kHz, head) then the
// RLB high-pass (~38 Hz); coefficients for any sample rate (formulas of libebur128), double
// precision.  Gives the coefficients of the standard at 48 kHz (tests/test_dsp.cpp).
struct KWeighting
{
    double b[2][3], a[2][3];   // stage, coefficient (a[s][0] = 1)
    double z[2][2];            // transposed direct form II states

    void setup(const double sr)
    {
        const double pi = 3.14159265358979323846;
        {
            const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
            const double k = std::tan(pi * f0 / sr);
            const double vh = std::pow(10.0, g / 20.0), vb = std::pow(vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            b[0][0] = (vh + vb * k / q + k * k) / a0;
            b[0][1] = 2.0 * (k * k - vh) / a0;
            b[0][2] = (vh - vb * k / q + k * k) / a0;
            a[0][1] = 2.0 * (k * k - 1.0) / a0;
            a[0][2] = (1.0 - k / q + k * k) / a0;
        }
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k = std::tan(pi * f0 / sr);
            const double a0 = 1.0 + k / q + k * k;
            b[1][0] = 1.0;
            b[1][1] = -2.0;
            b[1][2] = 1.0;
            a[1][1] = 2.0 * (k * k - 1.0) / a0;
            a[1][2] = (1.0 - k / q + k * k) / a0;
        }
        a[0][0] = a[1][0] = 1.0;
        reset();
    }

    void reset() { z[0][0] = z[0][1] = z[1][0] = z[1][1] = 0.0; }

    double process(double x)
    {
        for (int s = 0; s < 2; ++s)
        {
            const double y = b[s][0] * x + z[s][0];
            z[s][0] = b[s][1] * x - a[s][1] * y + z[s][1];
            z[s][1] = b[s][2] * x - a[s][2] * y;
            x = y;
        }
        return x;
    }
};

// Peak of the last ~30 ms, whatever the block size: 8 bins of 5 ms, the current one
// and the 6 previous ones are read.  The UI samples it every 25 ms without missing a peak.
struct RecentPeak
{
    static const int kBins = 8;
    static const int kRead = 7;
    float bins[kBins];
    int cur = 0;
    unsigned filled = 0;

    void reset()
    {
        for (int i = 0; i < kBins; ++i)
            bins[i] = 0.0f;
        cur = 0;
        filled = 0;
    }

    void add(const float v, const unsigned frames, const unsigned binLength)
    {
        bins[cur] = std::fmax(bins[cur], v);
        filled += frames;
        if (filled >= binLength)
        {
            cur = (cur + 1) % kBins;
            bins[cur] = 0.0f;
            filled = 0;
        }
    }

    float value() const
    {
        float v = 0.0f;
        for (int i = 0; i < kRead; ++i)
            v = std::fmax(v, bins[(cur - i + kBins) % kBins]);
        return v;
    }
};

// Envelope follower + gain history for one decay / lookahead (both channels).
struct Detector
{
    float env[2] = { 0.0f, 0.0f };
    float envTr = 0.5f;
    unsigned look = 1;
    std::vector<float> gain[2];

    // updates the envelope with a[] (|signal|), stores the gain of sample w and returns in g[]
    // the gain for the sample leaving the signal delay lfix (lookahead 'look')
    void step(const float* const a, const int stereo, const float knee, const unsigned w,
              const unsigned lfix, const unsigned mask, float* const g)
    {
        if (stereo == kStereoLinked)
        {
            follow(env[0], std::fmax(a[0], a[1]), envTr);
            env[1] = env[0];
        }
        else
        {
            follow(env[0], a[0], envTr);
            follow(env[1], a[1], envTr);
        }
        const unsigned r = (w - (lfix - look)) & mask;
        for (int c = 0; c < 2; ++c)
        {
            gain[c][w] = 1.0f / std::fmax(env[c], knee);
            g[c] = gain[c][r];
        }
    }
};

class BrimstonePlugin : public Plugin
{
public:
    BrimstonePlugin()
        : Plugin(kParamCount, kBrimstonePresetCount, kStateCount)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            fParams[i] = kBrimstoneRanges[i].def;
        allocate();
        configure();
    }

protected:
    const char* getLabel() const override { return "BrimstoneMaximizer"; }
    const char* getDescription() const override
    {
        return "Brutal peak maximizer after swh satan_maximiser: every peak above the knee "
               "is pushed to the ceiling.";
    }
    const char* getMaker() const override { return "Brimstone"; }
    const char* getLicense() const override { return "GPL-2.0-or-later"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }

    void initParameter(const uint32_t index, Parameter& p) override
    {
        if (index == kParamBypass)
        {
            p.initDesignation(kParameterDesignationBypass);
            return;
        }

        p.ranges.min = kBrimstoneRanges[index].min;
        p.ranges.max = kBrimstoneRanges[index].max;
        p.ranges.def = kBrimstoneRanges[index].def;
        p.hints = kParameterIsAutomatable;

        switch (index)
        {
        case kParamDecay:   set(p, "Decay (samples)", "decay", "smp"); break;
        case kParamDecayMs: set(p, "Decay (ms)", "decay_ms", "ms"); break;
        case kParamDecayUnit:
            set(p, "Decay unit", "decay_unit", "");
            enumeration(p, 2, "Samples", "Milliseconds", nullptr);
            break;
        case kParamKnee:    set(p, "Knee", "knee", "dB"); break;
        case kParamDrive:   set(p, "Drive", "drive", "dB"); break;
        case kParamCeiling: set(p, "Ceiling", "ceiling", "dB"); break;
        case kParamClip:
            set(p, "Clip", "clip", "");
            enumeration(p, 3, "Off", "Hard", "Soft");
            break;
        case kParamStereo:
            set(p, "Stereo", "stereo", "");
            enumeration(p, 3, "Linked", "Independent", "Mid/Side");
            break;
        case kParamMix:     set(p, "Mix", "mix", "%"); break;
        case kParamGain:    set(p, "Gain", "gain", "dB"); break;
        case kParamOversampling:
            set(p, "Oversampling", "oversampling", "");
            enumeration(p, 3, "1x", "2x", "4x");
            break;
        case kParamClipKnee: set(p, "Clip knee", "clip_knee", "dB"); break;
        case kParamMatch:
            set(p, "Level match", "match", "");
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            break;
        case kParamDelta:
            set(p, "Delta", "delta", "");
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            break;
        case kParamMeterIn:   output(p, "Input level", "meter_in", "dB"); break;
        case kParamMeterGR:   output(p, "Gain reduction", "meter_gr", "dB"); break;
        case kParamMeterOut:  output(p, "Output level", "meter_out", "dB"); break;
        case kParamMeterLoud: output(p, "Loudness change", "meter_loud", "LU"); break;
        case kParamLatency:   output(p, "Latency", "latency", "smp"); break;
        case kParamMeterClip: output(p, "Clipper reduction", "meter_clip", "dB"); break;
        case kParamHistIn:    output(p, "History input", "hist_in", ""); break;
        case kParamHistOut:   output(p, "History output", "hist_out", ""); break;
        case kParamHistMax:   output(p, "History maximizer", "hist_max", ""); break;
        }
    }

    void initProgramName(const uint32_t index, String& name) override
    {
        name = kBrimstonePresets[index].name;
    }

    // states of the interface (A/B comparison), saved with the project; nothing for the DSP
    void initState(const uint32_t index, State& state) override
    {
        if (index >= static_cast<uint32_t>(kStateCount))
            return;
        state.key = kStateKeys[index];
        state.defaultValue = "";
        state.label = index == kStateAB ? "A/B comparison" : "History display";
        state.hints = kStateIsOnlyForUI;
    }

    void setState(const char* const key, const char* const value) override
    {
        for (int i = 0; i < kStateCount; ++i)
            if (std::strcmp(key, kStateKeys[i]) == 0)
                fStates[i] = value;
    }

    String getState(const char* const key) const override
    {
        for (int i = 0; i < kStateCount; ++i)
            if (std::strcmp(key, kStateKeys[i]) == 0)
                return fStates[i];
        return String();
    }

    void loadProgram(const uint32_t index) override
    {
        if (index >= static_cast<uint32_t>(kBrimstonePresetCount))
            return;
        for (int i = 0; i < kPresetParamCount; ++i)
            fParams[i] = kBrimstonePresets[index].values[i];
    }

    float getParameterValue(const uint32_t index) const override
    {
        return fParams[index];
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index < static_cast<uint32_t>(kParamFirstOutput))
            fParams[index] = value;
    }

    void activate() override
    {
        allocate();
        configure();
    }

    void sampleRateChanged(double) override
    {
        allocate();
        configure();
    }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        const ScopedNoDenormals noDenormals;
        if (configKey() != fConfigKey)
            configure();

        const unsigned os = fOs.factor();
        const float osf = static_cast<float>(os);
        const float sr = static_cast<float>(getSampleRate());

        // decay in internal samples, lookahead and gain tap
        float dInt = fParams[kParamDecayUnit] > 0.5f
                   ? fParams[kParamDecayMs] * 1e-3f * sr * osf
                   : fParams[kParamDecay] * osf;
        dInt = std::fmax(dInt, 2.0f);
        const float envTr = 1.0f / dInt;
        const unsigned look = static_cast<unsigned>(std::lround(dInt * 0.5f));
        const float fadeStep = static_cast<float>(1.0 / (0.005 * getSampleRate() * os));

        const int stereo = static_cast<int>(std::lround(fParams[kParamStereo]));
        const int clip = static_cast<int>(std::lround(fParams[kParamClip]));
        const bool match = fParams[kParamMatch] > 0.5f;

        const float kneeT = dbToGain(fParams[kParamKnee]);
        const float driveT = dbToGain(fParams[kParamDrive]);
        const float ceilT = dbToGain(fParams[kParamCeiling]);
        const float mixT = fParams[kParamMix] * 0.01f;
        const float outT = dbToGain(fParams[kParamGain]);
        const float softT = dbToGain(-fParams[kParamClipKnee]);   // knee of the soft clip / ceiling
        const float bypassT = fParams[kParamBypass] > 0.5f ? 1.0f : 0.0f;
        const float deltaT = fParams[kParamDelta] > 0.5f ? 1.0f : 0.0f;
        const bool deltaOn = deltaT > 0.0f || fDelta > 0.0f;
        if (deltaT > 0.0f && fDelta == 0.0f)
        {
            // the reference filter restarts (its state is stale), under the crossfade
            fOsRef[0].reset();
            fOsRef[1].reset();
        }
        const float sm = fSmoothInt, smBase = fSmoothBase;

        const float* const inL = inputs[0];
        const float* const inR = inputs[1];
        float* const outL = outputs[0];
        float* const outR = outputs[1];

        float peakIn = 0.0f, peakOut = 0.0f, peakMax = 0.0f, minRel = 1.0f, minClip = 1.0f;
        double sumIn = 0.0, sumOut = 0.0;

        const float compStart = fComp;
        const float compEnd = fCompTarget;

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float x[2] = { inL[i], inR[i] };
            float up[2][4];
            fOsCh[0].up(x[0], up[0]);
            fOsCh[1].up(x[1], up[1]);

            float wetInt[2][4], refInt[2][4];
            for (unsigned o = 0; o < os; ++o)
            {
                fKnee += (kneeT - fKnee) * sm;
                fDrive += (driveT - fDrive) * sm;
                fCeil += (ceilT - fCeil) * sm;
                fMix += (mixT - fMix) * sm;
                fOut += (outT - fOut) * sm;
                fSoft += (softT - fSoft) * sm;

                Detector& da = fDet[fActive];
                Detector& db = fDet[1 - fActive];
                if (!fFading && look != da.look)
                {
                    // start the new detector from the state of the active one
                    db.env[0] = da.env[0];
                    db.env[1] = da.env[1];
                    db.gain[0] = da.gain[0];   // same size: no allocation
                    db.gain[1] = da.gain[1];
                    db.look = look;
                    fFading = true;
                    fFade = 0.0f;
                }
                if (fFading)
                    db.envTr = envTr;   // the old detector keeps its decay during the fade
                else
                    da.envTr = envTr;

                float s[2] = { up[0][o] * fDrive, up[1][o] * fDrive };
                if (stereo == kStereoMidSide)
                {
                    const float m = (s[0] + s[1]) * 0.5f, d = (s[0] - s[1]) * 0.5f;
                    s[0] = m;
                    s[1] = d;
                }

                const float a[2] = { std::fabs(s[0]), std::fabs(s[1]) };
                const unsigned w = fPos;
                const unsigned rSig = (w - fLfix) & fMask;
                float g[2];
                da.step(a, stereo, fKnee, w, fLfix, fMask, g);
                if (fFading)
                {
                    float gb[2];
                    db.step(a, stereo, fKnee, w, fLfix, fMask, gb);
                    g[0] += (gb[0] - g[0]) * fFade;
                    g[1] += (gb[1] - g[1]) * fFade;
                    fFade += fadeStep;
                    if (fFade >= 1.0f)
                    {
                        fActive = 1 - fActive;
                        fFading = false;
                    }
                }

                float wet[2];
                for (int c = 0; c < 2; ++c)
                {
                    fSig[c][w] = s[c];
                    fDry[c][w] = up[c][o];
                    wet[c] = fSig[c][rSig] * g[c];
                    if (fMix > 1e-4f)   // at mix 0 % the maximizer is out of the signal
                        minRel = std::fmin(minRel, g[c] * fKnee);
                }
                fPos = (w + 1) & fMask;

                if (stereo == kStereoMidSide)
                {
                    const float m = wet[0], d = wet[1];
                    wet[0] = m + d;
                    wet[1] = m - d;
                }

                for (int c = 0; c < 2; ++c)
                {
                    const float dry = fDry[c][rSig];
                    const float y = (dry + (wet[c] * fCeil - dry) * fMix) * fOut;
                    peakMax = std::fmax(peakMax, std::fabs(y));   // into the clipper (internal rate)
                    float yc = y;
                    if (clip == kClipHard)
                        yc = std::fmin(std::fmax(y, -fCeil), fCeil);
                    else if (clip == kClipSoft)
                        yc = softClip(y, fCeil, fSoft * fCeil);
                    if (yc != y)
                        minClip = std::fmin(minClip, yc / y);
                    wetInt[c][o] = yc;
                    // linear chain for delta: maximizer gain at its makeup, no clip
                    if (deltaOn)
                        refInt[c][o] = dry * (1.0f + (fDrive * fCeil / fKnee - 1.0f) * fMix) * fOut;
                }
            }

            float p[2] = { fOsCh[0].down(wetInt[0]), fOsCh[1].down(wetInt[1]) };
            if (clip != kClipOff)
            {
                // the downsampling filter can ring slightly above the ceiling
                for (int c = 0; c < 2; ++c)
                {
                    const float pc = std::fmin(std::fmax(p[c], -fCeil), fCeil);
                    if (pc != p[c])
                        minClip = std::fmin(minClip, pc / p[c]);
                    p[c] = pc;
                }
            }

            // loudness: K-weighted powers (BS.1770, channel weights 1)
            for (int c = 0; c < 2; ++c)
            {
                const double ki = fKIn[c].process(x[c]), ko = fKOut[c].process(p[c]);
                sumIn += ki * ki;
                sumOut += ko * ko;
            }

            // input delayed by the latency (bypass, delta)
            const unsigned bw = fBasePos;
            const unsigned br = (bw - fLatency) & fBaseMask;
            fBase[0][bw] = x[0];
            fBase[1][bw] = x[1];
            fBasePos = (bw + 1) & fBaseMask;

            // delta: difference with the linear chain (maximizer gain held at 1 / knee, no clip)
            fDelta += (deltaT - fDelta) * smBase;
            if (deltaT == 0.0f && fDelta < 1e-6f)
                fDelta = 0.0f;
            else if (deltaT == 1.0f && fDelta > 1.0f - 1e-6f)
                fDelta = 1.0f;
            float d[2] = { 0.0f, 0.0f };
            if (deltaOn)
            {
                d[0] = p[0] - fOsRef[0].down(refInt[0]);
                d[1] = p[1] - fOsRef[1].down(refInt[1]);
            }

            if (match)
            {
                const float comp = compStart + (compEnd - compStart) * (static_cast<float>(i + 1) / frames);
                p[0] *= comp;
                p[1] *= comp;
            }
            if (fDelta > 0.0f)
            {
                p[0] += (d[0] - p[0]) * fDelta;
                p[1] += (d[1] - p[1]) * fDelta;
            }

            // bypass: input delayed by the latency, crossfaded
            fBypass += (bypassT - fBypass) * smBase;
            if (bypassT == 0.0f && fBypass < 1e-6f)
                fBypass = 0.0f;
            else if (bypassT == 1.0f && fBypass > 1.0f - 1e-6f)
                fBypass = 1.0f;

            float y[2];
            for (int c = 0; c < 2; ++c)
                y[c] = fBypass == 0.0f ? p[c]
                     : fBypass == 1.0f ? fBase[c][br]
                     : p[c] + (fBase[c][br] - p[c]) * fBypass;

            outL[i] = y[0];
            outR[i] = y[1];

            peakIn = std::fmax(peakIn, std::fmax(std::fabs(x[0]), std::fabs(x[1])));
            peakOut = std::fmax(peakOut, std::fmax(std::fabs(y[0]), std::fabs(y[1])));
        }

        fComp = compEnd;

        // meters: peaks with ~100 ms release (the UI adds its own ballistics)
        const float fr = static_cast<float>(frames);
        const float rel = std::exp(-fr / (0.1f * sr));
        fMeterIn = std::fmax(peakIn, fMeterIn * rel);
        fMeterOut = std::fmax(peakOut, fMeterOut * rel);
        fMeterRel = std::fmin(minRel, 1.0f - (1.0f - fMeterRel) * rel);
        fMeterClip = std::fmin(minClip, 1.0f - (1.0f - fMeterClip) * rel);

        // history: peaks of the last ~30 ms
        const unsigned bin = static_cast<unsigned>(0.005f * sr) + 1;
        fRecentIn.add(peakIn, frames, bin);
        fRecentOut.add(peakOut, frames, bin);
        fRecentMax.add(peakMax, frames, bin);

        // loudness (K-weighted mean square over ~1.5 s, BS.1770) for the level match
        if (frames > 0)
        {
            const double e = std::exp(-static_cast<double>(frames) / (1.5 * sr));
            fPowIn = fPowIn * e + (1.0 - e) * sumIn / (2.0 * frames);
            fPowOut = fPowOut * e + (1.0 - e) * sumOut / (2.0 * frames);
        }
        if (fPowIn > 1e-9 && fPowOut > 1e-9)
        {
            const float diff = static_cast<float>(10.0 * std::log10(fPowOut / fPowIn));
            fParams[kParamMeterLoud] = std::fmin(std::fmax(diff, -24.0f), 24.0f);
            fCompTarget = dbToGain(-fParams[kParamMeterLoud]);
        }
        if (!match)
            fComp = fCompTarget;

        fParams[kParamMeterIn] = gainToDb(fMeterIn, -60.0f);
        fParams[kParamMeterOut] = gainToDb(fMeterOut, -60.0f);
        fParams[kParamMeterGR] = gainToDb(fMeterRel, -40.0f);
        fParams[kParamLatency] = static_cast<float>(fLatency);
        fParams[kParamMeterClip] = gainToDb(fMeterClip, -40.0f);
        fParams[kParamHistIn] = std::fmin(fRecentIn.value(), 2.0f);
        fParams[kParamHistOut] = std::fmin(fRecentOut.value(), 2.0f);
        fParams[kParamHistMax] = std::fmin(fRecentMax.value(), 4.0f);
    }

private:
    static void set(Parameter& p, const char* name, const char* symbol, const char* unit)
    {
        p.name = name;
        p.symbol = symbol;
        p.unit = unit;
    }

    static void output(Parameter& p, const char* name, const char* symbol, const char* unit)
    {
        set(p, name, symbol, unit);
        p.hints = kParameterIsOutput;
    }

    static void enumeration(Parameter& p, const int count, const char* a, const char* b, const char* c)
    {
        p.hints |= kParameterIsInteger;
        p.enumValues.count = count;
        p.enumValues.restrictedMode = true;
        ParameterEnumerationValue* const v = new ParameterEnumerationValue[count];
        const char* labels[3] = { a, b, c };
        for (int i = 0; i < count; ++i)
        {
            v[i].label = labels[i];
            v[i].value = static_cast<float>(i);
        }
        p.enumValues.values = v;
    }

    static unsigned osFactor(const float v)
    {
        const long i = std::lround(v);
        return i >= 2 ? 4u : i == 1 ? 2u : 1u;
    }

    // largest lookahead, in internal samples, for a decay unit and an oversampling factor
    unsigned maxLookahead(const bool ms, const unsigned os) const
    {
        if (ms)
            return static_cast<unsigned>(std::lround(kBrimstoneRanges[kParamDecayMs].max * 1e-3
                                                     * getSampleRate() * os * 0.5));
        return static_cast<unsigned>(std::lround(kBrimstoneRanges[kParamDecay].max * os * 0.5f));
    }

    int configKey() const
    {
        return static_cast<int>(osFactor(fParams[kParamOversampling])) * 2 + (fParams[kParamDecayUnit] > 0.5f ? 1 : 0);
    }

    // buffers for the worst case at this sample rate (no allocation in run)
    void allocate()
    {
        unsigned maxLfix = 0, maxLatency = 0;
        for (unsigned os = 1; os <= 4; os *= 2)
        {
            const unsigned l = std::max(maxLookahead(false, os), maxLookahead(true, os)) + os;
            const unsigned t = Oversampler::latencyInternal(os) + l;
            maxLfix = std::max(maxLfix, l);
            maxLatency = std::max(maxLatency, (t + os - 1) / os);
        }
        const unsigned n = nextPow2(maxLfix + 4);
        const unsigned nb = nextPow2(maxLatency + 4);
        for (int c = 0; c < 2; ++c)
        {
            fSig[c].assign(n, 0.0f);
            fDet[0].gain[c].assign(n, 0.0f);
            fDet[1].gain[c].assign(n, 0.0f);
            fDry[c].assign(n, 0.0f);
            fBase[c].assign(nb, 0.0f);
        }
        fMask = n - 1;
        fBaseMask = nb - 1;
    }

    // latency and delays for the current oversampling and decay unit; resets the state
    void configure()
    {
        const unsigned os = osFactor(fParams[kParamOversampling]);
        const bool ms = fParams[kParamDecayUnit] > 0.5f;
        const unsigned lmax = maxLookahead(ms, os);
        const unsigned s = Oversampler::latencyInternal(os);
        const unsigned pad = (os - (s + lmax) % os) % os;
        fLfix = lmax + pad;
        fLatency = (s + fLfix) / os;
        fConfigKey = configKey();

        fOs.setFactor(os);
        fOsCh[0].setFactor(os);
        fOsCh[1].setFactor(os);
        fOsRef[0].setFactor(os);
        fOsRef[1].setFactor(os);
        setLatency(fLatency);
        fParams[kParamLatency] = static_cast<float>(fLatency);   // shown before the first run()
        resetState();
    }

    void resetState()
    {
        const unsigned os = fOs.factor();
        const double sr = getSampleRate();
        for (int c = 0; c < 2; ++c)
        {
            std::fill(fSig[c].begin(), fSig[c].end(), 0.0f);
            std::fill(fDet[0].gain[c].begin(), fDet[0].gain[c].end(), 0.0f);
            std::fill(fDet[1].gain[c].begin(), fDet[1].gain[c].end(), 0.0f);
            std::fill(fDry[c].begin(), fDry[c].end(), 0.0f);
            std::fill(fBase[c].begin(), fBase[c].end(), 0.0f);
        }
        fOsCh[0].reset();
        fOsCh[1].reset();
        fOsRef[0].reset();
        fOsRef[1].reset();
        fPos = fBasePos = 0;

        float dInt = fParams[kParamDecayUnit] > 0.5f
                   ? fParams[kParamDecayMs] * 1e-3f * static_cast<float>(sr) * os
                   : fParams[kParamDecay] * os;
        dInt = std::fmax(dInt, 2.0f);
        for (int d = 0; d < 2; ++d)
        {
            fDet[d].env[0] = fDet[d].env[1] = 0.0f;
            fDet[d].envTr = 1.0f / dInt;
            fDet[d].look = static_cast<unsigned>(std::lround(dInt * 0.5f));
        }
        fActive = 0;
        fFading = false;
        fFade = 0.0f;

        fKnee = dbToGain(fParams[kParamKnee]);
        fDrive = dbToGain(fParams[kParamDrive]);
        fCeil = dbToGain(fParams[kParamCeiling]);
        fMix = fParams[kParamMix] * 0.01f;
        fOut = dbToGain(fParams[kParamGain]);
        fSoft = dbToGain(-fParams[kParamClipKnee]);
        fBypass = fParams[kParamBypass] > 0.5f ? 1.0f : 0.0f;
        fDelta = fParams[kParamDelta] > 0.5f ? 1.0f : 0.0f;
        fSmoothInt = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * sr * os)));
        fSmoothBase = static_cast<float>(1.0 - std::exp(-1.0 / (0.01 * sr)));
        fMeterIn = fMeterOut = 0.0f;
        fMeterRel = fMeterClip = 1.0f;
        fRecentIn.reset();
        fRecentOut.reset();
        fRecentMax.reset();
        fPowIn = fPowOut = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            fKIn[c].setup(sr);
            fKOut[c].setup(sr);
        }
        fComp = fCompTarget = 1.0f;
    }

    float fParams[kParamCount];
    String fStates[kStateCount];   // strings of the interface, only kept for the host

    Oversampler fOs;          // only holds the factor
    Oversampler fOsCh[2];
    Oversampler fOsRef[2];    // downsampling of the linear chain (delta)
    std::vector<float> fSig[2], fDry[2], fBase[2];
    Detector fDet[2];
    int fActive = 0;
    bool fFading = false;
    float fFade = 0.0f;
    unsigned fMask = 0, fBaseMask = 0, fPos = 0, fBasePos = 0;
    unsigned fLfix = 0, fLatency = 0;
    int fConfigKey = -1;
    float fKnee = 1.0f, fDrive = 1.0f, fCeil = 1.0f, fMix = 1.0f, fOut = 1.0f, fSoft = 0.6f;
    float fBypass = 0.0f, fDelta = 0.0f;
    float fSmoothInt = 0.0f, fSmoothBase = 0.0f;
    float fMeterIn = 0.0f, fMeterOut = 0.0f, fMeterRel = 1.0f, fMeterClip = 1.0f;
    RecentPeak fRecentIn, fRecentOut, fRecentMax;
    double fPowIn = 0.0, fPowOut = 0.0;
    KWeighting fKIn[2], fKOut[2];
    float fComp = 1.0f, fCompTarget = 1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrimstonePlugin)
};

Plugin* createPlugin()
{
    return new BrimstonePlugin();
}

END_NAMESPACE_DISTRHO
