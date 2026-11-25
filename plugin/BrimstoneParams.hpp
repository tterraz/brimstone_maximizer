/*
 * Brimstone Maximizer - parameters and presets shared by the DSP and the UI.
 *
 * Copyright (C) 2026 Brimstone authors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef BRIMSTONE_PARAMS_HPP_INCLUDED
#define BRIMSTONE_PARAMS_HPP_INCLUDED

enum BrimstoneParam {
    // inputs
    kParamDecay = 0,   // envelope decay, samples at the host rate (2..30), original unit
    kParamDecayMs,     // envelope decay, milliseconds
    kParamDecayUnit,   // 0 = samples, 1 = milliseconds
    kParamKnee,        // knee point, dB (-90..0)
    kParamDrive,       // input gain into the maximizer (wet path only), dB
    kParamCeiling,     // output ceiling, dB
    kParamClip,        // 0 = off (original), 1 = hard, 2 = soft
    kParamStereo,      // 0 = linked, 1 = independent (original), 2 = mid/side
    kParamMix,         // dry/wet, percent
    kParamGain,        // gain into the clipper (after the mix), dB
    kParamOversampling,// 0 = 1x, 1 = 2x, 2 = 4x
    kParamMatch,       // level-matched listening (0/1)
    kParamBypass,      // host bypass
    kParamDelta,       // listen to the difference: output minus the input at the linear gain (0/1)
    // outputs
    kParamMeterIn,     // input peak, dBFS
    kParamMeterGR,     // gain reduction below makeup, dB
    kParamMeterOut,    // output peak, dBFS
    kParamMeterLoud,   // output minus input loudness (RMS, ~1.5 s), dB
    kParamLatency,     // current latency, samples
    kParamMeterClip,   // reduction by the clipper, dB (~100 ms release)
    kParamHistIn,      // history: input peak over the last ~30 ms, linear
    kParamHistOut,     // history: output peak over the last ~30 ms, linear
    kParamHistGR,      // history: maximizer reduction over the last ~30 ms, dB
    kParamHistClip,    // history: clipper reduction over the last ~30 ms, dB
    kParamCount
};

static const int kParamFirstOutput = kParamMeterIn;

enum { kClipOff = 0, kClipHard, kClipSoft };
enum { kStereoLinked = 0, kStereoIndependent, kStereoMidSide };

struct BrimstoneRange { float min, max, def; };

static const BrimstoneRange kBrimstoneRanges[kParamCount] = {
    {   2.0f,  30.0f,  30.0f },   // decay (samples)
    {  0.05f,   2.0f,  0.68f },   // decay (ms): 30 samples at 44.1 kHz
    {   0.0f,   1.0f,   0.0f },   // decay unit
    { -90.0f,   0.0f, -12.0f },   // knee
    {   0.0f,  24.0f,   0.0f },   // drive
    { -12.0f,   0.0f,  -0.1f },   // ceiling
    {   0.0f,   2.0f,   2.0f },   // clip
    {   0.0f,   2.0f,   0.0f },   // stereo
    {   0.0f, 100.0f, 100.0f },   // mix
    { -24.0f,  12.0f,   0.0f },   // output
    {   0.0f,   2.0f,   0.0f },   // oversampling
    {   0.0f,   1.0f,   0.0f },   // match
    {   0.0f,   1.0f,   0.0f },   // bypass
    {   0.0f,   1.0f,   0.0f },   // delta
    { -60.0f,   6.0f, -60.0f },   // meter in
    { -40.0f,   0.0f,   0.0f },   // meter GR
    { -60.0f,   6.0f, -60.0f },   // meter out
    { -24.0f,  24.0f,   0.0f },   // loudness difference
    {   0.0f, 4096.0f,  0.0f },   // latency
    { -40.0f,   0.0f,   0.0f },   // meter clip
    {   0.0f,   2.0f,   0.0f },   // history in
    {   0.0f,   2.0f,   0.0f },   // history out
    { -40.0f,   0.0f,   0.0f },   // history maximizer reduction
    { -40.0f,   0.0f,   0.0f },   // history clipper reduction
};

// ------------------------------------------------------------------------------------------------
// Factory presets: values of the input parameters kParamDecay .. kParamMatch (bypass and delta,
// monitoring switches, excluded).

static const int kPresetParamCount = kParamBypass;

struct BrimstonePreset {
    const char* name;
    float values[kPresetParamCount];
};

static const BrimstonePreset kBrimstonePresets[] = {
    //                     decay  ms   unit  knee  drive ceil  clip stereo mix  gain os  match
    { "Default",         { 30.0f, 0.68f, 0, -12.0f, 0.0f, -0.1f, 2, 0, 100.0f,  0.0f, 0, 0 } },
    { "Original (swh)",  { 30.0f, 0.68f, 0, -12.0f, 0.0f,  0.0f, 0, 1, 100.0f,  0.0f, 0, 0 } },
    { "Subtle",          { 30.0f, 0.68f, 0,  -6.0f, 0.0f, -0.3f, 2, 0,  60.0f,  0.0f, 1, 0 } },
    { "Loud master",     { 30.0f, 1.50f, 1, -14.0f, 3.0f, -0.3f, 2, 0, 100.0f,  0.0f, 2, 0 } },
    { "Wide crush",      { 12.0f, 0.27f, 0, -24.0f, 0.0f, -0.5f, 2, 2,  70.0f,  0.0f, 1, 0 } },
    { "Destruction",     {  2.0f, 0.05f, 0, -45.0f, 12.0f, -1.0f, 1, 1, 100.0f, -3.0f, 0, 0 } },
    // pure clipper: mix 0 % bypasses the maximizer core, Gain drives the clipper
    { "Clipper",         { 30.0f, 0.68f, 0, -12.0f, 0.0f, -0.3f, 1, 1,   0.0f,  3.0f, 2, 0 } },
};

static const int kBrimstonePresetCount = sizeof(kBrimstonePresets) / sizeof(kBrimstonePresets[0]);

#endif
