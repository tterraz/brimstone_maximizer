/*
 * Brimstone Maximizer - CPU bench of the DSP (run()), native build:
 *   g++ -O3 -ffast-math -mno-daz-ftz -std=gnu++11 -Iplugin -Idpf/distrho -Idpf/dgl \
 *       -DDISTRHO_PLUGIN_TARGET_EXPORT -DDISTRHO_IS_STANDALONE=0 tests/bench_cpu.cpp -o build/bench_cpu
 *   taskset -c 2 build/bench_cpu
 * Same optimisation options as the plugin (DPF: -O3 -ffast-math), but -mno-daz-ftz: an
 * executable built with -ffast-math would otherwise switch FTZ / DAZ on at start-up, which a
 * plugin does not do (the state of the FPU is the host's).
 *
 * Stereo, 44.1 kHz, blocks of 512, preset Default (Soft clip) at 1x, 2x, 4x, three signals:
 *   music    noise bursts with a sine, 10 s;
 *   silence  10 s of silence after 1 s of music (only the silence is timed): the envelope
 *            decays into denormal numbers;
 *   bypass   music, Bypass on (the processing still runs, for the crossfade).
 * Each case is timed with the FPU of the caller (FTZ / DAZ off, the worst host) and with
 * FTZ / DAZ on.  Best of 5 runs.  Result: ns per stereo frame and load of one instance in %
 * of one core in real time.
 *
 * Copyright (C) 2026 Brimstone authors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "src/DistrhoPluginInternal.hpp"
#include "src/DistrhoPlugin.cpp"
#include "src/DistrhoUtils.cpp"
#include "../plugin/BrimstonePlugin.cpp"

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#include <xmmintrin.h>

USE_NAMESPACE_DISTRHO

static const double kRate = 44100.0;
static const uint32_t kBlock = 512;

enum { kMusic = 0, kSilence, kBypassed, kNumSignals };
static const char* const kSignalNames[kNumSignals] = { "music", "silence", "bypass" };

static void fillMusic(std::vector<float>& L, std::vector<float>& R, const size_t n0, const size_t n1)
{
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (size_t n = n0; n < n1; ++n)
    {
        const float burst = (n / 2000) % 2 ? 1.6f : 0.08f;
        L[n] = burst * (0.6f * std::sin(0.031f * n) + 0.4f * u(rng));
        R[n] = burst * (0.5f * std::sin(0.017f * n + 1.0f) + 0.5f * u(rng));
    }
}

// seconds spent in run() on frames [t0, n) of the signal (frames before t0 run untimed)
static double timeRun(const int os, const int signal, const bool ftz)
{
    const size_t warm = signal == kSilence ? static_cast<size_t>(kRate) : 0;
    const size_t n = warm + static_cast<size_t>(10 * kRate);
    std::vector<float> L(n, 0.0f), R(n, 0.0f), oL(kBlock), oR(kBlock);
    fillMusic(L, R, 0, signal == kSilence ? warm : n);

    const unsigned csr = _mm_getcsr();
    _mm_setcsr(ftz ? (csr | 0x8040) : (csr & ~0x8040u));   // FTZ (bit 15) and DAZ (bit 6)

    PluginExporter p(nullptr, nullptr, nullptr, nullptr);
    for (int i = 0; i < kPresetParamCount; ++i)
        p.setParameterValue(i, kBrimstonePresets[0].values[i]);
    p.setParameterValue(kParamOversampling, static_cast<float>(os));
    p.setParameterValue(kParamBypass, signal == kBypassed ? 1.0f : 0.0f);
    p.activate();

    double seconds = 0.0;
    for (size_t k = 0; k < n; k += kBlock)
    {
        const uint32_t f = static_cast<uint32_t>(std::min<size_t>(kBlock, n - k));
        const float* in[2] = { &L[k], &R[k] };
        float* out[2] = { oL.data(), oR.data() };
        const auto t0 = std::chrono::steady_clock::now();
        p.run(in, out, f);
        const auto t1 = std::chrono::steady_clock::now();
        if (k >= warm)
            seconds += std::chrono::duration<double>(t1 - t0).count();
    }
    _mm_setcsr(csr);
    return seconds;
}

int main()
{
    d_nextBufferSize = kBlock;
    d_nextSampleRate = kRate;
    const int factors[3] = { 1, 2, 4 };
    std::printf("Brimstone CPU bench: stereo, 44.1 kHz, blocks of %u, preset Default, best of 5\n", kBlock);
    std::printf("%-4s %-8s  %16s  %16s  %8s\n", "os", "signal", "FTZ off: ns  (%)", "FTZ on: ns  (%)", "off/on");
    for (const int o : { 0, 1, 2 })
        for (int s = 0; s < kNumSignals; ++s)
        {
            double best[2] = { 1e30, 1e30 };
            for (int run = 0; run < 5; ++run)
                for (int f = 0; f < 2; ++f)
                    best[f] = std::min(best[f], timeRun(o, s, f == 1));
            const double frames = 10 * kRate;
            std::printf("%dx   %-8s  %8.1f (%5.2f)  %8.1f (%5.2f)  %8.2f\n", factors[o], kSignalNames[s],
                        best[0] / frames * 1e9, best[0] / 10.0 * 100.0,
                        best[1] / frames * 1e9, best[1] / 10.0 * 100.0, best[0] / best[1]);
        }
    return 0;
}
