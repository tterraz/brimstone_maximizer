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
 */

#include "DistrhoUI.hpp"
#include "BrimstoneParams.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

static const float kW = 900.0f;
static const float kH = 600.0f;
static const float kPi = 3.14159265358979f;
static const float kArcStart = 0.75f * kPi;
static const float kArcSweep = 1.5f * kPi;

// Palette
static const Color cBg0(18, 7, 8);
static const Color cBg1(38, 12, 12);
static const Color cPanel(26, 14, 15);
static const Color cPanelEdge(70, 28, 26);
static const Color cText(236, 220, 212);
static const Color cDim(150, 116, 108);
static const Color cFaint(96, 72, 68);
static const Color cHot(255, 76, 28);
static const Color cGold(255, 184, 52);
static const Color cDeep(150, 20, 16);

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

enum { kKnobDecay = 0, kKnobKnee, kKnobDrive, kKnobMix, kKnobGain, kKnobCeiling, kNumKnobs };

struct KnobDef {
    int param;          // kParamDecay is replaced by kParamDecayMs when the unit is ms
    float cx, cy, r;
    bool labelRight;    // small knob of the maximizer section: label on the right
    const char* label;
    const char* help;
};

static const KnobDef kKnobs[kNumKnobs] = {
    { kParamDecay,   100.0f, 165.0f, 50.0f, false, "DECAY",
      "Release of the envelope. Short: harder and grittier; long: smoother." },
    { kParamKnee,    250.0f, 165.0f, 50.0f, false, "KNEE",
      "Peaks above the knee are pushed to the ceiling; below it the gain is constant (makeup)." },
    { kParamDrive,    66.0f, 372.0f, 26.0f, true,  "DRIVE",
      "Gain into the maximizer only: the dry signal of the mix is not changed." },
    { kParamMix,     390.0f, 345.0f, 24.0f, false, "MIX",
      "Balance between the dry signal and the maximizer. 0 %: maximizer off, pure clipper." },
    { kParamGain,    470.0f, 345.0f, 24.0f, false, "GAIN",
      "Gain after the mix, into the clipper: pushes the peaks over the ceiling." },
    { kParamCeiling, 550.0f, 345.0f, 24.0f, false, "CEILING",
      "Highest output level. Peaks above it are clipped when Clip is Hard or Soft." },
};

static const Rect kUnit    = { 55, 270, 90, 18 };
static const Rect kStereo  = { 176, 361, 140, 22 };
static const Rect kGraph   = { 348, 96, 244, 200 };
static const Rect kClip    = { 400, 418, 192, 22 };
static const Rect kOvers   = { 734, 326, 138, 22 };
static const Rect kMatch   = { 622, 364, 120, 24 };
static const Rect kPrevBtn = { 462, 16, 26, 26 };
static const Rect kPreset  = { 490, 16, 180, 26 };
static const Rect kNextBtn = { 672, 16, 26, 26 };
static const Rect kBypass  = { 724, 14, 152, 30 };

static const float kMeterX[4] = { 642.0f, 700.0f, 758.0f, 816.0f };   // IN, GR, CLIP, OUT
static const float kMeterY = 100.0f, kMeterH = 168.0f, kMeterW = 22.0f;

// hover zones with a tooltip (knobs are handled separately)
enum {
    kZoneUnit = 0, kZoneStereo, kZoneGraph, kZoneClip, kZoneOvers, kZoneMatch, kZonePreset,
    kZoneBypass, kZoneIn, kZoneGR, kZoneClipMeter, kZoneOut, kZoneHistory, kNumZones
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
      "OFF: like the original, may exceed 0 dBFS. HARD: flat at the ceiling. SOFT: rounded above 60 %." },
    { kOvers, "OVERSAMPLING",
      "Runs the processing at 2x or 4x the sample rate: less aliasing, more latency and CPU." },
    { kMatch, "LEVEL MATCH",
      "Lowers the output by the loudness gained, to compare with Bypass at equal level." },
    { { kPrevBtn.x, kPrevBtn.y, kNextBtn.x + kNextBtn.w - kPrevBtn.x, kPrevBtn.h }, "PRESETS",
      "Factory presets. A * means a setting was changed since the preset was loaded." },
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
};

static const int kHistorySize = 286;   // ~7 s at 40 points / s

// history series, each can be hidden by clicking its legend entry
enum { kSeriesIn = 0, kSeriesOut, kSeriesGR, kSeriesClip, kSeriesDiff, kNumSeries };
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
        for (int s = 0; s < kNumSeries; ++s)
        {
            fShow[s] = true;
            for (int i = 0; i < kHistorySize; ++i)
                fHist[s][i] = s == kSeriesDiff ? kNoValue : 0.0f;
        }
        loadSharedResources();
        fFont = findFont(NANOVG_DEJAVU_SANS_TTF);
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
            for (int i = 0; i < kPresetParamCount; ++i)
                fValues[i] = kBrimstonePresets[index].values[i];
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
        if (fHover >= 0 && fDrag < 0 && fEdit < 0)
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

        if (kPrevBtn.contains(x, y)) { applyPreset((fPreset + kBrimstonePresetCount - 1) % kBrimstonePresetCount); return true; }
        if (kNextBtn.contains(x, y) || kPreset.contains(x, y)) { applyPreset((fPreset + 1) % kBrimstonePresetCount); return true; }
        if (kBypass.contains(x, y)) { toggle(kParamBypass); return true; }
        if (kMatch.contains(x, y)) { toggle(kParamMatch); return true; }
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
        const int k = knobAt(static_cast<float>(ev.pos.getX()) / s, static_cast<float>(ev.pos.getY()) / s);
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

    void applyPreset(const int index)
    {
        fPreset = index;
        for (int i = 0; i < kPresetParamCount; ++i)
            setOnce(i, kBrimstonePresets[index].values[i]);
        repaint();
    }

    bool presetModified() const
    {
        for (int i = 0; i < kPresetParamCount; ++i)
            if (std::fabs(fValues[i] - kBrimstonePresets[fPreset].values[i]) > 1e-4f)
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
        fillPaint(boxGradient(r.x, r.y + 3, r.w, r.h, 10, 14, Color(0, 0, 0, 0.55f), Color(0, 0, 0, 0.0f)));
        fill();

        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 8);
        fillPaint(linearGradient(r.x, r.y, r.x, r.y + r.h, Color(34, 17, 18), cPanel));
        fill();
        strokeWidth(1.0f);
        strokeColor(cPanelEdge);
        stroke();
    }

    // numbered section title: "1  MAXIMIZER  what it does"
    void sectionTitle(const Rect& r, const char* number, const char* title, const char* subtitle)
    {
        const float cx = r.x + 18, cy = r.y + 17;
        beginPath();
        circle(cx, cy, 8.5f);
        fillPaint(linearGradient(0, cy - 8, 0, cy + 8, cHot, cDeep));
        fill();
        label(cx, cy + 0.5f, 10, cText, ALIGN_CENTER | ALIGN_MIDDLE, number);
        label(cx + 15, cy, 11, cText, ALIGN_LEFT | ALIGN_MIDDLE, title, 2);
        label(r.x + r.w - 12, cy, 9, cFaint, ALIGN_RIGHT | ALIGN_MIDDLE, subtitle, 0.5f);
    }

    void divider(const float x0, const float x1, const float y)
    {
        beginPath();
        moveTo(x0, y + 0.5f);
        lineTo(x1, y + 0.5f);
        strokeWidth(1.0f);
        strokeColor(Color(70, 28, 26, 0.7f));
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
        fillColor(Color(255, 110, 40, 0.7f));
        fill();
    }

    void button(const Rect& r, const bool on, const char* str, const float size = 11.0f)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 5);
        fillPaint(linearGradient(r.x, r.y, r.x, r.y + r.h,
                                 on ? Color(120, 30, 14) : Color(40, 22, 22),
                                 on ? Color(70, 14, 8) : Color(24, 13, 14)));
        fill();
        strokeWidth(1.0f);
        strokeColor(on ? Color(255, 110, 40, 0.8f) : cPanelEdge);
        stroke();
        if (str)
            label(r.x + r.w * 0.5f, r.y + r.h * 0.5f, size, on ? cText : cDim, ALIGN_CENTER | ALIGN_MIDDLE, str, 1);
    }

    void led(const float x, const float y, const bool on)
    {
        if (on)
        {
            beginPath();
            circle(x, y, 10);
            fillPaint(radialGradient(x, y, 2, 10, Color(255, 120, 40, 0.8f), Color(255, 60, 10, 0.0f)));
            fill();
        }
        beginPath();
        circle(x, y, 4.0f);
        fillColor(on ? Color(255, 210, 120) : Color(60, 30, 28));
        fill();
    }

    void segmented(const Rect& r, const char* const* labels, const int count, const int selected,
                   const float size)
    {
        const float sw = r.w / count;
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 5);
        fillColor(Color(14, 6, 7));
        fill();
        strokeWidth(1.0f);
        strokeColor(fHover == hoverOf(r) ? Color(255, 110, 40, 0.6f) : cPanelEdge);
        stroke();
        for (int i = 0; i < count; ++i)
        {
            if (i == selected)
            {
                beginPath();
                roundedRect(r.x + i * sw + 1.5f, r.y + 1.5f, sw - 3.0f, r.h - 3.0f, 4);
                fillPaint(linearGradient(0, r.y, 0, r.y + r.h, Color(150, 40, 16), Color(90, 18, 10)));
                fill();
            }
            label(r.x + (i + 0.5f) * sw, r.y + r.h * 0.5f, size, i == selected ? cText : cDim,
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
        fillPaint(linearGradient(0, 0, 0, kH, cBg1, cBg0));
        fill();

        beginPath();
        rect(0, 0, kW, kH);
        fillPaint(radialGradient(kW * 0.5f, kH + 80, 40, 560, Color(170, 40, 10, 0.26f), Color(0, 0, 0, 0.0f)));
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
        fillPaint(linearGradient(0, 0, 0, 56, Color(46, 14, 12), Color(22, 8, 8)));
        fill();
        beginPath();
        moveTo(0, 56.5f);
        lineTo(kW, 56.5f);
        strokeColor(Color(cHot, Color(0, 0, 0), 0.35f));
        strokeWidth(1.0f);
        stroke();

        fontFaceId(fFont);
        fontSize(30);
        textLetterSpacing(6);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontBlur(10);
        fillColor(Color(255, 70, 20, 0.9f));
        text(26, 29, "BRIMSTONE", nullptr);
        fontBlur(0);
        fillPaint(linearGradient(0, 14, 0, 44, cGold, cHot));
        text(26, 29, "BRIMSTONE", nullptr);
        label(256, 31, 14, cDim, ALIGN_LEFT | ALIGN_MIDDLE, "MAXIMIZER", 5);

        // preset browser
        label(kPrevBtn.x - 10, kPrevBtn.y + kPrevBtn.h * 0.5f, 9, cFaint, ALIGN_RIGHT | ALIGN_MIDDLE, "PRESET", 1);
        button(kPrevBtn, false, "<", 13);
        button(kNextBtn, false, ">", 13);
        beginPath();
        roundedRect(kPreset.x, kPreset.y, kPreset.w, kPreset.h, 5);
        fillColor(Color(14, 6, 7));
        fill();
        strokeWidth(1.0f);
        strokeColor(cPanelEdge);
        stroke();
        char name[64];
        std::snprintf(name, sizeof(name), "%s%s", kBrimstonePresets[fPreset].name, presetModified() ? " *" : "");
        label(kPreset.x + kPreset.w * 0.5f, kPreset.y + kPreset.h * 0.5f, 12, cText,
              ALIGN_CENTER | ALIGN_MIDDLE, name, 0.5f);

        // bypass
        const bool bypassed = fValues[kParamBypass] > 0.5f;
        button(kBypass, bypassed, nullptr);
        led(kBypass.x + 18, kBypass.y + kBypass.h * 0.5f, !bypassed);
        label(kBypass.x + kBypass.w * 0.5f + 10, kBypass.y + kBypass.h * 0.5f, 12, bypassed ? cGold : cText,
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
        label(kStereo.x, kStereo.y - 11, 10, cDim, ALIGN_LEFT | ALIGN_MIDDLE, "STEREO", 1);
        const char* const stereo[3] = { "LINK", "L / R", "M / S" };
        segmented(kStereo, stereo, 3, static_cast<int>(std::lround(fValues[kParamStereo])), 10);
        globalAlpha(1.0f);

        if (!active)
            label(kSecMax.x + kSecMax.w * 0.5f, 430, 10, cGold, ALIGN_CENTER | ALIGN_MIDDLE,
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
        label(kSecOut.x + 12, kClip.y + kClip.h * 0.5f, 10, cDim, ALIGN_LEFT | ALIGN_MIDDLE, "CLIP", 1);
        const char* const clip[3] = { "OFF", "HARD", "SOFT" };
        segmented(kClip, clip, 3, static_cast<int>(std::lround(fValues[kParamClip])), 10);
    }

    void drawMonitorSection()
    {
        sectionTitle(kSecMon, "3", "MONITOR", "levels and options");
        drawMeters();
        divider(kSecMon.x + 12, kSecMon.x + kSecMon.w - 12, 312);

        label(kSecMon.x + 12, kOvers.y + kOvers.h * 0.5f, 10, cDim, ALIGN_LEFT | ALIGN_MIDDLE, "OVERSAMPLING", 1);
        const char* const os[3] = { "1x", "2x", "4x" };
        segmented(kOvers, os, 3, static_cast<int>(std::lround(fValues[kParamOversampling])), 10);

        const bool match = fValues[kParamMatch] > 0.5f;
        button(kMatch, match, nullptr);
        led(kMatch.x + 14, kMatch.y + kMatch.h * 0.5f, match);
        label(kMatch.x + kMatch.w * 0.5f + 8, kMatch.y + kMatch.h * 0.5f, 10, match ? cText : cDim,
              ALIGN_CENTER | ALIGN_MIDDLE, "LEVEL MATCH", 1);
        char loud[40];
        std::snprintf(loud, sizeof(loud), "%+.1f dB louder", fValues[kParamMeterLoud]);
        label(kMatch.x + kMatch.w + 12, kMatch.y + kMatch.h * 0.5f, 10, cGold, ALIGN_LEFT | ALIGN_MIDDLE, loud);

        char lat[64];
        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        std::snprintf(lat, sizeof(lat), "latency %.0f samples (%.2f ms)", fValues[kParamLatency],
                      fValues[kParamLatency] * 1000.0 / sr);
        label(kSecMon.x + 12, 408, 9, cDim, ALIGN_LEFT | ALIGN_MIDDLE, lat, 0.5f);
        label(kSecMon.x + 12, 424, 9, cFaint, ALIGN_LEFT | ALIGN_MIDDLE, "knobs: drag, wheel, shift = fine", 0.5f);
        label(kSecMon.x + 12, 438, 9, cFaint, ALIGN_LEFT | ALIGN_MIDDLE,
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
        const float track = k.r + (big ? 9.0f : 6.0f);
        const float tw = big ? 5.0f : 3.5f;

        beginPath();
        arc(k.cx, k.cy, track, kArcStart, kArcStart + kArcSweep, CW);
        lineCap(ROUND);
        strokeWidth(tw);
        strokeColor(Color(10, 4, 5));
        stroke();

        if (n > 0.001f)
        {
            beginPath();
            arc(k.cx, k.cy, track, kArcStart, a, CW);
            strokeWidth(tw * 3.2f);
            strokeColor(Color(255, 70, 20, active ? 0.30f : 0.18f));
            stroke();

            beginPath();
            arc(k.cx, k.cy, track, kArcStart, a, CW);
            strokeWidth(tw);
            strokePaint(linearGradient(k.cx - track, k.cy + track, k.cx + track, k.cy - track, cDeep,
                                       active ? cGold : Color(cGold, cHot, 0.4f)));
            stroke();
        }

        beginPath();
        circle(k.cx, k.cy + 4, k.r + 2);
        fillPaint(radialGradient(k.cx, k.cy + 4, k.r - 4, k.r + 8, Color(0, 0, 0, 0.7f), Color(0, 0, 0, 0.0f)));
        fill();

        beginPath();
        circle(k.cx, k.cy, k.r);
        fillPaint(linearGradient(k.cx, k.cy - k.r, k.cx, k.cy + k.r, Color(74, 52, 50), Color(24, 16, 17)));
        fill();
        beginPath();
        circle(k.cx, k.cy, k.r * 0.84f);
        fillPaint(radialGradient(k.cx - k.r * 0.3f, k.cy - k.r * 0.4f, 2, k.r * 1.1f,
                                 Color(62, 44, 42), Color(20, 13, 14)));
        fill();
        strokeWidth(1.0f);
        strokeColor(Color(0, 0, 0, 0.6f));
        stroke();

        const float ca = std::cos(a), sa = std::sin(a);
        beginPath();
        moveTo(k.cx + ca * k.r * 0.30f, k.cy + sa * k.r * 0.30f);
        lineTo(k.cx + ca * k.r * 0.78f, k.cy + sa * k.r * 0.78f);
        lineCap(ROUND);
        strokeWidth(big ? 4.0f : 3.0f);
        strokeColor(Color(255, 90, 30, 0.35f));
        stroke();
        strokeWidth(big ? 2.2f : 1.8f);
        strokeColor(active ? cGold : Color(255, 140, 70));
        stroke();

        char buf[32];
        valueText(p, buf, sizeof(buf));
        float vx, vy;
        int align;
        if (k.labelRight)
        {
            label(k.cx + k.r + 16, k.cy - 9, 12, cText, ALIGN_LEFT | ALIGN_MIDDLE, k.label, 2);
            vx = k.cx + k.r + 16;
            vy = k.cy + 9;
            align = ALIGN_LEFT | ALIGN_MIDDLE;
        }
        else
        {
            const float gap = big ? 26.0f : 17.0f;
            label(k.cx, k.cy + k.r + gap, big ? 13 : 11, cText, ALIGN_CENTER | ALIGN_MIDDLE, k.label, big ? 3 : 1.5f);
            vx = k.cx;
            vy = k.cy + k.r + gap + (big ? 17.0f : 15.0f);
            align = ALIGN_CENTER | ALIGN_MIDDLE;
        }

        if (i == fEdit)
        {
            const float bw = 76.0f, bx = k.labelRight ? vx - 4 : vx - bw * 0.5f;
            beginPath();
            roundedRect(bx, vy - 10, bw, 20, 4);
            fillColor(Color(10, 4, 5));
            fill();
            strokeWidth(1.0f);
            strokeColor(cGold);
            stroke();
            char e[40];
            std::snprintf(e, sizeof(e), "%s_", fEditText);
            label(bx + bw * 0.5f, vy, 12, cGold, ALIGN_CENTER | ALIGN_MIDDLE, e);
        }
        else
        {
            label(vx, vy, big ? 12 : 11, active ? cGold : cDim, align, buf);
        }
    }

    static float softClip(const float x, const float ceiling)
    {
        const float t = 0.6f * ceiling;
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
        fillColor(Color(12, 5, 6));
        fill();

        strokeWidth(1.0f);
        for (float db = -48.0f; db <= 0.0f; db += 12.0f)
        {
            beginPath();
            moveTo(gx(db) + 0.5f, y0);
            lineTo(gx(db) + 0.5f, y0 + h);
            moveTo(x0, gy(db) + 0.5f);
            lineTo(x0 + w, gy(db) + 0.5f);
            strokeColor(db == 0.0f ? Color(120, 50, 40, 0.8f) : Color(70, 34, 32, 0.6f));
            stroke();

            char buf[8];
            std::snprintf(buf, sizeof(buf), "%.0f", db);
            label(x0 - 4, gy(db), 9, cDim, ALIGN_RIGHT | ALIGN_MIDDLE, buf);
            label(gx(db), y0 + h + 9, 9, cDim, ALIGN_CENTER | ALIGN_MIDDLE, buf);
        }

        beginPath();
        moveTo(gx(lo), gy(lo));
        lineTo(gx(hi), gy(hi));
        strokeColor(Color(120, 90, 85, 0.35f));
        stroke();

        // ceiling line
        beginPath();
        moveTo(x0, gy(fValues[kParamCeiling]) + 0.5f);
        lineTo(x0 + w, gy(fValues[kParamCeiling]) + 0.5f);
        strokeColor(Color(255, 200, 120, 0.25f));
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
            strokeColor(pass == 0 ? Color(255, 70, 20, 0.22f) : cGold);
            stroke();
        }

        const float in = fValues[kParamMeterIn];
        if (in > lo + 0.5f)
        {
            const float px = gx(in), py = gy(curve(in));
            beginPath();
            circle(px, py, 9);
            fillPaint(radialGradient(px, py, 1, 9, Color(255, 200, 80, 0.8f), Color(255, 70, 20, 0.0f)));
            fill();
            beginPath();
            circle(px, py, 3);
            fillColor(Color(255, 236, 190));
            fill();
        }
        restore();

        label(kGraph.x + kGraph.w * 0.5f + 8, kGraph.y + kGraph.h - 6, 9, cDim, ALIGN_CENTER | ALIGN_MIDDLE,
              "INPUT dB  ->  OUTPUT dB", 1);
    }

    void meterBar(const float x, const float frac, const bool fromTop, const char* name, const char* value)
    {
        const float y = kMeterY, h = kMeterH, w = kMeterW;
        beginPath();
        roundedRect(x, y, w, h, 3);
        fillColor(Color(10, 4, 5));
        fill();
        strokeWidth(1.0f);
        strokeColor(Color(60, 26, 24));
        stroke();

        const float f = std::fmin(std::fmax(frac, 0.0f), 1.0f);
        if (f > 0.002f)
        {
            const float fh = (h - 4) * f;
            const float fy = fromTop ? y + 2 : y + h - 2 - fh;
            beginPath();
            roundedRect(x + 2, fy, w - 4, fh, 2);
            if (fromTop)
                fillPaint(linearGradient(0, y, 0, y + h, cHot, cDeep));
            else
                fillPaint(linearGradient(0, y + h, 0, y, cDeep, cGold));
            fill();
        }

        label(x + w * 0.5f, y + h + 13, 11, cText, ALIGN_CENTER | ALIGN_MIDDLE, name, 1);
        label(x + w * 0.5f, y + h + 28, 10, cDim, ALIGN_CENTER | ALIGN_MIDDLE, value);
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
        strokeColor(Color(255, 220, 160, 0.6f));
        stroke();
        for (float db = -48.0f; db <= 0.0f; db += 12.0f)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%.0f", db);
            label(kMeterX[0] + w + 13, ly(db), 8, cFaint, ALIGN_CENTER | ALIGN_MIDDLE, b);
            label(kMeterX[3] - 13, ly(db), 8, cFaint, ALIGN_CENTER | ALIGN_MIDDLE, b);
        }
        for (float db = -10.0f; db >= -30.0f; db -= 10.0f)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%.0f", db);
            label((kMeterX[1] + w + kMeterX[2]) * 0.5f, y + 2 + (h - 4) * (-db / 40.0f), 8, cFaint,
                  ALIGN_CENTER | ALIGN_MIDDLE, b);
        }
    }

    static float difference(const float in, const float out)
    {
        if (in < 1e-4f || out < 1e-6f)
            return kNoValue;   // silence: no ratio
        return 20.0f * std::log10(out / in);
    }

    static Color seriesColor(const int s)
    {
        switch (s)
        {
        case kSeriesIn:  return Color(170, 120, 100);
        case kSeriesOut: return cGold;
        case kSeriesGR:  return cHot;
        case kSeriesClip: return Color(120, 200, 255);
        default:         return Color(140, 225, 150);
        }
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
        fillColor(Color(12, 5, 6));
        fill();

        // legend: click to show / hide
        label(kHistory.x + 12, kHistory.y + 16, 10, cText, ALIGN_LEFT | ALIGN_MIDDLE, "HISTORY", 1);
        for (int s = 0; s < kNumSeries; ++s)
        {
            const Rect lr = legendRect(s);
            const Color c = seriesColor(s);
            beginPath();
            moveTo(lr.x, lr.y + 8.5f);
            lineTo(lr.x + 12, lr.y + 8.5f);
            lineCap(ROUND);
            strokeWidth(fShow[s] ? 2.0f : 1.0f);
            strokeColor(fShow[s] ? c : cFaint);
            stroke();
            label(lr.x + 18, lr.y + 8, 9.5f, fShow[s] ? cText : cFaint, ALIGN_LEFT | ALIGN_MIDDLE, kSeriesNames[s], 0.3f);
        }
        label(kHistory.x + 12, kHistory.y + kHistory.h - 8, 8, cFaint, ALIGN_LEFT | ALIGN_MIDDLE, "click: show / hide", 0.3f);

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
        strokeColor(Color(70, 34, 32, 0.5f));
        stroke();

        const float ceilY = r.y + r.h * std::fmin(-fValues[kParamCeiling] / kHistLevelRange, 1.0f);
        beginPath();
        for (float xx = r.x; xx < r.x + r.w; xx += 8.0f)
        {
            moveTo(xx, ceilY + 0.5f);
            lineTo(xx + 4.0f, ceilY + 0.5f);
        }
        strokeColor(Color(255, 200, 120, 0.35f));
        stroke();

        for (float db = 0.0f; db >= -kHistLevelRange + 1.0f; db -= 12.0f)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%.0f", db);
            label(r.x + r.w + 4, r.y + r.h * (-db / kHistLevelRange), 8, cFaint, ALIGN_LEFT | ALIGN_MIDDLE, b);
        }
        label(r.x + r.w + 4, r.y + r.h - 4, 7, cFaint, ALIGN_LEFT | ALIGN_MIDDLE, "dBFS");
        for (float db = -6.0f; db >= -kHistReductionRange + 1.0f; db -= 6.0f)
        {
            char b[8];
            std::snprintf(b, sizeof(b), "%.0f", db);
            label(r.x - 4, r.y + r.h * (-db / kHistReductionRange), 8, Color(cHot, cFaint, 0.5f),
                  ALIGN_RIGHT | ALIGN_MIDDLE, b);
        }

        // difference: centred scale (0 dB in the middle), inside the plot on the right
        if (fShow[kSeriesDiff])
        {
            const Color dc = Color(seriesColor(kSeriesDiff), cFaint, 0.4f);
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
        fillColor(Color(8, 3, 4, 0.95f));
        fill();
        strokeWidth(1.0f);
        strokeColor(Color(255, 110, 40, 0.6f));
        stroke();
        label(x + pad, y + 13, 11, cGold, ALIGN_LEFT | ALIGN_MIDDLE, title, 0.5f);
        fontSize(10);
        textLetterSpacing(0);
        fillColor(cText);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        textBox(x + pad, y + 24, w - 2 * pad, help, nullptr);
    }

    // ------------------------------------------------------------------ state

    float fValues[kParamCount];
    FontId fFont = 0;
    int fDrag = -1, fHover = -1, fEdit = -1;
    int fLastClickTarget = -1;
    uint fLastClickTime = 0;
    float fLastY = 0.0f;
    int fPreset = 0;
    char fEditText[16] = { 0 };
    bool fEditFresh = false;

    float fHist[kNumSeries][kHistorySize];
    bool fShow[kNumSeries];
    int fHistPos = 0;
    std::chrono::steady_clock::time_point fLastPush;
#ifdef BRIMSTONE_UI_DEMO_PRESET
    bool fDemoPresetDone = false;
#endif

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrimstoneUI)
};

UI* createUI()
{
    return new BrimstoneUI();
}

END_NAMESPACE_DISTRHO
