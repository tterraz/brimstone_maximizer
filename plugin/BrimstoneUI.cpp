/*
 * Brimstone Maximizer - user interface (DPF / NanoVG).
 *
 * Copyright (C) 2026 Brimstone authors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Everything is drawn in a logical 900 x 600 space, scaled to the window size (resizable,
 * aspect ratio kept).  Three sections, left to right in the order of the signal:
 *   1 MAXIMIZER (decay, knee, drive, stereo) -> 2 OUTPUT STAGE (mix, gain, ceiling, clip)
 *   -> 3 MONITOR (meters, oversampling, level match), and the history below (input and output
 * waveforms, maximizer and clipper reductions; click a legend entry to show / hide it).
 * Knobs: drag vertically (Shift = fine), mouse wheel, double-click or Ctrl+click = default,
 * right-click = type a value (Enter / Escape).  Every control has a tooltip.
 * Presets: drop-down list in the header (click to open, wheel over the closed box to step),
 * factory presets then the presets saved by the user (BrimstoneUserPresets.hpp: "Save current
 * settings...", name typed in the box; delete with the cross of a saved preset, clicked twice).
 * Two themes, Ember (dark) and Ash (light), switched by the EMBER | ASH pill of the header; the
 * choice is a preference of the user (BrimstonePrefs.hpp), not saved with the project.
 */

#include "DistrhoUI.hpp"
#include "BrimstoneParams.hpp"
#include "BrimstonePrefs.hpp"
#include "BrimstoneUserPresets.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

static const float kW = 900.0f;
static const float kH = 600.0f;
static const float kPi = 3.14159265358979f;
static const float kArcStart = 0.75f * kPi;
static const float kArcSweep = 1.5f * kPi;

// ------------------------------------------------------------------------------------------------
// Themes: every colour of the interface.  Ember (dark, the original look) and Ash (light, same
// identity: burnt orange and ochre on ash-grey paper).  The choice is a preference of the user
// (BrimstonePrefs.hpp), switched by the EMBER | ASH pill of the header.

enum { kSeriesIn = 0, kSeriesOut, kSeriesGR, kSeriesClip, kSeriesDiff, kNumSeries };

struct Theme {
    // background, header, panels
    Color bg0, bg1, bgGlow, header0, header1, headerLine, panel0, panel1, panelEdge, panelShadow, divider;
    // text: normal, secondary, faint, on an accent fill
    Color text, dim, faint, onAccent;
    // accents
    Color hot, gold, deep, arrow, brandGlow;
    // buttons, LEDs, segmented selectors, recessed wells (preset box, selectors / plots / meters)
    Color btnOn0, btnOn1, btnOff0, btnOff1, btnOnEdge, ledGlow, ledGlowEnd, ledOn, ledOff;
    Color well, plot, deepWell, hoverEdge, sel0, sel1;
    // knobs
    Color knobShadow, knob0, knob1, knobIn0, knobIn1, knobEdge, arcGlow, arcGlowActive, pointerGlow, pointer;
    // transfer curve, meters, history
    Color grid, gridZero, diagonal, ceilLine, curveGlow, dotGlow, dotGlowEnd, dotCore;
    Color meterEdge, meterTick, histGrid, histCeil;
    Color series[kNumSeries];
    // tooltip; label of the Bypass button when bypassed
    Color tipBg, tipEdge, bypassText;
};

static Color transparent(const Color& c)
{
    Color t(c);
    t.alpha = 0.0f;
    return t;
}

static const Theme kThemes[BrimstonePrefs::kThemeCount] = {
    {   // Ember
        Color(18, 7, 8), Color(38, 12, 12), Color(170, 40, 10, 0.26f), Color(46, 14, 12), Color(22, 8, 8),
        Color(Color(255, 76, 28), Color(0, 0, 0), 0.35f),
        Color(34, 17, 18), Color(26, 14, 15), Color(70, 28, 26), Color(0, 0, 0, 0.55f), Color(70, 28, 26, 0.7f),
        Color(236, 220, 212), Color(150, 116, 108), Color(96, 72, 68), Color(236, 220, 212),
        Color(255, 76, 28), Color(255, 184, 52), Color(150, 20, 16), Color(255, 110, 40, 0.7f),
        Color(255, 70, 20, 0.9f),
        Color(120, 30, 14), Color(70, 14, 8), Color(40, 22, 22), Color(24, 13, 14), Color(255, 110, 40, 0.8f),
        Color(255, 120, 40, 0.8f), Color(255, 60, 10, 0.0f), Color(255, 210, 120), Color(60, 30, 28),
        Color(14, 6, 7), Color(12, 5, 6), Color(10, 4, 5), Color(255, 110, 40, 0.6f),
        Color(150, 40, 16), Color(90, 18, 10),
        Color(0, 0, 0, 0.7f), Color(74, 52, 50), Color(24, 16, 17), Color(62, 44, 42), Color(20, 13, 14),
        Color(0, 0, 0, 0.6f), Color(255, 70, 20, 0.18f), Color(255, 70, 20, 0.30f), Color(255, 90, 30, 0.35f),
        Color(255, 140, 70),
        Color(70, 34, 32, 0.6f), Color(120, 50, 40, 0.8f), Color(120, 90, 85, 0.35f), Color(255, 200, 120, 0.25f),
        Color(255, 70, 20, 0.22f), Color(255, 200, 80, 0.8f), Color(255, 70, 20, 0.0f), Color(255, 236, 190),
        Color(60, 26, 24), Color(255, 220, 160, 0.6f), Color(70, 34, 32, 0.5f), Color(255, 200, 120, 0.35f),
        { Color(170, 120, 100), Color(255, 184, 52), Color(255, 76, 28), Color(120, 200, 255), Color(140, 225, 150) },
        Color(8, 3, 4, 0.95f), Color(255, 110, 40, 0.6f), Color(255, 184, 52),
    },
    {   // Ash
        Color(226, 220, 211), Color(237, 232, 225), Color(194, 65, 12, 0.07f), Color(246, 242, 236),
        Color(232, 226, 218), Color(194, 65, 12, 0.55f),
        Color(250, 247, 243), Color(242, 238, 232), Color(201, 191, 179), Color(80, 60, 45, 0.16f),
        Color(201, 191, 179, 0.8f),
        Color(42, 36, 33), Color(110, 98, 91), Color(150, 140, 132), Color(255, 248, 240),
        Color(194, 65, 12), Color(150, 88, 12), Color(138, 28, 12), Color(194, 65, 12, 0.75f),
        Color(194, 65, 12, 0.25f),
        Color(206, 82, 26), Color(160, 48, 12), Color(252, 250, 247), Color(232, 226, 218),
        Color(194, 65, 12, 0.9f),
        Color(255, 120, 40, 0.55f), Color(255, 120, 40, 0.0f), Color(230, 90, 20), Color(196, 186, 176),
        Color(232, 226, 218), Color(250, 248, 245), Color(214, 207, 198), Color(194, 65, 12, 0.7f),
        Color(206, 82, 26), Color(160, 48, 12),
        Color(80, 60, 45, 0.30f), Color(253, 251, 248), Color(214, 207, 198), Color(248, 245, 241),
        Color(222, 215, 206),
        Color(120, 100, 85, 0.45f), Color(194, 65, 12, 0.12f), Color(194, 65, 12, 0.22f), Color(194, 65, 12, 0.18f),
        Color(194, 65, 12),
        Color(150, 135, 120, 0.35f), Color(150, 110, 90, 0.7f), Color(110, 98, 91, 0.35f), Color(168, 101, 15, 0.45f),
        Color(194, 65, 12, 0.15f), Color(230, 120, 40, 0.6f), Color(230, 120, 40, 0.0f), Color(138, 28, 12),
        Color(190, 180, 168), Color(138, 28, 12, 0.7f), Color(150, 135, 120, 0.3f), Color(168, 101, 15, 0.6f),
        { Color(140, 110, 96), Color(150, 88, 12), Color(194, 65, 12), Color(31, 111, 178), Color(46, 139, 78) },
        Color(255, 253, 249, 0.97f), Color(194, 65, 12, 0.6f), Color(255, 236, 200),
    },
};

static const char* const kThemeNames[BrimstonePrefs::kThemeCount] = { "EMBER", "ASH" };

// ------------------------------------------------------------------------------------------------
// Layout

struct Rect {
    float x, y, w, h;
    bool contains(const float px, const float py) const
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

// sections
static const Rect kSecMax  = { 20, 70, 310, 380 };
static const Rect kSecOut  = { 340, 70, 260, 380 };
static const Rect kSecMon  = { 610, 70, 270, 380 };
static const Rect kHistory = { 20, 460, 860, 128 };
static const Rect kHistPlot = { 130, 468, 718, 112 };   // waveform / reduction area

enum { kKnobDecay = 0, kKnobKnee, kKnobDrive, kKnobMix, kKnobGain, kKnobCeiling, kKnobClipKnee, kNumKnobs };

struct KnobDef {
    int param;          // kParamDecay is replaced by kParamDecayMs when the unit is ms
    float cx, cy, r;
    bool labelRight;    // small knob of the maximizer section: label on the right
    const char* label;
    const char* help;
    bool compact;       // tiny knob in a row of selectors: no label, value on the right
};

static const KnobDef kKnobs[kNumKnobs] = {
    { kParamDecay,   100.0f, 165.0f, 50.0f, false, "DECAY",
      "Release of the envelope. Short: harder and grittier; long: smoother.", false },
    { kParamKnee,    250.0f, 165.0f, 50.0f, false, "KNEE",
      "Peaks above the knee are pushed to the ceiling; below it the gain is constant (makeup).", false },
    { kParamDrive,    66.0f, 372.0f, 26.0f, true,  "DRIVE",
      "Gain into the maximizer only: the dry signal of the mix is not changed.", false },
    { kParamMix,     390.0f, 345.0f, 24.0f, false, "MIX",
      "Balance between the dry signal and the maximizer. 0 %: maximizer off, pure clipper.", false },
    { kParamGain,    470.0f, 345.0f, 24.0f, false, "GAIN",
      "Gain after the mix, into the clipper: pushes the peaks over the ceiling.", false },
    { kParamCeiling, 550.0f, 345.0f, 24.0f, false, "CEILING",
      "Highest output level. Peaks above it are clipped when Clip is Hard or Soft.", false },
    { kParamClipKnee, 547.0f, 429.0f, 9.0f, false, "SOFT KNEE",
      "Clip Soft: the rounding starts this many dB below the ceiling. Small: closer to Hard, louder "
      "peaks; large: rounder, peaks pushed to the ceiling come out lower.", true },
};

static const Rect kUnit    = { 55, 270, 90, 18 };
static const Rect kStereo  = { 176, 361, 140, 22 };
static const Rect kGraph   = { 348, 96, 244, 200 };
static const Rect kClip    = { 400, 418, 130, 22 };
static const Rect kOvers   = { 734, 326, 138, 22 };
static const Rect kMatch   = { 622, 358, 120, 24 };
static const Rect kDelta   = { 750, 358, 122, 24 };
static const Rect kPreset  = { 456, 16, 206, 26 };   // drop-down box; the list opens below it
static const float kMenuItemH = 22.0f;     // preset list: item
static const float kMenuHeaderH = 20.0f;   // section title (FACTORY, USER)
static const float kMenuDividerH = 9.0f;   // line before "Save current settings..."

// rows of the preset list
enum { kRowHeader = 0, kRowFactory, kRowUser, kRowNone, kRowDivider, kRowSave };

struct MenuRow {
    int kind;
    int index;    // factory preset, saved preset (in fUserNames) or section (0 factory, 1 user)
    float y, h;   // y from the top of the list content
};
static const Rect kTheme   = { 674, 17, 76, 24 };
static const Rect kBypass  = { 762, 14, 114, 30 };

static const float kMeterX[4] = { 642.0f, 700.0f, 758.0f, 816.0f };   // IN, GR, CLIP, OUT
static const float kMeterY = 100.0f, kMeterH = 168.0f, kMeterW = 22.0f;

// hover zones with a tooltip (knobs are handled separately)
enum {
    kZoneUnit = 0, kZoneStereo, kZoneGraph, kZoneClip, kZoneOvers, kZoneMatch, kZoneDelta, kZonePreset,
    kZoneBypass, kZoneIn, kZoneGR, kZoneClipMeter, kZoneOut, kZoneHistory, kZoneTheme, kNumZones
};

struct Zone {
    Rect r;
    const char* title;
    const char* help;
};

static const Zone kZones[kNumZones] = {
    { kUnit, "DECAY UNIT",
      "SMP: samples, the unit of the original. MS: milliseconds, same sound at any sample rate." },
    { kStereo, "STEREO",
      "LINK: one detector for both channels. L / R: each channel alone (original). M / S: mid and side." },
    { kGraph, "TRANSFER CURVE",
      "Output level for a steady input level, all settings included. The dot follows the input." },
    { kClip, "CLIP",
      "OFF: like the original, may exceed 0 dBFS. HARD: flat at the ceiling. SOFT: rounded from "
      "the soft knee (small knob on the right) up to the ceiling." },
    { kOvers, "OVERSAMPLING",
      "Runs the processing at 2x or 4x the sample rate: less aliasing, more latency and CPU." },
    { kMatch, "LEVEL MATCH",
      "Lowers the output by the loudness gained (ITU-R BS.1770, K-weighted, ~1.5 s; shown below "
      "in LU), to compare with Bypass at equal loudness." },
    { kDelta, "DELTA",
      "Listen to what the maximizer and the clipper change: the output minus the same chain "
      "without reduction nor clipping (makeup gain removed). Bypass has priority." },
    { kPreset, "PRESETS",
      "Click to choose a preset: factory presets, then yours (saved for all projects on this "
      "computer). Wheel: next / previous. A * means a setting was changed since it was loaded." },
    { kBypass, "BYPASS",
      "Passes the input unprocessed, delayed by the latency (no click, no time shift)." },
    { { kMeterX[0] - 6, kMeterY, kMeterW + 12, kMeterH + 40 }, "IN", "Input peak level, dBFS." },
    { { kMeterX[1] - 6, kMeterY, kMeterW + 12, kMeterH + 40 }, "GR",
      "Gain reduction of the maximizer below its makeup gain, dB (0 when Mix is at 0 %)." },
    { { kMeterX[2] - 6, kMeterY, kMeterW + 12, kMeterH + 40 }, "CLIP",
      "Gain reduction of the clipper, dB." },
    { { kMeterX[3] - 6, kMeterY, kMeterW + 12, kMeterH + 40 }, "OUT", "Output peak level, dBFS." },
    { kHistPlot, "HISTORY (7 s)",
      "Input and output peak levels rise from the bottom (dBFS, right scale; dashed: ceiling). "
      "Reductions of the maximizer and of the clipper hang from the top (dB, left scale). "
      "out - in: total gain, output minus input peak (dB, centred scale inside on the right)." },
    { kTheme, "THEME",
      "EMBER: dark. ASH: light. Kept for all instances and projects (a setting of this computer, "
      "not of the project); other open windows change when reopened." },
};

static const int kHistorySize = 286;   // ~7 s at 40 points / s

// history series, each can be hidden by clicking its legend entry
static const char* const kSeriesNames[kNumSeries] = { "input", "output", "maximizer GR", "clipper GR",
                                                      "out - in" };
static const float kNoValue = -1000.0f;            // gap in a series (difference on silence)
static const float kHistDiffRange = 24.0f;        // dB: difference from -24 (bottom) to +24 (top)
static const float kHistLevelRange = 36.0f;       // dB: levels from -36 (bottom) to 0 dBFS (top)
static const float kHistReductionRange = 24.0f;   // dB: reductions from 0 (top) to -24 (bottom)

static Rect legendRect(const int s)
{
    return { kHistory.x + 10, kHistory.y + 26 + s * 17.0f, 104, 15 };
}
static const int kHoverZoneBase = 100;

// ------------------------------------------------------------------------------------------------

class BrimstoneUI : public UI
{
public:
    BrimstoneUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT, true)
    {
        for (int i = 0; i < kParamCount; ++i)
            fValues[i] = kBrimstoneRanges[i].def;
        for (int i = 0; i < kPresetParamCount; ++i)
            fPresetValues[i] = kBrimstonePresets[0].values[i];
        for (int s = 0; s < kNumSeries; ++s)
        {
            fShow[s] = true;
            for (int i = 0; i < kHistorySize; ++i)
                fHist[s][i] = s == kSeriesDiff ? kNoValue : 0.0f;
        }
        loadSharedResources();
        fFont = findFont(NANOVG_DEJAVU_SANS_TTF);
#ifdef BRIMSTONE_UI_DEMO_THEME
        fTheme = BRIMSTONE_UI_DEMO_THEME;   // screenshots: forced, never saved
#else
        fTheme = BrimstonePrefs::loadTheme();
#endif
        fT = &kThemes[fTheme];
        fLastPush = std::chrono::steady_clock::now();
#ifdef BRIMSTONE_UI_DEMO
        // screenshots only (-DBRIMSTONE_UI_DEMO=<hover id> [-DBRIMSTONE_UI_DEMO_PRESET=<index>]):
        // a tooltip, and a synthetic history (drum-like hits) since there is no audio
        fHover = BRIMSTONE_UI_DEMO;
        for (int i = 0; i < kHistorySize; ++i)
        {
            const float t = static_cast<float>(i % 20) / 20.0f;
            const float in = 0.25f + 0.7f * std::exp(-t * 9.0f) * ((i / 20) % 2 ? 1.0f : 0.7f)
                           + 0.03f * std::sin(i * 1.7f);
            const float g = in > 0.4f ? 0.4f / in : 1.0f;   // maximizer reduction
            float out = std::fmin(in * g * 2.2f, 0.95f);
            const float clip = in * g * 2.2f > 0.95f ? 0.95f / (in * g * 2.2f) : 1.0f;
            fHist[kSeriesIn][i] = in;
            fHist[kSeriesOut][i] = out;
            fHist[kSeriesGR][i] = 20.0f * std::log10(g);
            fHist[kSeriesClip][i] = 20.0f * std::log10(clip);
            fHist[kSeriesDiff][i] = difference(in, out);
        }
#endif
    }

protected:
    // ------------------------------------------------------------------ host -> UI

    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index >= kParamCount || fValues[index] == value)
            return;
        fValues[index] = value;
        if (index < static_cast<uint32_t>(kParamFirstOutput))
            repaint();
    }

    void programLoaded(const uint32_t index) override
    {
        if (index < static_cast<uint32_t>(kBrimstonePresetCount))
        {
            fPreset = static_cast<int>(index);
            fPresetUser = false;
            fPresetName = kBrimstonePresets[index].name;
            for (int i = 0; i < kPresetParamCount; ++i)
                fValues[i] = fPresetValues[i] = kBrimstonePresets[index].values[i];
            repaint();
        }
    }

    void uiIdle() override
    {
#ifdef BRIMSTONE_UI_DEMO_PRESET
        if (!fDemoPresetDone)
        {
            fDemoPresetDone = true;
            applyPreset(BRIMSTONE_UI_DEMO_PRESET);
        }
#endif
#ifdef BRIMSTONE_UI_DEMO_MENU
        // screenshots: the list open, the mouse over row BRIMSTONE_UI_DEMO_MENU
        // (with BRIMSTONE_UI_DEMO_ARM: its cross clicked once)
        if (!fDemoMenuDone)
        {
            fDemoMenuDone = true;
            openMenu();
            fMenuHover = BRIMSTONE_UI_DEMO_MENU;
# ifdef BRIMSTONE_UI_DEMO_ARM
            fDeleteArmed = fMenuHover;
# endif
        }
#endif
#ifdef BRIMSTONE_UI_DEMO_NAME
        // screenshots: a name being typed
        if (!fDemoNameDone)
        {
            fDemoNameDone = true;
            startNaming();
            fNameText = BRIMSTONE_UI_DEMO_NAME;
        }
#endif
        const auto now = std::chrono::steady_clock::now();
        if (now - fLastPush < std::chrono::milliseconds(25))
            return;
        fLastPush = now;
#ifdef BRIMSTONE_UI_DEMO
        return;   // keep the synthetic history
#endif
        fHistPos = (fHistPos + 1) % kHistorySize;
        fHist[kSeriesIn][fHistPos] = fValues[kParamHistIn];
        fHist[kSeriesOut][fHistPos] = fValues[kParamHistOut];
        fHist[kSeriesGR][fHistPos] = fValues[kParamHistGR];
        fHist[kSeriesClip][fHistPos] = fValues[kParamHistClip];
        fHist[kSeriesDiff][fHistPos] = difference(fValues[kParamHistIn], fValues[kParamHistOut]);
        repaint();
    }

    // ------------------------------------------------------------------ drawing

    void onNanoDisplay() override
    {
        const float s = scaleFactor();
        save();
        scale(s, s);

        drawBackground();
        drawHeader();
        drawMaximizerSection();
        drawOutputSection();
        drawMonitorSection();
        drawHistory();
        if (fMenuOpen)
            drawPresetMenu();
        else if (fNaming)
            drawNamingHint();
        else if (fHover >= 0 && fDrag < 0 && fEdit < 0)
            drawTooltip(fHover);

        restore();
    }

    // ------------------------------------------------------------------ mouse

    bool onMouse(const MouseEvent& ev) override
    {
        const float s = scaleFactor();
        const float x = static_cast<float>(ev.pos.getX()) / s;
        const float y = static_cast<float>(ev.pos.getY()) / s;

        if (!ev.press)
        {
            if (ev.button == kMouseButtonLeft && fDrag >= 0)
            {
                editParameter(knobParam(fDrag), false);
                fDrag = -1;
                repaint();
                return true;
            }
            return false;
        }

        if (fEdit >= 0)
            commitEdit(true);
        if (fNaming)
            cancelNaming();   // a click elsewhere cancels the name being typed

        // open list: a click chooses an item; outside the list it closes it (and goes no further)
        if (fMenuOpen)
        {
            menuClick(x, y, ev.button == kMouseButtonLeft);
            return true;
        }

        const int k = knobAt(x, y);

        if (ev.button == kMouseButtonRight)
        {
            if (k < 0)
                return false;
            fEdit = k;
            formatNumber(knobParam(k), fEditText, sizeof(fEditText));
            fEditFresh = true;
            repaint();
            return true;
        }

        if (ev.button != kMouseButtonLeft)
            return false;

        if (kPreset.contains(x, y)) { openMenu(); return true; }
        if (kBypass.contains(x, y)) { toggle(kParamBypass); return true; }
        if (kTheme.contains(x, y)) { setTheme(static_cast<int>(segmentAt(kTheme, x, BrimstonePrefs::kThemeCount))); return true; }
        if (kMatch.contains(x, y)) { toggle(kParamMatch); return true; }
        if (kDelta.contains(x, y)) { toggle(kParamDelta); return true; }
        if (kUnit.contains(x, y)) { setDecayUnit(x > kUnit.x + kUnit.w * 0.5f ? 1 : 0); return true; }
        if (kStereo.contains(x, y)) { setOnce(kParamStereo, segmentAt(kStereo, x, 3)); return true; }
        if (kClip.contains(x, y)) { setOnce(kParamClip, segmentAt(kClip, x, 3)); return true; }
        if (kOvers.contains(x, y)) { setOnce(kParamOversampling, segmentAt(kOvers, x, 3)); return true; }
        for (int s = 0; s < kNumSeries; ++s)
            if (legendRect(s).contains(x, y))
            {
                fShow[s] = !fShow[s];
                repaint();
                return true;
            }

        if (k < 0)
            return false;

        const int p = knobParam(k);
        const bool doubleClick = k == fLastClickTarget && ev.time - fLastClickTime < 350;
        fLastClickTarget = k;
        fLastClickTime = ev.time;

        if (doubleClick || (ev.mod & kModifierControl))
        {
            setOnce(p, defaultValue(p));
            fLastClickTarget = -1;
            return true;
        }

        fDrag = k;
        fLastY = y;
        editParameter(p, true);
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float s = scaleFactor();
        const float x = static_cast<float>(ev.pos.getX()) / s;
        const float y = static_cast<float>(ev.pos.getY()) / s;

        if (fMenuOpen)
        {
            int row = rowAt(x, y);
            if (row >= 0 && fRows[row].kind != kRowFactory && fRows[row].kind != kRowUser && fRows[row].kind != kRowSave)
                row = -1;
            const bool onCross = row >= 0 && fRows[row].kind == kRowUser && crossRect(rowRect(row)).contains(x, y);
            if (row != fMenuHover || onCross != fMenuOnCross)
            {
                if (row != fMenuHover)
                    fDeleteArmed = -1;
                fMenuHover = row;
                fMenuOnCross = onCross;
                repaint();
            }
            return true;
        }

        if (fDrag >= 0)
        {
            const int p = knobParam(fDrag);
            const float pixels = (ev.mod & kModifierShift) ? 900.0f : 220.0f;
            const float norm = toNorm(p, fValues[p]) + (fLastY - y) / pixels;
            fLastY = y;
            changeValue(p, fromNorm(p, norm));
            return true;
        }

        int h = knobAt(x, y);
        if (h < 0)
            for (int z = 0; z < kNumZones; ++z)
                if (kZones[z].r.contains(x, y))
                {
                    h = kHoverZoneBase + z;
                    break;
                }
        if (h != fHover)
        {
            fHover = h;
            repaint();
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float s = scaleFactor();
        const float x = static_cast<float>(ev.pos.getX()) / s, y = static_cast<float>(ev.pos.getY()) / s;
        const float d = static_cast<float>(ev.delta.getY());
        if (fMenuOpen)
        {
            // long list: scrolls
            const float most = fMenuContentH - menuVisibleH();
            if (most > 0.0f && d != 0.0f)
            {
                fMenuScroll = std::fmin(std::fmax(fMenuScroll - d * kMenuItemH, 0.0f), most);
                fMenuHover = fDeleteArmed = -1;
                repaint();
            }
            return true;
        }
        if (kPreset.contains(x, y) && !fNaming)
        {
            // wheel up: previous preset, down: next
            if (d != 0.0f)
                stepPreset(d > 0.0f ? -1 : 1);
            return true;
        }
        const int k = knobAt(x, y);
        if (k < 0)
            return false;
        const int p = knobParam(k);
        const float step = (ev.mod & kModifierShift) ? 0.005f : 0.025f;
        setOnce(p, fromNorm(p, toNorm(p, fValues[p]) + static_cast<float>(ev.delta.getY()) * step));
        return true;
    }

    // ------------------------------------------------------------------ keyboard (value entry)

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (fMenuOpen && ev.press && ev.key == kKeyEscape)
        {
            closeMenu();
            return true;
        }
        if (fNaming)
        {
            if (!ev.press)
                return true;
            if (ev.key == kKeyEnter)
                commitNaming();
            else if (ev.key == kKeyEscape)
                cancelNaming();
            else if ((ev.key == kKeyBackspace || ev.key == kKeyDelete) && !fNameText.empty())
            {
                fNameText.pop_back();
                fNameError = false;
                repaint();
            }
            return true;
        }
        if (fEdit < 0 || !ev.press)
            return fEdit >= 0;
        if (ev.key == kKeyEnter)
            commitEdit(true);
        else if (ev.key == kKeyEscape)
            commitEdit(false);
        else if (ev.key == kKeyBackspace || ev.key == kKeyDelete)
        {
            const size_t n = std::strlen(fEditText);
            if (fEditFresh)
                fEditText[0] = '\0';
            else if (n > 0)
                fEditText[n - 1] = '\0';
            fEditFresh = false;
            repaint();
        }
        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (fNaming)
        {
            // characters allowed in a preset name (BrimstoneUserPresets::validName)
            const uint32_t c = ev.character;
            if (c >= 32 && c <= 126 && std::strchr("<>:\"/\\|?*", static_cast<int>(c)) == nullptr
                && fNameText.size() < BrimstoneUserPresets::kMaxNameLength)
            {
                fNameText.push_back(static_cast<char>(c));
                fNameError = false;
                repaint();
            }
            return true;
        }
        if (fEdit < 0)
            return false;
        char c = static_cast<char>(ev.character);
        if (c == ',')
            c = '.';
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')
        {
            if (fEditFresh)
                fEditText[0] = '\0';
            fEditFresh = false;
            const size_t n = std::strlen(fEditText);
            if (n + 1 < sizeof(fEditText))
            {
                fEditText[n] = c;
                fEditText[n + 1] = '\0';
            }
            repaint();
        }
        return true;
    }

private:
    // ------------------------------------------------------------------ helpers

    float scaleFactor() const { return static_cast<float>(getWidth()) / kW; }

    int knobParam(const int k) const
    {
        if (k == kKnobDecay && fValues[kParamDecayUnit] > 0.5f)
            return kParamDecayMs;
        return kKnobs[k].param;
    }

    // the maximizer does nothing when the mix is at 0 %
    bool maximizerActive() const { return fValues[kParamMix] > 0.05f; }

    static float defaultValue(const int p) { return kBrimstoneRanges[p].def; }

    static float toNorm(const int p, const float v)
    {
        const BrimstoneRange& r = kBrimstoneRanges[p];
        return (v - r.min) / (r.max - r.min);
    }

    static float fromNorm(const int p, float n)
    {
        const BrimstoneRange& r = kBrimstoneRanges[p];
        n = std::fmin(std::fmax(n, 0.0f), 1.0f);
        return r.min + n * (r.max - r.min);
    }

    static float clampValue(const int p, const float v)
    {
        const BrimstoneRange& r = kBrimstoneRanges[p];
        return std::fmin(std::fmax(v, r.min), r.max);
    }

    static float segmentAt(const Rect& r, const float x, const int count)
    {
        const int i = static_cast<int>((x - r.x) / (r.w / count));
        return static_cast<float>(i < 0 ? 0 : i >= count ? count - 1 : i);
    }

    void changeValue(const int p, const float v)
    {
        if (fValues[p] == v)
            return;
        fValues[p] = v;
        setParameterValue(p, v);
        repaint();
    }

    // one complete edit gesture
    void setOnce(const int p, const float v)
    {
        editParameter(p, true);
        changeValue(p, v);
        editParameter(p, false);
    }

    void toggle(const int p) { setOnce(p, fValues[p] > 0.5f ? 0.0f : 1.0f); }

    // interface only: no parameter, no effect on the sound; saved as a preference of the user
    void setTheme(const int theme)
    {
        if (theme == fTheme)
            return;
        fTheme = theme;
        fT = &kThemes[theme];
#ifndef BRIMSTONE_UI_DEMO_THEME
        BrimstonePrefs::saveTheme(theme);
#endif
        repaint();
    }

    void setDecayUnit(const int unit)
    {
        if ((fValues[kParamDecayUnit] > 0.5f) == (unit == 1))
            return;
        // keep the same time constant when the unit changes
        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        if (unit == 1)
            setOnce(kParamDecayMs, clampValue(kParamDecayMs, static_cast<float>(fValues[kParamDecay] * 1000.0 / sr)));
        else
            setOnce(kParamDecay, clampValue(kParamDecay, static_cast<float>(fValues[kParamDecayMs] * sr / 1000.0)));
        setOnce(kParamDecayUnit, static_cast<float>(unit));
    }

    // loads a set of values: one edit gesture per parameter, remembered to show the "*"
    void loadValues(const float values[kPresetParamCount])
    {
        for (int i = 0; i < kPresetParamCount; ++i)
        {
            setOnce(i, values[i]);
            fPresetValues[i] = values[i];
        }
        repaint();
    }

    void applyPreset(const int index)
    {
        loadValues(kBrimstonePresets[index].values);
        fPreset = index;
        fPresetUser = false;
        fPresetName = kBrimstonePresets[index].name;
    }

    // a saved preset, read from its file now (it may have been changed by another instance)
    void applyUserPreset(const std::string& name)
    {
        float values[kPresetParamCount];
        if (!BrimstoneUserPresets::load(name, values))
            return;
        loadValues(values);
        fPreset = -1;
        fPresetUser = true;
        fPresetName = name;
    }

    // next (+1) or previous (-1) preset, factory presets then saved ones, in a loop
    void stepPreset(const int step)
    {
        fUserNames = BrimstoneUserPresets::list();
        const int n = kBrimstonePresetCount + static_cast<int>(fUserNames.size());
        int pos = fPreset;
        if (fPresetUser)
        {
            pos = kBrimstonePresetCount - 1;   // deleted meanwhile: between the two sections
            for (size_t j = 0; j < fUserNames.size(); ++j)
                if (BrimstoneUserPresets::sameName(fUserNames[j], fPresetName))
                    pos = kBrimstonePresetCount + static_cast<int>(j);
        }
        pos = (pos + step + n) % n;
        if (pos < kBrimstonePresetCount)
            applyPreset(pos);
        else
            applyUserPreset(fUserNames[pos - kBrimstonePresetCount]);
    }

    bool isCurrentRow(const MenuRow& m) const
    {
        if (m.kind == kRowFactory)
            return !fPresetUser && fPreset == m.index;
        return m.kind == kRowUser && fPresetUser && BrimstoneUserPresets::sameName(fUserNames[m.index], fPresetName);
    }

    // ---- preset list: rows, read again from the folder each time the list opens

    void buildRows()
    {
        fUserNames = BrimstoneUserPresets::list();
        fRows.clear();
        float y = 4.0f;
        const auto add = [&](const int kind, const int index, const float h) {
            fRows.push_back({ kind, index, y, h });
            y += h;
        };
        add(kRowHeader, 0, kMenuHeaderH);
        for (int i = 0; i < kBrimstonePresetCount; ++i)
            add(kRowFactory, i, kMenuItemH);
        add(kRowHeader, 1, kMenuHeaderH + 10.0f);
        if (fUserNames.empty())
            add(kRowNone, 0, kMenuItemH);
        for (size_t j = 0; j < fUserNames.size(); ++j)
            add(kRowUser, static_cast<int>(j), kMenuItemH);
        add(kRowDivider, 0, kMenuDividerH);
        add(kRowSave, 0, kMenuItemH);
        fMenuContentH = y + 4.0f;
        fMenuScroll = std::fmin(fMenuScroll, std::fmax(fMenuContentH - menuVisibleH(), 0.0f));
    }

    static float menuTop() { return kPreset.y + kPreset.h + 4.0f; }

    float menuVisibleH() const { return std::fmin(fMenuContentH, kH - menuTop() - 8.0f); }

    Rect rowRect(const int i) const
    {
        return { kPreset.x, menuTop() + fRows[i].y - fMenuScroll, kPreset.w, fRows[i].h };
    }

    // cross that deletes a saved preset (on the right of its row)
    static Rect crossRect(const Rect& r) { return { r.x + r.w - 26.0f, r.y, 26.0f, r.h }; }

    int rowAt(const float x, const float y) const
    {
        const Rect list = { kPreset.x, menuTop(), kPreset.w, menuVisibleH() };
        if (!list.contains(x, y))
            return -1;
        for (int i = 0; i < static_cast<int>(fRows.size()); ++i)
            if (rowRect(i).contains(x, y))
                return i;
        return -1;
    }

    void openMenu()
    {
        fMenuScroll = 0.0f;
        buildRows();
        fMenuOpen = true;
        fMenuHover = fDeleteArmed = -1;
        fMenuOnCross = false;
        repaint();
    }

    void closeMenu()
    {
        fMenuOpen = false;
        fMenuHover = fDeleteArmed = -1;
        repaint();
    }

    void menuClick(const float x, const float y, const bool left)
    {
        const int row = rowAt(x, y);
        if (row < 0)
        {
            closeMenu();
            return;
        }
        if (!left)
            return;
        const MenuRow m = fRows[row];
        if (m.kind == kRowUser && crossRect(rowRect(row)).contains(x, y))
        {
            // first click arms, second click deletes (no undo)
            if (fDeleteArmed == row)
            {
                BrimstoneUserPresets::erase(fUserNames[m.index]);
                buildRows();
                fDeleteArmed = fMenuHover = -1;
            }
            else
                fDeleteArmed = row;
            repaint();
            return;
        }
        if (m.kind == kRowFactory)
        {
            closeMenu();
            applyPreset(m.index);
        }
        else if (m.kind == kRowUser)
        {
            const std::string name = fUserNames[m.index];
            closeMenu();
            applyUserPreset(name);
        }
        else if (m.kind == kRowSave)
        {
            closeMenu();
            startNaming();
        }
    }

    // ---- saving: the name is typed in the preset box

    void startNaming()
    {
        fUserNames = BrimstoneUserPresets::list();
        fNaming = true;
        fNameText = fPresetUser ? fPresetName : std::string();
        fNameError = false;
        repaint();
    }

    void cancelNaming()
    {
        fNaming = false;
        repaint();
    }

    static std::string trimmed(const std::string& s)
    {
        const size_t a = s.find_first_not_of(' ');
        if (a == std::string::npos)
            return std::string();
        return s.substr(a, s.find_last_not_of(' ') - a + 1);
    }

    void commitNaming()
    {
        const std::string name = trimmed(fNameText);
        if (!BrimstoneUserPresets::validName(name) || BrimstoneUserPresets::isFactoryName(name))
            return;   // the hint says why
        float values[kPresetParamCount];
        for (int i = 0; i < kPresetParamCount; ++i)
            values[i] = fValues[i];
        if (!BrimstoneUserPresets::save(name, values))
        {
            fNameError = true;
            repaint();
            return;
        }
        for (int i = 0; i < kPresetParamCount; ++i)
            fPresetValues[i] = values[i];
        fNaming = false;
        fPreset = -1;
        fPresetUser = true;
        fPresetName = name;
        repaint();
    }

    // what Enter will do with the name typed; error: Enter does nothing
    const char* namingHint(bool& error) const
    {
        const std::string name = trimmed(fNameText);
        error = true;
        if (fNameError)
            return "Could not write the preset file (folder not writable).";
        if (!name.empty() && BrimstoneUserPresets::isFactoryName(name))
            return "This is the name of a factory preset: choose another one.";
        if (!name.empty() && !BrimstoneUserPresets::validName(name))
            return "Invalid name: no dot at the end, not CON, PRN, AUX, NUL, COM1 or LPT1.";
        error = false;
        if (name.empty())
            return "Type a name. Enter: save. Esc: cancel.";
        for (const std::string& other : fUserNames)
            if (BrimstoneUserPresets::sameName(other, name))
                return "Enter: replace your preset of this name. Esc: cancel.";
        return "Enter: save (kept for all projects on this computer). Esc: cancel.";
    }

    bool presetModified() const
    {
        for (int i = 0; i < kPresetParamCount; ++i)
            if (std::fabs(fValues[i] - fPresetValues[i]) > 1e-4f)
                return true;
        return false;
    }

    void commitEdit(const bool apply)
    {
        if (fEdit < 0)
            return;
        if (apply && fEditText[0] != '\0')
        {
            char* end = nullptr;
            const float v = std::strtof(fEditText, &end);
            if (end != fEditText && std::isfinite(v))
            {
                const int p = knobParam(fEdit);
                setOnce(p, clampValue(p, v));
            }
        }
        fEdit = -1;
        repaint();
    }

    int knobAt(const float x, const float y) const
    {
        for (int i = 0; i < kNumKnobs; ++i)
        {
            const float dx = x - kKnobs[i].cx, dy = y - kKnobs[i].cy, r = kKnobs[i].r + 8.0f;
            if (dx * dx + dy * dy <= r * r)
                return i;
        }
        return -1;
    }

    void formatNumber(const int p, char* buf, const size_t size) const
    {
        const float v = fValues[p];
        std::snprintf(buf, size, p == kParamDecayMs ? "%.2f" : p == kParamMix ? "%.0f" : "%.1f", v);
    }

    void valueText(const int p, char* buf, const size_t size) const
    {
        const float v = fValues[p];
        switch (p)
        {
        case kParamDecay:   std::snprintf(buf, size, "%.1f samples", v); break;
        case kParamDecayMs: std::snprintf(buf, size, "%.2f ms", v); break;
        case kParamKnee:    std::snprintf(buf, size, "%.1f dB", v); break;
        case kParamDrive:   std::snprintf(buf, size, "+%.1f dB", v); break;
        case kParamMix:     std::snprintf(buf, size, "%.0f %%", v); break;
        case kParamGain:    std::snprintf(buf, size, "%+.1f dB", v); break;
        case kParamCeiling: std::snprintf(buf, size, "%.1f dB", v); break;
        case kParamClipKnee: std::snprintf(buf, size, "%.1f dB", v); break;
        default:            std::snprintf(buf, size, "%.2f", v); break;
        }
    }

    void label(const float x, const float y, const float size, const Color& c, const int align,
               const char* str, const float spacing = 0.0f)
    {
        fontFaceId(fFont);
        fontSize(size);
        textLetterSpacing(spacing);
        fillColor(c);
        textAlign(align);
        text(x, y, str, nullptr);
    }

    void panel(const Rect& r)
    {
        beginPath();
        rect(r.x - 10, r.y - 10, r.w + 20, r.h + 24);
        roundedRect(r.x, r.y, r.w, r.h, 8);
        pathWinding(CW);
        fillPaint(boxGradient(r.x, r.y + 3, r.w, r.h, 10, 14, fT->panelShadow, transparent(fT->panelShadow)));
        fill();

        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 8);
        fillPaint(linearGradient(r.x, r.y, r.x, r.y + r.h, fT->panel0, fT->panel1));
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->panelEdge);
        stroke();
    }

    // numbered section title: "1  MAXIMIZER  what it does"
    void sectionTitle(const Rect& r, const char* number, const char* title, const char* subtitle)
    {
        const float cx = r.x + 18, cy = r.y + 17;
        beginPath();
        circle(cx, cy, 8.5f);
        fillPaint(linearGradient(0, cy - 8, 0, cy + 8, fT->hot, fT->deep));
        fill();
        label(cx, cy + 0.5f, 10, fT->onAccent, ALIGN_CENTER | ALIGN_MIDDLE, number);
        label(cx + 15, cy, 11, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, title, 2);
        label(r.x + r.w - 12, cy, 9, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, subtitle, 0.5f);
    }

    void divider(const float x0, const float x1, const float y)
    {
        beginPath();
        moveTo(x0, y + 0.5f);
        lineTo(x1, y + 0.5f);
        strokeWidth(1.0f);
        strokeColor(fT->divider);
        stroke();
    }

    // small arrow in the gap between two sections: the signal goes from left to right
    void flowArrow(const float x, const float y)
    {
        beginPath();
        moveTo(x - 3, y - 7);
        lineTo(x + 4, y);
        lineTo(x - 3, y + 7);
        closePath();
        fillColor(fT->arrow);
        fill();
    }

    void button(const Rect& r, const bool on, const char* str, const float size = 11.0f)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 5);
        fillPaint(linearGradient(r.x, r.y, r.x, r.y + r.h,
                                 on ? fT->btnOn0 : fT->btnOff0,
                                 on ? fT->btnOn1 : fT->btnOff1));
        fill();
        strokeWidth(1.0f);
        strokeColor(on ? fT->btnOnEdge : fT->panelEdge);
        stroke();
        if (str)
            label(r.x + r.w * 0.5f, r.y + r.h * 0.5f, size, on ? fT->onAccent : fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, str, 1);
    }

    void led(const float x, const float y, const bool on)
    {
        if (on)
        {
            beginPath();
            circle(x, y, 10);
            fillPaint(radialGradient(x, y, 2, 10, fT->ledGlow, fT->ledGlowEnd));
            fill();
        }
        beginPath();
        circle(x, y, 4.0f);
        fillColor(on ? fT->ledOn : fT->ledOff);
        fill();
    }

    void segmented(const Rect& r, const char* const* labels, const int count, const int selected,
                   const float size)
    {
        const float sw = r.w / count;
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 5);
        fillColor(fT->well);
        fill();
        strokeWidth(1.0f);
        strokeColor(fHover == hoverOf(r) ? fT->hoverEdge : fT->panelEdge);
        stroke();
        for (int i = 0; i < count; ++i)
        {
            if (i == selected)
            {
                beginPath();
                roundedRect(r.x + i * sw + 1.5f, r.y + 1.5f, sw - 3.0f, r.h - 3.0f, 4);
                fillPaint(linearGradient(0, r.y, 0, r.y + r.h, fT->sel0, fT->sel1));
                fill();
            }
            label(r.x + (i + 0.5f) * sw, r.y + r.h * 0.5f, size, i == selected ? fT->onAccent : fT->dim,
                  ALIGN_CENTER | ALIGN_MIDDLE, labels[i], 0.5f);
        }
    }

    int hoverOf(const Rect& r) const
    {
        for (int z = 0; z < kNumZones; ++z)
            if (kZones[z].r.x == r.x && kZones[z].r.y == r.y)
                return kHoverZoneBase + z;
        return -2;
    }

    // ------------------------------------------------------------------ parts

    void drawBackground()
    {
        beginPath();
        rect(0, 0, kW, kH);
        fillPaint(linearGradient(0, 0, 0, kH, fT->bg1, fT->bg0));
        fill();

        beginPath();
        rect(0, 0, kW, kH);
        fillPaint(radialGradient(kW * 0.5f, kH + 80, 40, 560, fT->bgGlow, transparent(fT->bgGlow)));
        fill();

        panel(kSecMax);
        panel(kSecOut);
        panel(kSecMon);
        panel(kHistory);
        flowArrow(kSecMax.x + kSecMax.w + 5, kSecMax.y + kSecMax.h * 0.5f);
        flowArrow(kSecOut.x + kSecOut.w + 5, kSecOut.y + kSecOut.h * 0.5f);
    }

    void drawHeader()
    {
        beginPath();
        rect(0, 0, kW, 56);
        fillPaint(linearGradient(0, 0, 0, 56, fT->header0, fT->header1));
        fill();
        beginPath();
        moveTo(0, 56.5f);
        lineTo(kW, 56.5f);
        strokeColor(fT->headerLine);
        strokeWidth(1.0f);
        stroke();

        fontFaceId(fFont);
        fontSize(30);
        textLetterSpacing(6);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontBlur(10);
        fillColor(fT->brandGlow);
        text(26, 29, "BRIMSTONE", nullptr);
        fontBlur(0);
        fillPaint(linearGradient(0, 14, 0, 44, fT->gold, fT->hot));
        text(26, 29, "BRIMSTONE", nullptr);
        label(256, 31, 14, fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, "MAXIMIZER", 5);

        // preset drop-down box
        label(kPreset.x - 10, kPreset.y + kPreset.h * 0.5f, 9, fT->faint, ALIGN_RIGHT | ALIGN_MIDDLE, "PRESET", 1);
        beginPath();
        roundedRect(kPreset.x, kPreset.y, kPreset.w, kPreset.h, 5);
        fillColor(fT->well);
        fill();
        strokeWidth(1.0f);
        strokeColor(fMenuOpen || fNaming ? fT->gold : fHover == hoverOf(kPreset) ? fT->hoverEdge : fT->panelEdge);
        stroke();
        char name[64];
        if (fNaming)
        {
            std::snprintf(name, sizeof(name), "%s_", fNameText.c_str());
            label(kPreset.x + 12, kPreset.y + kPreset.h * 0.5f, 12, fT->gold, ALIGN_LEFT | ALIGN_MIDDLE, name, 0.5f);
        }
        else
        {
            std::snprintf(name, sizeof(name), "%s%s", fPresetName.c_str(), presetModified() ? " *" : "");
            label(kPreset.x + 12, kPreset.y + kPreset.h * 0.5f, 12, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, name, 0.5f);
        }
        // arrow: down when closed, up when open
        const float ax = kPreset.x + kPreset.w - 14, ay = kPreset.y + kPreset.h * 0.5f, d = fMenuOpen ? -1.0f : 1.0f;
        beginPath();
        moveTo(ax - 5, ay - 2.5f * d);
        lineTo(ax + 5, ay - 2.5f * d);
        lineTo(ax, ay + 3.5f * d);
        closePath();
        fillColor(fMenuOpen ? fT->gold : fT->dim);
        fill();

        // theme
        segmented(kTheme, kThemeNames, BrimstonePrefs::kThemeCount, fTheme, 9);

        // bypass
        const bool bypassed = fValues[kParamBypass] > 0.5f;
        button(kBypass, bypassed, nullptr);
        led(kBypass.x + 18, kBypass.y + kBypass.h * 0.5f, !bypassed);
        label(kBypass.x + kBypass.w * 0.5f + 10, kBypass.y + kBypass.h * 0.5f, 12, bypassed ? fT->bypassText : fT->text,
              ALIGN_CENTER | ALIGN_MIDDLE, bypassed ? "BYPASSED" : "ACTIVE", 2);
    }

    void drawMaximizerSection()
    {
        const bool active = maximizerActive();
        sectionTitle(kSecMax, "1", "MAXIMIZER", "pushes the peaks up");

        if (!active)
            globalAlpha(0.3f);
        drawKnob(kKnobDecay);
        drawKnob(kKnobKnee);
        const char* const units[2] = { "SMP", "MS" };
        segmented(kUnit, units, 2, fValues[kParamDecayUnit] > 0.5f ? 1 : 0, 9);

        divider(kSecMax.x + 12, kSecMax.x + kSecMax.w - 12, 312);
        drawKnob(kKnobDrive);
        label(kStereo.x, kStereo.y - 11, 10, fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, "STEREO", 1);
        const char* const stereo[3] = { "LINK", "L / R", "M / S" };
        segmented(kStereo, stereo, 3, static_cast<int>(std::lround(fValues[kParamStereo])), 10);
        globalAlpha(1.0f);

        if (!active)
            label(kSecMax.x + kSecMax.w * 0.5f, 430, 10, fT->gold, ALIGN_CENTER | ALIGN_MIDDLE,
                  "OFF: mix at 0 % (pure clipper)", 0.5f);
    }

    void drawOutputSection()
    {
        sectionTitle(kSecOut, "2", "OUTPUT STAGE", "mix, gain, clipper");
        drawGraph();
        divider(kSecOut.x + 12, kSecOut.x + kSecOut.w - 12, 304);
        drawKnob(kKnobMix);
        drawKnob(kKnobGain);
        drawKnob(kKnobCeiling);
        label(kSecOut.x + 12, kClip.y + kClip.h * 0.5f, 10, fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, "CLIP", 1);
        const char* const clip[3] = { "OFF", "HARD", "SOFT" };
        const int clipMode = static_cast<int>(std::lround(fValues[kParamClip]));
        segmented(kClip, clip, 3, clipMode, 10);
        if (clipMode != kClipSoft)
            globalAlpha(0.3f);   // the soft knee only acts in Soft
        drawKnob(kKnobClipKnee);
        globalAlpha(1.0f);
    }

    void drawMonitorSection()
    {
        sectionTitle(kSecMon, "3", "MONITOR", "levels and options");
        drawMeters();
        divider(kSecMon.x + 12, kSecMon.x + kSecMon.w - 12, 312);

        label(kSecMon.x + 12, kOvers.y + kOvers.h * 0.5f, 10, fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, "OVERSAMPLING", 1);
        const char* const os[3] = { "1x", "2x", "4x" };
        segmented(kOvers, os, 3, static_cast<int>(std::lround(fValues[kParamOversampling])), 10);

        const bool match = fValues[kParamMatch] > 0.5f;
        button(kMatch, match, nullptr);
        led(kMatch.x + 14, kMatch.y + kMatch.h * 0.5f, match);
        label(kMatch.x + kMatch.w * 0.5f + 8, kMatch.y + kMatch.h * 0.5f, 10, match ? fT->onAccent : fT->dim,
              ALIGN_CENTER | ALIGN_MIDDLE, "LEVEL MATCH", 1);
        const bool delta = fValues[kParamDelta] > 0.5f;
        button(kDelta, delta, nullptr);
        led(kDelta.x + 14, kDelta.y + kDelta.h * 0.5f, delta);
        label(kDelta.x + kDelta.w * 0.5f + 8, kDelta.y + kDelta.h * 0.5f, 10, delta ? fT->onAccent : fT->dim,
              ALIGN_CENTER | ALIGN_MIDDLE, "DELTA", 1);
        char loud[48];
        std::snprintf(loud, sizeof(loud), "loudness (BS.1770): %+.1f LU", fValues[kParamMeterLoud]);
        label(kSecMon.x + 12, 394, 10, fT->gold, ALIGN_LEFT | ALIGN_MIDDLE, loud);

        char lat[64];
        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        std::snprintf(lat, sizeof(lat), "latency %.0f samples (%.2f ms)", fValues[kParamLatency],
                      fValues[kParamLatency] * 1000.0 / sr);
        label(kSecMon.x + 12, 410, 9, fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, lat, 0.5f);
        label(kSecMon.x + 12, 425, 9, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, "knobs: drag, wheel, shift = fine", 0.5f);
        label(kSecMon.x + 12, 439, 9, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE,
              "double-click: reset, right-click: type", 0.5f);
    }

    void drawKnob(const int i)
    {
        const KnobDef& k = kKnobs[i];
        const int p = knobParam(i);
        const bool active = i == fHover || i == fDrag || i == fEdit;
        const float n = toNorm(p, fValues[p]);
        const float a = kArcStart + n * kArcSweep;
        const bool big = k.r > 40.0f;
        const float track = k.r + (big ? 9.0f : k.compact ? 4.0f : 6.0f);
        const float tw = big ? 5.0f : k.compact ? 2.5f : 3.5f;

        beginPath();
        arc(k.cx, k.cy, track, kArcStart, kArcStart + kArcSweep, CW);
        lineCap(ROUND);
        strokeWidth(tw);
        strokeColor(fT->deepWell);
        stroke();

        if (n > 0.001f)
        {
            beginPath();
            arc(k.cx, k.cy, track, kArcStart, a, CW);
            strokeWidth(tw * 3.2f);
            strokeColor(active ? fT->arcGlowActive : fT->arcGlow);
            stroke();

            beginPath();
            arc(k.cx, k.cy, track, kArcStart, a, CW);
            strokeWidth(tw);
            strokePaint(linearGradient(k.cx - track, k.cy + track, k.cx + track, k.cy - track, fT->deep,
                                       active ? fT->gold : Color(fT->gold, fT->hot, 0.4f)));
            stroke();
        }

        beginPath();
        circle(k.cx, k.cy + 4, k.r + 2);
        fillPaint(radialGradient(k.cx, k.cy + 4, k.r - 4, k.r + 8, fT->knobShadow, transparent(fT->knobShadow)));
        fill();

        beginPath();
        circle(k.cx, k.cy, k.r);
        fillPaint(linearGradient(k.cx, k.cy - k.r, k.cx, k.cy + k.r, fT->knob0, fT->knob1));
        fill();
        beginPath();
        circle(k.cx, k.cy, k.r * 0.84f);
        fillPaint(radialGradient(k.cx - k.r * 0.3f, k.cy - k.r * 0.4f, 2, k.r * 1.1f,
                                 fT->knobIn0, fT->knobIn1));
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->knobEdge);
        stroke();

        const float ca = std::cos(a), sa = std::sin(a);
        beginPath();
        moveTo(k.cx + ca * k.r * 0.30f, k.cy + sa * k.r * 0.30f);
        lineTo(k.cx + ca * k.r * 0.78f, k.cy + sa * k.r * 0.78f);
        lineCap(ROUND);
        strokeWidth(big ? 4.0f : k.compact ? 2.4f : 3.0f);
        strokeColor(fT->pointerGlow);
        stroke();
        strokeWidth(big ? 2.2f : k.compact ? 1.4f : 1.8f);
        strokeColor(active ? fT->gold : fT->pointer);
        stroke();

        char buf[32];
        valueText(p, buf, sizeof(buf));
        float vx, vy;
        int align;
        if (k.compact)
        {
            vx = k.cx + k.r + 10;   // no label (it is in the tooltip), the value on the right
            vy = k.cy;
            align = ALIGN_LEFT | ALIGN_MIDDLE;
        }
        else if (k.labelRight)
        {
            label(k.cx + k.r + 16, k.cy - 9, 12, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, k.label, 2);
            vx = k.cx + k.r + 16;
            vy = k.cy + 9;
            align = ALIGN_LEFT | ALIGN_MIDDLE;
        }
        else
        {
            const float gap = big ? 26.0f : 17.0f;
            label(k.cx, k.cy + k.r + gap, big ? 13 : 11, fT->text, ALIGN_CENTER | ALIGN_MIDDLE, k.label, big ? 3 : 1.5f);
            vx = k.cx;
            vy = k.cy + k.r + gap + (big ? 17.0f : 15.0f);
            align = ALIGN_CENTER | ALIGN_MIDDLE;
        }

        if (i == fEdit)
        {
            const float bw = k.compact ? 46.0f : 76.0f, bx = k.labelRight || k.compact ? vx - 4 : vx - bw * 0.5f;
            beginPath();
            roundedRect(bx, vy - 10, bw, 20, 4);
            fillColor(fT->deepWell);
            fill();
            strokeWidth(1.0f);
            strokeColor(fT->gold);
            stroke();
            char e[40];
            std::snprintf(e, sizeof(e), "%s_", fEditText);
            label(bx + bw * 0.5f, vy, 12, fT->gold, ALIGN_CENTER | ALIGN_MIDDLE, e);
        }
        else
        {
            label(vx, vy, big ? 12 : k.compact ? 10 : 11, active ? fT->gold : fT->dim, align, buf);
        }
    }

    float softClip(const float x, const float ceiling) const
    {
        const float t = std::pow(10.0f, -fValues[kParamClipKnee] * 0.05f) * ceiling;
        if (x <= t)
            return x;
        const float r = ceiling - t;
        return t + r * std::tanh((x - t) / r);
    }

    // static curve: output level (dB) for a steady input level (dB), all settings included
    float curve(const float inDb) const
    {
        const float x = std::pow(10.0f, inDb * 0.05f);
        const float s = x * std::pow(10.0f, fValues[kParamDrive] * 0.05f);
        const float knee = std::pow(10.0f, fValues[kParamKnee] * 0.05f);
        const float ceil = std::pow(10.0f, fValues[kParamCeiling] * 0.05f);
        const float wet = s / std::fmax(s, knee) * ceil;
        float y = (x + (wet - x) * fValues[kParamMix] * 0.01f) * std::pow(10.0f, fValues[kParamGain] * 0.05f);
        const int clip = static_cast<int>(std::lround(fValues[kParamClip]));
        if (clip == kClipHard)
            y = std::fmin(y, ceil);
        else if (clip == kClipSoft)
            y = softClip(y, ceil);
        return 20.0f * std::log10(std::fmax(y, 1e-6f));
    }

    void drawGraph()
    {
        const float lo = -60.0f, hi = 6.0f;
        const float x0 = kGraph.x + 22, y0 = kGraph.y + 4, w = kGraph.w - 30, h = kGraph.h - 34;
        const auto gx = [&](float db) { return x0 + (std::fmin(std::fmax(db, lo), hi) - lo) / (hi - lo) * w; };
        const auto gy = [&](float db) { return y0 + h - (std::fmin(std::fmax(db, lo), hi) - lo) / (hi - lo) * h; };

        beginPath();
        rect(x0, y0, w, h);
        fillColor(fT->plot);
        fill();

        strokeWidth(1.0f);
        for (float db = -48.0f; db <= 0.0f; db += 12.0f)
        {
            beginPath();
            moveTo(gx(db) + 0.5f, y0);
            lineTo(gx(db) + 0.5f, y0 + h);
            moveTo(x0, gy(db) + 0.5f);
            lineTo(x0 + w, gy(db) + 0.5f);
            strokeColor(db == 0.0f ? fT->gridZero : fT->grid);
            stroke();

            char buf[8];
            std::snprintf(buf, sizeof(buf), "%.0f", db);
            label(x0 - 4, gy(db), 9, fT->dim, ALIGN_RIGHT | ALIGN_MIDDLE, buf);
            label(gx(db), y0 + h + 9, 9, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, buf);
        }

        beginPath();
        moveTo(gx(lo), gy(lo));
        lineTo(gx(hi), gy(hi));
        strokeColor(fT->diagonal);
        stroke();

        // ceiling line
        beginPath();
        moveTo(x0, gy(fValues[kParamCeiling]) + 0.5f);
        lineTo(x0 + w, gy(fValues[kParamCeiling]) + 0.5f);
        strokeColor(fT->ceilLine);
        stroke();

        save();
        scissor(x0, y0, w, h);
        for (int pass = 0; pass < 2; ++pass)
        {
            beginPath();
            moveTo(gx(lo), gy(curve(lo)));
            for (int j = 1; j <= 132; ++j)
            {
                const float in = lo + j * 0.5f;
                lineTo(gx(in), gy(curve(in)));
            }
            lineCap(ROUND);
            lineJoin(ROUND);
            strokeWidth(pass == 0 ? 7.0f : 2.2f);
            strokeColor(pass == 0 ? fT->curveGlow : fT->gold);
            stroke();
        }

        const float in = fValues[kParamMeterIn];
        if (in > lo + 0.5f)
        {
            const float px = gx(in), py = gy(curve(in));
            beginPath();
            circle(px, py, 9);
            fillPaint(radialGradient(px, py, 1, 9, fT->dotGlow, fT->dotGlowEnd));
            fill();
            beginPath();
            circle(px, py, 3);
            fillColor(fT->dotCore);
            fill();
        }
        restore();

        label(kGraph.x + kGraph.w * 0.5f + 8, kGraph.y + kGraph.h - 6, 9, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE,
              "INPUT dB  ->  OUTPUT dB", 1);
    }

    void meterBar(const float x, const float frac, const bool fromTop, const char* name, const char* value)
    {
        const float y = kMeterY, h = kMeterH, w = kMeterW;
        beginPath();
        roundedRect(x, y, w, h, 3);
        fillColor(fT->deepWell);
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->meterEdge);
        stroke();

        const float f = std::fmin(std::fmax(frac, 0.0f), 1.0f);
        if (f > 0.002f)
        {
            const float fh = (h - 4) * f;
            const float fy = fromTop ? y + 2 : y + h - 2 - fh;
            beginPath();
            roundedRect(x + 2, fy, w - 4, fh, 2);
            if (fromTop)
                fillPaint(linearGradient(0, y, 0, y + h, fT->hot, fT->deep));
            else
                fillPaint(linearGradient(0, y + h, 0, y, fT->deep, fT->gold));
            fill();
        }

        label(x + w * 0.5f, y + h + 13, 11, fT->text, ALIGN_CENTER | ALIGN_MIDDLE, name, 1);
        label(x + w * 0.5f, y + h + 28, 10, fT->dim, ALIGN_CENTER | ALIGN_MIDDLE, value);
    }

    void drawMeters()
    {
        char bin[16], bgr[16], bcl[16], bout[16];
        const float in = fValues[kParamMeterIn], gr = fValues[kParamMeterGR], out = fValues[kParamMeterOut];
        const float cl = fValues[kParamMeterClip];
        std::snprintf(bin, sizeof(bin), in <= -59.9f ? "-inf" : "%.1f", in);
        std::snprintf(bgr, sizeof(bgr), "%.1f", gr > -0.05f ? 0.0f : gr);
        std::snprintf(bcl, sizeof(bcl), "%.1f", cl > -0.05f ? 0.0f : cl);
        std::snprintf(bout, sizeof(bout), out <= -59.9f ? "-inf" : "%.1f", out);

        meterBar(kMeterX[0], (in + 60.0f) / 66.0f, false, "IN", bin);
        meterBar(kMeterX[1], -gr / 40.0f, true, "GR", bgr);
        meterBar(kMeterX[2], -cl / 40.0f, true, "CLIP", bcl);
        meterBar(kMeterX[3], (out + 60.0f) / 66.0f, false, "OUT", bout);

        // 0 dBFS ticks and scales (levels: -60..+6 dB; reduction: 0..-40 dB)
        const float y = kMeterY, h = kMeterH, w = kMeterW;
        const auto ly = [&](float db) { return y + h - 2 - (h - 4) * ((db + 60.0f) / 66.0f); };
        strokeWidth(1.0f);
        beginPath();
        moveTo(kMeterX[0] - 4, ly(0.0f) + 0.5f);
        lineTo(kMeterX[0] + w + 4, ly(0.0f) + 0.5f);
        moveTo(kMeterX[3] - 4, ly(0.0f) + 0.5f);
        lineTo(kMeterX[3] + w + 4, ly(0.0f) + 0.5f);
        strokeColor(fT->meterTick);
        stroke();
        for (float db = -48.0f; db <= 0.0f; db += 12.0f)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%.0f", db);
            label(kMeterX[0] + w + 13, ly(db), 8, fT->faint, ALIGN_CENTER | ALIGN_MIDDLE, b);
            label(kMeterX[3] - 13, ly(db), 8, fT->faint, ALIGN_CENTER | ALIGN_MIDDLE, b);
        }
        for (float db = -10.0f; db >= -30.0f; db -= 10.0f)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%.0f", db);
            label((kMeterX[1] + w + kMeterX[2]) * 0.5f, y + 2 + (h - 4) * (-db / 40.0f), 8, fT->faint,
                  ALIGN_CENTER | ALIGN_MIDDLE, b);
        }
    }

    static float difference(const float in, const float out)
    {
        if (in < 1e-4f || out < 1e-6f)
            return kNoValue;   // silence: no ratio
        return 20.0f * std::log10(out / in);
    }

    Color seriesColor(const int s) const
    {
        return fT->series[s];
    }

    // one history series as a thin line; y(v) maps a value to the plot; kNoValue leaves a gap
    template <typename F>
    void historyLine(const int s, const float x0, const float dx, F y, const float width)
    {
        beginPath();
        bool pen = false;
        for (int j = 0; j < kHistorySize; ++j)
        {
            const float v = fHist[s][(fHistPos + 1 + j) % kHistorySize];
            if (v <= kNoValue)
            {
                pen = false;
                continue;
            }
            if (pen)
                lineTo(x0 + j * dx, y(v));
            else
                moveTo(x0 + j * dx, y(v));
            pen = true;
        }
        lineJoin(ROUND);
        lineCap(ROUND);
        strokeWidth(width);
        strokeColor(seriesColor(s));
        stroke();
    }

    void drawHistory()
    {
        const Rect& r = kHistPlot;
        beginPath();
        rect(r.x, r.y, r.w, r.h);
        fillColor(fT->plot);
        fill();

        // legend: click to show / hide
        label(kHistory.x + 12, kHistory.y + 16, 10, fT->text, ALIGN_LEFT | ALIGN_MIDDLE, "HISTORY", 1);
        for (int s = 0; s < kNumSeries; ++s)
        {
            const Rect lr = legendRect(s);
            const Color c = seriesColor(s);
            beginPath();
            moveTo(lr.x, lr.y + 8.5f);
            lineTo(lr.x + 12, lr.y + 8.5f);
            lineCap(ROUND);
            strokeWidth(fShow[s] ? 2.0f : 1.0f);
            strokeColor(fShow[s] ? c : fT->faint);
            stroke();
            label(lr.x + 18, lr.y + 8, 9.5f, fShow[s] ? fT->text : fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, kSeriesNames[s], 0.3f);
        }
        label(kHistory.x + 12, kHistory.y + kHistory.h - 8, 8, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, "click: show / hide", 0.3f);

        // levels rise from the bottom (0 dBFS at the top), reductions hang from the top
        const auto levelY = [&](const float v) {
            const float db = v > 1e-6f ? 20.0f * std::log10(v) : -120.0f;
            return r.y + r.h * std::fmin(std::fmax(-db / kHistLevelRange, 0.0f), 1.0f);
        };
        const auto reductionY = [&](const float db) {
            return r.y + r.h * std::fmin(std::fmax(-db / kHistReductionRange, 0.0f), 1.0f);
        };
        const auto differenceY = [&](const float db) {
            return r.y + r.h * 0.5f * (1.0f - std::fmin(std::fmax(db / kHistDiffRange, -1.0f), 1.0f));
        };

        // grid (level scale), ceiling (dashed), scales: levels right, reductions left
        strokeWidth(1.0f);
        beginPath();
        for (float db = -12.0f; db > -kHistLevelRange; db -= 12.0f)
        {
            moveTo(r.x, r.y + r.h * (-db / kHistLevelRange) + 0.5f);
            lineTo(r.x + r.w, r.y + r.h * (-db / kHistLevelRange) + 0.5f);
        }
        strokeColor(fT->histGrid);
        stroke();

        const float ceilY = r.y + r.h * std::fmin(-fValues[kParamCeiling] / kHistLevelRange, 1.0f);
        beginPath();
        for (float xx = r.x; xx < r.x + r.w; xx += 8.0f)
        {
            moveTo(xx, ceilY + 0.5f);
            lineTo(xx + 4.0f, ceilY + 0.5f);
        }
        strokeColor(fT->histCeil);
        stroke();

        for (float db = 0.0f; db >= -kHistLevelRange + 1.0f; db -= 12.0f)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%.0f", db);
            label(r.x + r.w + 4, r.y + r.h * (-db / kHistLevelRange), 8, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, b);
        }
        label(r.x + r.w + 4, r.y + r.h - 4, 7, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, "dBFS");
        for (float db = -6.0f; db >= -kHistReductionRange + 1.0f; db -= 6.0f)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%.0f", db);
            label(r.x - 4, r.y + r.h * (-db / kHistReductionRange), 8, Color(fT->hot, fT->faint, 0.5f),
                  ALIGN_RIGHT | ALIGN_MIDDLE, b);
        }

        // difference: centred scale (0 dB in the middle), inside the plot on the right
        if (fShow[kSeriesDiff])
        {
            const Color dc = Color(seriesColor(kSeriesDiff), fT->faint, 0.4f);
            beginPath();
            for (float xx = r.x; xx < r.x + r.w; xx += 6.0f)
            {
                moveTo(xx, differenceY(0.0f) + 0.5f);
                lineTo(xx + 2.0f, differenceY(0.0f) + 0.5f);
            }
            strokeWidth(1.0f);
            strokeColor(Color(dc, Color(0, 0, 0, 0.0f), 0.5f));
            stroke();
            for (float db = 12.0f; db >= -12.0f; db -= 12.0f)
            {
                char b[8];
                std::snprintf(b, sizeof(b), "%+.0f", db);
                label(r.x + r.w - 4, differenceY(db), 8, dc, ALIGN_RIGHT | ALIGN_MIDDLE, db == 0.0f ? "0" : b);
            }
        }

        const float dx = r.w / (kHistorySize - 1);
        save();
        scissor(r.x, r.y - 1, r.w, r.h + 2);
        if (fShow[kSeriesIn])
            historyLine(kSeriesIn, r.x, dx, levelY, 0.9f);
        if (fShow[kSeriesOut])
            historyLine(kSeriesOut, r.x, dx, levelY, 1.0f);
        if (fShow[kSeriesGR])
            historyLine(kSeriesGR, r.x, dx, reductionY, 1.0f);
        if (fShow[kSeriesClip])
            historyLine(kSeriesClip, r.x, dx, reductionY, 1.0f);
        if (fShow[kSeriesDiff])
            historyLine(kSeriesDiff, r.x, dx, differenceY, 1.0f);
        restore();
    }

    // open preset list, drawn over everything: factory presets, a line, saved presets, a line,
    // "Save current settings..."; the current preset is marked with a dot
    void drawPresetMenu()
    {
        const float x = kPreset.x, w = kPreset.w, top = menuTop(), h = menuVisibleH();
        beginPath();
        rect(x - 10, top - 6, w + 20, h + 22);
        roundedRect(x, top, w, h, 6);
        pathWinding(CW);
        fillPaint(boxGradient(x, top + 3, w, h, 8, 12, fT->panelShadow, transparent(fT->panelShadow)));
        fill();
        beginPath();
        roundedRect(x, top, w, h, 6);
        fillColor(fT->tipBg);
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->tipEdge);
        stroke();

        save();
        scissor(x, top + 1, w, h - 2);
        for (int i = 0; i < static_cast<int>(fRows.size()); ++i)
        {
            const MenuRow& m = fRows[i];
            const Rect r = rowRect(i);
            if (r.y + r.h < top || r.y > top + h)
                continue;
            const float cy = r.y + r.h * 0.5f;
            const bool hover = i == fMenuHover;
            switch (m.kind)
            {
            case kRowHeader:
                if (m.index == 1)
                    menuLine(x, w, r.y + 5.0f);
                label(x + 12, r.y + r.h - 9.0f, 8.5f, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE,
                      m.index == 0 ? "FACTORY" : "USER", 1.5f);
                break;
            case kRowDivider:
                menuLine(x, w, cy);
                break;
            case kRowNone:
                label(x + 22, cy, 11, fT->faint, ALIGN_LEFT | ALIGN_MIDDLE, "none saved yet", 0.3f);
                break;
            case kRowSave:
                if (hover)
                    menuHighlight(r);
                label(x + 12, cy, 12, hover ? fT->onAccent : fT->dim, ALIGN_LEFT | ALIGN_MIDDLE, "+", 0.0f);
                label(x + 22, cy, 12, hover ? fT->onAccent : fT->dim, ALIGN_LEFT | ALIGN_MIDDLE,
                      "Save current settings...", 0.3f);
                break;
            default:   // factory or saved preset
            {
                const bool user = m.kind == kRowUser;
                const bool armed = user && i == fDeleteArmed;
                if (hover)
                    menuHighlight(r);
                const Color c = hover ? fT->onAccent : isCurrentRow(m) ? fT->gold : fT->text;
                if (isCurrentRow(m))
                {
                    beginPath();
                    circle(x + 12, cy, 2.5f);
                    fillColor(c);
                    fill();
                }
                save();
                intersectScissor(x, r.y, w - (armed ? 70.0f : user && hover ? 28.0f : 6.0f), r.h);
                label(x + 22, cy, 12, c, ALIGN_LEFT | ALIGN_MIDDLE,
                      user ? fUserNames[m.index].c_str() : kBrimstonePresets[m.index].name, 0.5f);
                restore();
                if (user && (hover || armed))
                {
                    // cross: delete (first click arms, the second deletes)
                    const Rect cr = crossRect(r);
                    const float ccx = cr.x + cr.w * 0.5f;
                    const Color xc = armed ? fT->ledOn : fMenuOnCross ? fT->onAccent : Color(fT->onAccent, fT->sel0, 0.45f);
                    if (armed)
                    {
                        beginPath();
                        roundedRect(cr.x + 3, r.y + 3, cr.w - 6, r.h - 6, 3);
                        fillColor(fT->deep);
                        fill();
                        label(cr.x - 4, cy, 10, hover ? fT->onAccent : fT->hot, ALIGN_RIGHT | ALIGN_MIDDLE, "delete?", 0.5f);
                    }
                    beginPath();
                    moveTo(ccx - 3.5f, cy - 3.5f);
                    lineTo(ccx + 3.5f, cy + 3.5f);
                    moveTo(ccx + 3.5f, cy - 3.5f);
                    lineTo(ccx - 3.5f, cy + 3.5f);
                    lineCap(ROUND);
                    strokeWidth(1.6f);
                    strokeColor(xc);
                    stroke();
                }
                break;
            }
            }
        }
        restore();

        // scroll bar when the list is longer than the window
        if (fMenuContentH > h)
        {
            const float most = fMenuContentH - h;
            const float bh = h * h / fMenuContentH;
            beginPath();
            roundedRect(x + w - 4, top + 2 + (h - 4 - bh) * (fMenuScroll / most), 2.5f, bh, 1.2f);
            fillColor(fT->dim);
            fill();
        }
    }

    void menuLine(const float x, const float w, const float y)
    {
        beginPath();
        moveTo(x + 8, y + 0.5f);
        lineTo(x + w - 8, y + 0.5f);
        strokeWidth(1.0f);
        strokeColor(fT->tipEdge);
        stroke();
    }

    void menuHighlight(const Rect& r)
    {
        beginPath();
        roundedRect(r.x + 2, r.y + 1, r.w - 4, r.h - 2, 4);
        fillPaint(linearGradient(0, r.y, 0, r.y + r.h, fT->sel0, fT->sel1));
        fill();
    }

    // under the box while a name is typed: what Enter will do
    void drawNamingHint()
    {
        bool error = false;
        const char* const hint = namingHint(error);
        const float w = 260.0f, pad = 10.0f, x = kPreset.x, y = kPreset.y + kPreset.h + 6.0f;
        fontFaceId(fFont);
        fontSize(10);
        textLetterSpacing(0);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        float bounds[4];
        textBoxBounds(0, 0, w - 2 * pad, hint, nullptr, bounds);
        const float h = 26.0f + (bounds[3] - bounds[1]) + 6.0f;
        beginPath();
        roundedRect(x, y, w, h, 6);
        fillColor(fT->tipBg);
        fill();
        strokeWidth(1.0f);
        strokeColor(error ? fT->hot : fT->tipEdge);
        stroke();
        label(x + pad, y + 13, 11, fT->gold, ALIGN_LEFT | ALIGN_MIDDLE, "SAVE PRESET", 0.5f);
        fontSize(10);
        textLetterSpacing(0);
        fillColor(error ? fT->hot : fT->text);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        textBox(x + pad, y + 24, w - 2 * pad, hint, nullptr);
    }

    void drawTooltip(const int hover)
    {
        char title[96];
        const char* help;
        Rect anchor;
        if (hover < kHoverZoneBase)
        {
            const KnobDef& d = kKnobs[hover];
            char v[32];
            valueText(knobParam(hover), v, sizeof(v));
            std::snprintf(title, sizeof(title), "%s  %s", d.label, v);
            help = d.help;
            const float r = d.r + 10;
            anchor = { d.cx - r, d.cy - r, 2 * r, 2 * r };
        }
        else
        {
            const Zone& z = kZones[hover - kHoverZoneBase];
            std::snprintf(title, sizeof(title), "%s", z.title);
            help = z.help;
            anchor = z.r;
        }

        const float w = 280.0f, pad = 10.0f;
        fontFaceId(fFont);
        fontSize(10);
        textLetterSpacing(0);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        float bounds[4];
        textBoxBounds(0, 0, w - 2 * pad, help, nullptr, bounds);
        const float h = 26.0f + (bounds[3] - bounds[1]) + 6.0f;

        float x = anchor.x + anchor.w * 0.5f - w * 0.5f;
        x = std::fmin(std::fmax(x, 6.0f), kW - w - 6.0f);
        float y = anchor.y + anchor.h + 6.0f;
        if (y + h > kH - 4.0f)
            y = anchor.y - h - 6.0f;

        beginPath();
        roundedRect(x, y, w, h, 6);
        fillColor(fT->tipBg);
        fill();
        strokeWidth(1.0f);
        strokeColor(fT->tipEdge);
        stroke();
        label(x + pad, y + 13, 11, fT->gold, ALIGN_LEFT | ALIGN_MIDDLE, title, 0.5f);
        fontSize(10);
        textLetterSpacing(0);
        fillColor(fT->text);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        textBox(x + pad, y + 24, w - 2 * pad, help, nullptr);
    }

    // ------------------------------------------------------------------ state

    float fValues[kParamCount];
    int fTheme = BrimstonePrefs::kThemeEmber;
    const Theme* fT = &kThemes[BrimstonePrefs::kThemeEmber];
    FontId fFont = 0;
    int fDrag = -1, fHover = -1, fEdit = -1;
    int fLastClickTarget = -1;
    uint fLastClickTime = 0;
    float fLastY = 0.0f;
    // current preset: factory (fPreset) or saved (fPresetUser), its values when loaded (for the "*")
    int fPreset = 0;
    bool fPresetUser = false;
    std::string fPresetName = kBrimstonePresets[0].name;
    float fPresetValues[kPresetParamCount];
    // preset list
    bool fMenuOpen = false;
    std::vector<MenuRow> fRows;
    std::vector<std::string> fUserNames;   // saved presets, read when the list opens
    int fMenuHover = -1;                   // selectable row under the mouse (-1: none)
    bool fMenuOnCross = false;             // the mouse is on the cross of that row
    int fDeleteArmed = -1;                 // row whose cross was clicked once
    float fMenuScroll = 0.0f, fMenuContentH = 0.0f;
    // name of a preset being saved
    bool fNaming = false, fNameError = false;
    std::string fNameText;
    char fEditText[16] = { 0 };
    bool fEditFresh = false;

    float fHist[kNumSeries][kHistorySize];
    bool fShow[kNumSeries];
    int fHistPos = 0;
    std::chrono::steady_clock::time_point fLastPush;
#ifdef BRIMSTONE_UI_DEMO_PRESET
    bool fDemoPresetDone = false;
#endif
#ifdef BRIMSTONE_UI_DEMO_MENU
    bool fDemoMenuDone = false;
#endif
#ifdef BRIMSTONE_UI_DEMO_NAME
    bool fDemoNameDone = false;
#endif

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrimstoneUI)
};

UI* createUI()
{
    return new BrimstoneUI();
}

END_NAMESPACE_DISTRHO
