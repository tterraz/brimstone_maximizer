/*
 * Brimstone Maximizer - DSP tests (native build, see ../build_windows.sh).
 *
 * Copyright (C) 2026 Brimstone authors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * 1. "Original" settings: output == direct transcription of the LADSPA satan_maximiser
 *    algorithm, shifted by the fixed latency (32 combinations, odd block sizes).
 * 2. Ceiling: with clip hard/soft, |out| <= ceiling for every oversampling factor.
 * 3. Bypass: output == input delayed by the reported latency, exactly.
 * 4. Latency of 2x/4x: with mix 0, output ~ input delayed by the reported latency.
 * 5. Decay sweep: no step in the gain larger than with a fixed decay (no click).
 * 6. Presets: every preset gives a finite output under the ceiling.
 * 7. Clipper preset: output == clip(input x gain) at 1x.
 * 8. Meters: maximizer and clipper reductions, history peaks.
 * 9. Denormals: FTZ / DAZ are switched on in run() only, the FPU state of the caller is
 *    restored (the gain in silence is measured by tests/bench_cpu.cpp).
 */
#include "src/DistrhoPluginInternal.hpp"
#include "src/DistrhoPlugin.cpp"
#include "src/DistrhoUtils.cpp"
#include "../plugin/BrimstonePlugin.cpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

USE_NAMESPACE_DISTRHO

static int gFailures = 0;

static void check(const bool ok, const char* what)
{
    gFailures += !ok;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
}

// Original algorithm, one channel; det = detector input (|x| or linked max).
static void reference(const std::vector<float>& x, const std::vector<float>& det, float decay, float kneeDb,
                      std::vector<float>& y)
{
    const int delay = (int)lroundf(decay * 0.5f);
    const float envTr = 1.0f / decay, knee = powf(10.0f, kneeDb * 0.05f);
    float env = 0.0f, buf[16] = {0};
    unsigned pos = 0;
    y.resize(x.size());
    for (size_t n = 0; n < x.size(); ++n)
    {
        const float a = det[n];
        env = a > env ? a : a * envTr + env * (1.0f - envTr);
        const float sc = env <= knee ? 1.0f / knee : 1.0f / env;
        buf[pos] = x[n];
        y[n] = buf[(pos - delay) & 15] * sc;
        pos = (pos + 1) & 15;
    }
}

struct Settings {
    float values[kPresetParamCount];
    float bypass = 0.0f;
};

static Settings originalSettings(float decay, float knee, int stereo)
{
    Settings s;
    for (int i = 0; i < kPresetParamCount; ++i)
        s.values[i] = kBrimstonePresets[1].values[i];   // "Original (swh)"
    s.values[kParamDecay] = decay;
    s.values[kParamKnee] = knee;
    s.values[kParamStereo] = (float)stereo;
    return s;
}

// Runs the plugin with odd block sizes; returns the latency.
static uint32_t process(const Settings& s, const std::vector<float>& L, const std::vector<float>& R,
                        std::vector<float>& oL, std::vector<float>& oR,
                        const std::vector<float>* decaySweep = nullptr, float* outs = nullptr)
{
    PluginExporter p(nullptr, nullptr, nullptr, nullptr);
    for (int i = 0; i < kPresetParamCount; ++i)
        p.setParameterValue(i, s.values[i]);
    p.setParameterValue(kParamBypass, s.bypass);
    p.activate();

    const size_t N = L.size();
    oL.assign(N, 0.0f);
    oR.assign(N, 0.0f);
    size_t n = 0, k = 0;
    const uint32_t sizes[] = { 1, 7, 64, 333, 512 };
    while (n < N)
    {
        const uint32_t f = (uint32_t)std::min<size_t>(sizes[k++ % 5], N - n);
        if (decaySweep)
            p.setParameterValue(kParamDecay, (*decaySweep)[n]);
        const float* in[2] = { &L[n], &R[n] };
        float* out[2] = { &oL[n], &oR[n] };
        p.run(in, out, f);
        n += f;
    }
    if (outs)
        for (int i = kParamFirstOutput; i < kParamCount; ++i)
            outs[i] = p.getParameterValue(i);
    return p.getLatency();
}

int main()
{
    d_nextBufferSize = 512;
    d_nextSampleRate = 44100.0;
    const size_t N = 20000;
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::vector<float> L(N), R(N), oL, oR;
    for (size_t n = 0; n < N; ++n)
    {
        const float burst = (n / 2000) % 2 ? 1.6f : 0.08f;
        L[n] = burst * (0.6f * sinf(0.031f * n) + 0.4f * u(rng));
        R[n] = burst * (0.5f * sinf(0.017f * n + 1.0f) + 0.5f * u(rng));
    }

    // 1. original algorithm --------------------------------------------------------------------
    std::printf("1. original algorithm (stereo independent = original on each channel; linked)\n");
    {
        const float decays[] = { 2.0f, 7.0f, 16.4f, 30.0f };
        const float knees[] = { -60.0f, -20.0f, -6.0f, 0.0f };
        float worst = 0.0f;
        bool latencyOk = true;
        for (int stereo = 0; stereo < 2; ++stereo)
        for (float decay : decays)
        for (float knee : knees)
        {
            const uint32_t lat = process(originalSettings(decay, knee, stereo), L, R, oL, oR);
            latencyOk = latencyOk && lat == 15;
            const bool link = stereo == kStereoLinked;
            std::vector<float> dL(N), dR(N), yL, yR;
            for (size_t i = 0; i < N; ++i)
            {
                dL[i] = link ? std::max(fabsf(L[i]), fabsf(R[i])) : fabsf(L[i]);
                dR[i] = link ? dL[i] : fabsf(R[i]);
            }
            reference(L, dL, decay, knee, yL);
            reference(R, dR, decay, knee, yR);
            const size_t shift = 15 - (size_t)lroundf(decay * 0.5f);
            for (size_t i = 15; i < N; ++i)
            {
                const float scale = std::max(1.0f, std::max(fabsf(yL[i - shift]), fabsf(yR[i - shift])));
                worst = std::max(worst, std::max(fabsf(oL[i] - yL[i - shift]), fabsf(oR[i] - yR[i - shift])) / scale);
            }
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "32 cases, latency 15, max relative error %.2e", worst);
        check(latencyOk && worst < 1e-5f, buf);
    }

    // 2. ceiling ------------------------------------------------------------------------------
    std::printf("2. ceiling\n");
    for (int clip = 1; clip <= 2; ++clip)
    for (int os = 0; os <= 2; ++os)
    {
        Settings s = originalSettings(2.0f, -40.0f, kStereoMidSide);
        s.values[kParamClip] = (float)clip;
        s.values[kParamOversampling] = (float)os;
        s.values[kParamCeiling] = -1.0f;
        s.values[kParamDrive] = 12.0f;
        s.values[kParamGain] = 6.0f;
        process(s, L, R, oL, oR);
        float peak = 0.0f;
        for (size_t i = 0; i < N; ++i)
            peak = std::max(peak, std::max(fabsf(oL[i]), fabsf(oR[i])));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "clip %s, %dx: peak %.4f dBFS (ceiling -1)",
                      clip == 1 ? "hard" : "soft", 1 << os, 20.0f * log10f(peak));
        check(peak <= dbToGain(-1.0f) * 1.000001f, buf);
    }

    // 3. bypass ------------------------------------------------------------------------------
    std::printf("3. bypass\n");
    for (int os = 0; os <= 2; ++os)
    {
        Settings s = originalSettings(30.0f, -20.0f, 0);
        s.values[kParamOversampling] = (float)os;
        s.values[kParamDecayUnit] = 1.0f;
        s.bypass = 1.0f;
        const uint32_t lat = process(s, L, R, oL, oR);
        float err = 0.0f;
        for (size_t i = lat; i < N; ++i)
            err = std::max(err, std::max(fabsf(oL[i] - L[i - lat]), fabsf(oR[i] - R[i - lat])));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%dx, unit ms: latency %u, max error %.1e", 1 << os, lat, err);
        check(err == 0.0f, buf);
    }

    // 4. latency of the oversampling (mix 0: only the filters and the delays) ------------------
    std::printf("4. latency with oversampling (mix 0, 1 kHz sine)\n");
    {
        std::vector<float> S(N);
        for (size_t n = 0; n < N; ++n)
            S[n] = 0.5f * sinf(2.0f * (float)M_PI * 1000.0f * n / 44100.0f);
        for (int os = 0; os <= 2; ++os)
        for (int unit = 0; unit <= 1; ++unit)
        {
            Settings s = originalSettings(30.0f, -20.0f, 0);
            s.values[kParamOversampling] = (float)os;
            s.values[kParamDecayUnit] = (float)unit;
            s.values[kParamMix] = 0.0f;
            const uint32_t lat = process(s, S, S, oL, oR);
            float err = 0.0f;
            for (size_t i = 1000; i < N; ++i)
                err = std::max(err, fabsf(oL[i] - S[i - lat]));
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%dx, unit %s: latency %u, max error %.1e",
                          1 << os, unit ? "ms" : "samples", lat, err);
            check(err < 1e-3f, buf);
        }
    }

    // 5. decay sweep without click ------------------------------------------------------------
    std::printf("5. decay changes (samples 30 <-> 2 every 1000 samples, sine 0 dBFS + knee -30)\n");
    {
        std::vector<float> S(N), sweep(N), fixed(N, 30.0f);
        for (size_t n = 0; n < N; ++n)
        {
            S[n] = 0.9f * sinf(2.0f * (float)M_PI * 220.0f * n / 44100.0f);
            sweep[n] = (n / 1000) % 2 ? 2.0f : 30.0f;
        }
        Settings s = originalSettings(30.0f, -30.0f, 0);
        std::vector<float> aL, aR;
        process(s, S, S, aL, aR, &sweep);
        process(s, S, S, oL, oR, &fixed);
        float jumpSweep = 0.0f, jumpFixed = 0.0f;
        for (size_t i = 100; i < N; ++i)
        {
            jumpSweep = std::max(jumpSweep, fabsf(aL[i] - aL[i - 1]));
            jumpFixed = std::max(jumpFixed, fabsf(oL[i] - oL[i - 1]));
        }
        std::vector<float> two(N, 2.0f);
        process(s, S, S, oL, oR, &two);
        float jumpTwo = 0.0f;
        for (size_t i = 100; i < N; ++i)
            jumpTwo = std::max(jumpTwo, fabsf(oL[i] - oL[i - 1]));
        char buf[160];
        std::snprintf(buf, sizeof(buf), "largest step %.4f, no larger than with a fixed decay (30: %.4f, 2: %.4f)",
                      jumpSweep, jumpFixed, jumpTwo);
        check(jumpSweep <= std::max(jumpFixed, jumpTwo) * 1.05f, buf);
    }

    // 6. presets ------------------------------------------------------------------------------
    std::printf("6. presets\n");
    for (int k = 0; k < kBrimstonePresetCount; ++k)
    {
        Settings s;
        for (int i = 0; i < kPresetParamCount; ++i)
            s.values[i] = kBrimstonePresets[k].values[i];
        const uint32_t lat = process(s, L, R, oL, oR);
        bool finite = true;
        float peak = 0.0f;
        for (size_t i = 0; i < N; ++i)
        {
            finite = finite && std::isfinite(oL[i]) && std::isfinite(oR[i]);
            peak = std::max(peak, std::max(fabsf(oL[i]), fabsf(oR[i])));
        }
        const bool clipped = s.values[kParamClip] > 0.5f;
        const float ceil = dbToGain(s.values[kParamCeiling]);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%-16s latency %3u, peak %+.2f dBFS%s", kBrimstonePresets[k].name, lat,
                      20.0f * log10f(peak), clipped ? "" : " (clip off)");
        check(finite && (!clipped || peak <= ceil * 1.000001f), buf);
    }

    // 7. clipper preset = pure clipper (checked at 1x, where it must be exact) -----------------
    std::printf("7. clipper preset (mix 0 %%: maximizer bypassed)\n");
    {
        int k = 0;
        while (std::strcmp(kBrimstonePresets[k].name, "Clipper") != 0)
            ++k;
        Settings s;
        for (int i = 0; i < kPresetParamCount; ++i)
            s.values[i] = kBrimstonePresets[k].values[i];
        s.values[kParamOversampling] = 0.0f;
        const uint32_t lat = process(s, L, R, oL, oR);
        const float g = dbToGain(s.values[kParamGain]), c = dbToGain(s.values[kParamCeiling]);
        float err = 0.0f;
        size_t clipped = 0;
        for (size_t i = lat; i < N; ++i)
        {
            const float y = std::fmin(std::fmax(L[i - lat] * g, -c), c);
            err = std::max(err, fabsf(oL[i] - y));
            clipped += fabsf(L[i - lat] * g) > c;
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "1x: out == clip(in x %+.0f dB, %.1f dB), max error %.1e, %zu samples clipped",
                      s.values[kParamGain], s.values[kParamCeiling], err, clipped);
        check(err < 1e-6f && clipped > 0, buf);
    }

    // 8. meters --------------------------------------------------------------------------------
    std::printf("8. meters (end of a 0.5 sine, 220 Hz)\n");
    {
        std::vector<float> S(N);
        for (size_t n = 0; n < N; ++n)
            S[n] = 0.5f * sinf(2.0f * (float)M_PI * 220.0f * n / 44100.0f);
        float m[kParamCount];
        int k = 0;
        while (std::strcmp(kBrimstonePresets[k].name, "Clipper") != 0)
            ++k;
        Settings clipper;
        for (int i = 0; i < kPresetParamCount; ++i)
            clipper.values[i] = kBrimstonePresets[k].values[i];
        clipper.values[kParamGain] = 12.0f;   // 0.5 x 4 = 2 > ceiling: clipped by ~6.3 dB
        process(clipper, S, S, oL, oR, nullptr, m);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "clipper, gain +12: maximizer GR %.1f, clip %.1f dB (history %.1f), in %.3f, out %.3f",
                      m[kParamMeterGR], m[kParamMeterClip], m[kParamHistClip], m[kParamHistIn], m[kParamHistOut]);
        check(m[kParamMeterGR] == 0.0f && fabsf(m[kParamMeterClip] + 6.32f) < 0.1f
              && fabsf(m[kParamHistClip] - m[kParamMeterClip]) < 0.1f
              && fabsf(m[kParamHistIn] - 0.5f) < 0.01f && fabsf(m[kParamHistOut] - dbToGain(-0.3f)) < 0.01f, buf);

        Settings def;
        for (int i = 0; i < kPresetParamCount; ++i)
            def.values[i] = kBrimstonePresets[0].values[i];
        def.values[kParamClip] = kClipOff;
        process(def, S, S, oL, oR, nullptr, m);
        std::snprintf(buf, sizeof(buf), "default, clip off (knee -12): maximizer GR %.1f dB (history %.1f), clip %.1f dB",
                      m[kParamMeterGR], m[kParamHistGR], m[kParamMeterClip]);
        // 0.5 = -6 dBFS, 6 dB above the knee: reduction 6 dB below the makeup gain
        check(fabsf(m[kParamMeterGR] + 6.02f) < 0.1f && fabsf(m[kParamHistGR] + 6.02f) < 0.1f
              && m[kParamMeterClip] == 0.0f, buf);
    }

    // 9. denormals ------------------------------------------------------------------------------
    std::printf("9. denormals (FTZ / DAZ during run only)\n");
    {
#if defined(__SSE__) || defined(_M_X64)
        const unsigned saved = _mm_getcsr();
        bool restored = true;
        for (const unsigned csr : { saved & ~0x8040u, saved | 0x8040u })
        {
            _mm_setcsr(csr);
            std::vector<float> S(30000, 0.0f);
            for (size_t n = 0; n < 4410; ++n)
                S[n] = 0.5f * sinf(0.0314f * n);
            for (const int os : { 0, 2 })
            {
                Settings def;
                for (int i = 0; i < kPresetParamCount; ++i)
                    def.values[i] = kBrimstonePresets[0].values[i];
                def.values[kParamOversampling] = (float)os;
                process(def, S, S, oL, oR);
                restored = restored && _mm_getcsr() == csr;
            }
        }
        _mm_setcsr(saved);
        check(restored, "FPU state of the caller restored (FTZ / DAZ off and on, 1x and 4x)");
#else
        check(true, "not an SSE processor: nothing to test");
#endif
    }

    std::printf("%s\n", gFailures ? "FAILED" : "all tests passed");
    return gFailures != 0;
}
