#!/usr/bin/env python3
"""Brimstone Maximizer - figures of the user manual (doc/manuel.tex).

python3 doc/figures.py  ->  doc/fig_*.pdf

Every curve is computed with the formulas of plugin/BrimstonePlugin.cpp (core at 1x, soft clip,
half-band filter, K-weighting), written again here in NumPy.
Copyright (C) 2026 Brimstone authors - SPDX-License-Identifier: GPL-2.0-or-later
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ORANGE, OCHRE, DARK, GREY, BLUE, GREEN = "#c2410c", "#a8650f", "#2a2421", "#8c8279", "#1f6fb2", "#2e8b4e"
plt.rcParams.update({"font.size": 9, "axes.grid": True, "grid.alpha": 0.3, "axes.spines.top": False,
                     "axes.spines.right": False, "legend.frameon": False, "figure.dpi": 150})


def db(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), 1e-12))


def gain(dbv):
    return 10.0 ** (np.asarray(dbv) / 20.0)


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, name))
    plt.close(fig)


# ---------------------------------------------------------------------------------- DSP formulas

def soft_clip(x, ceiling, knee_db):
    t = ceiling * gain(-knee_db)
    a = np.abs(x)
    r = ceiling - t
    y = np.where(a <= t, a, t + r * np.tanh((a - t) / r))
    return np.sign(x) * y


def core(x, decay, knee_db, ceiling_db=-0.1, clip="off", clip_knee=1.0):
    """Maximizer at 1x, one channel, mix 100 %, drive 0, gain 0: returns output (aligned with
    the input, latency removed), envelope at the time the gain is applied, gain."""
    knee, ceil = gain(knee_db), gain(ceiling_db)
    tr = 1.0 / decay
    look = int(round(decay * 0.5))
    env = np.zeros(len(x))
    e = 0.0
    for n, a in enumerate(np.abs(x)):
        e = a if a > e else a * tr + e * (1.0 - tr)
        env[n] = e
    # the gain applied to x[n] comes from the envelope look samples later (anticipation)
    env_at = np.concatenate([env[look:], np.full(look, env[-1])])
    g = 1.0 / np.maximum(env_at, knee)
    y = x * g * ceil
    if clip == "hard":
        y = np.clip(y, -ceil, ceil)
    elif clip == "soft":
        y = soft_clip(y, ceil, clip_knee)
    return y, env_at, g


def halfband(taps=107):
    """Half-band FIR of the oversampler (Kaiser beta = 8), normalised to a DC gain of 1: 107 taps
    for the stage at the host Nyquist frequency (47 before 2026-11-25, and for the second stage of
    4x)."""
    center, beta = (taps - 1) // 2, 8.0
    n = np.arange(taps)
    t = (n - center) * 0.5
    h = 0.5 * np.sinc(t) * np.i0(beta * np.sqrt(np.maximum(0.0, 1.0 - ((n - center) / center) ** 2))) / np.i0(beta)
    return h / h.sum()


def kweighting(sr):
    """K-weighting of ITU-R BS.1770 (libebur128 formulas): two biquads (b, a)."""
    f0, g, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = np.tan(np.pi * f0 / sr)
    vh, vb = 10 ** (g / 20), (10 ** (g / 20)) ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    b1 = [(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0]
    a1 = [1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    f0, q = 38.13547087602444, 0.5003270373238773
    k = np.tan(np.pi * f0 / sr)
    a0 = 1 + k / q + k * k
    b2 = [1, -2, 1]
    a2 = [1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    return (b1, a1), (b2, a2)


def response(b, a, f, sr):
    z = np.exp(-2j * np.pi * f / sr)
    return np.polyval(b[::-1], z) / np.polyval(a[::-1], z)


# ---------------------------------------------------------------------------------- figures

def fig_transfer():
    """Static curve: output level for a steady input level (clip off, ceiling -0.1)."""
    fig, ax = plt.subplots(figsize=(5.6, 3.4))
    lin = np.linspace(-60, 6, 600)
    x = gain(lin)
    for knee, col in ((-6, OCHRE), (-12, ORANGE), (-24, DARK)):
        y = x / np.maximum(x, gain(knee)) * gain(-0.1)
        ax.plot(lin, db(y), color=col, label=f"Knee {knee} dB")
    k, mix = gain(-12), 0.5
    y = (x + (x / np.maximum(x, k) * gain(-0.1) - x) * mix)
    ax.plot(lin, db(y), color=ORANGE, ls="--", label="Knee -12 dB, Mix 50 %")
    ax.plot(lin, lin, color=GREY, lw=0.8, ls=":", label="sans traitement")
    ax.set_xlabel("niveau d'entrée stable (dBFS)")
    ax.set_ylabel("niveau de sortie (dBFS)")
    ax.set_xlim(-60, 6)
    ax.set_ylim(-60, 6)
    ax.legend(loc="lower right")
    save(fig, "fig_transfert.pdf")


def fig_time():
    """One hit on a quiet tone: input, envelope, gain and output (decay 30 samples, knee -12)."""
    sr = 44100.0
    n = np.arange(400)
    x = 0.08 * np.sin(2 * np.pi * 1500 * n / sr)
    hit = (n >= 150)
    x = x + hit * 0.9 * np.exp(-(n - 150) / 25.0) * np.sin(2 * np.pi * 900 * (n - 150) / sr + 1.2)
    decay, knee = 30.0, -12.0
    y_off, env_at, g = core(x, decay, knee)
    y_soft, _, _ = core(x, decay, knee, clip="soft", clip_knee=1.0)
    fig, axs = plt.subplots(3, 1, figsize=(5.8, 5.4), sharex=True)
    t = n
    axs[0].plot(t, x, color=GREY, lw=0.9, label="entrée x")
    axs[0].plot(t, env_at, color=ORANGE, lw=1.2, label="enveloppe e(n + a), a = 15")
    axs[0].axhline(gain(knee), color=DARK, lw=0.8, ls="--", label="knee (-12 dB)")
    axs[0].legend(loc="upper right", fontsize=7)
    axs[0].set_ylabel("amplitude")
    axs[1].plot(t, db(g * gain(-0.1)), color=ORANGE, lw=1.2)
    axs[1].set_ylabel("gain appliqué (dB)")
    axs[1].axvline(150 - 15, color=GREY, lw=0.8, ls=":")
    axs[1].annotate("la réduction commence\na = 15 éch. avant la crête", xy=(135, 0), xytext=(190, 6),
                    fontsize=7, arrowprops=dict(arrowstyle="->", color=GREY))
    axs[2].plot(t, y_off, color=ORANGE, lw=1.0, label="sortie, Clip Off")
    axs[2].plot(t, y_soft, color=BLUE, lw=0.9, label="sortie, Clip Soft (knee 1 dB)")
    axs[2].axhline(gain(-0.1), color=DARK, lw=0.8, ls="--")
    axs[2].axhline(-gain(-0.1), color=DARK, lw=0.8, ls="--", label="plafond (-0.1 dB)")
    axs[2].legend(loc="lower right", fontsize=7)
    axs[2].set_ylabel("amplitude")
    axs[2].set_xlabel("temps (échantillons à 44,1 kHz, latence retirée)")
    save(fig, "fig_temps.pdf")
    return float(np.max(np.abs(y_off)))


def fig_overshoot():
    """Largest overshoot of Clip Off for an isolated peak: (1 - 1/d)^(-round(d/2))."""
    d = np.arange(2, 31)
    look = np.round(d * 0.5 + 1e-9)
    over = -look * 20 * np.log10(1 - 1 / d)
    fig, ax = plt.subplots(figsize=(5.0, 2.6))
    ax.plot(d, over, "o-", color=ORANGE, ms=3)
    ax.set_xlabel("Decay (échantillons, unité d'origine)")
    ax.set_ylabel("dépassement max. (dB)")
    ax.set_ylim(0, 8)
    ax.set_xlim(1, 31)
    save(fig, "fig_depassement.pdf")
    return over[0], over[1], over[-1]


def fig_softclip():
    fig, ax = plt.subplots(figsize=(5.0, 3.2))
    x = np.linspace(0, 2.0, 800)
    ax.plot(x, np.minimum(x, 1.0), color=DARK, lw=1.0, label="Hard")
    for k, col in ((0.1, OCHRE), (1.0, ORANGE), (2.0, BLUE), (4.44, GREEN), (6.0, GREY)):
        ax.plot(x, soft_clip(x, 1.0, k), color=col, lw=1.1, label=f"Soft, knee {k:g} dB")
    ax.plot(x, x, color=GREY, lw=0.7, ls=":")
    ax.set_xlim(0, 2)
    ax.set_ylim(0, 1.1)
    ax.set_xlabel("entrée de l'écrêteur (en multiple du plafond)")
    ax.set_ylabel("sortie (multiple du plafond)")
    ax.legend(loc="lower right", fontsize=7)
    save(fig, "fig_ecreteur.pdf")


def fig_halfband():
    """2x stage at a host rate of 44.1 kHz: one filter and the chain up + down; 107 taps, and the
    47 taps used before for comparison."""
    f = np.linspace(0, 0.5, 8000)   # cycles per high-rate sample
    fhost = 44.1
    k = f * 2 * fhost
    fig, ax = plt.subplots(figsize=(5.6, 3.1))
    resp = {}
    for taps in (107, 47):
        h = halfband(taps)
        resp[taps] = np.abs(np.exp(-2j * np.pi * np.outer(f, np.arange(len(h)))) @ h)
    ax.plot(k, db(resp[107]), color=ORANGE, label="107 coefficients : un filtre")
    ax.plot(k, db(resp[107] ** 2), color=BLUE, lw=0.9, label="107 coefficients : montée + descente")
    ax.plot(k, db(resp[47] ** 2), color=GREY, lw=0.8, ls="--", label="47 coefficients (pour comparaison) : montée + descente")
    ax.axvline(fhost / 2, color=GREY, ls=":", lw=0.8)
    ax.axvline(20.0, color=GREEN, ls="--", lw=0.8)
    ax.text(fhost / 2 + 7.0, -15, "Nyquist de l'hôte\n22,05 kHz", fontsize=7)
    ax.annotate("", xy=(fhost / 2, -12), xytext=(fhost / 2 + 6.8, -12), arrowprops=dict(arrowstyle="->", color=GREY))
    ax.text(19.6, -45, "20 kHz", fontsize=7, ha="right")
    ax.set_xlim(0, fhost)
    ax.set_ylim(-120, 5)
    ax.set_xlabel("fréquence (kHz), étage 2x d'un hôte à 44,1 kHz")
    ax.set_ylabel("réponse (dB)")
    ax.legend(loc="lower left", fontsize=7)
    save(fig, "fig_demi_bande.pdf")
    i20 = np.argmin(np.abs(k - 20.0))
    return db(resp[107][i20] ** 2), np.max(db(resp[107][k >= 24.2]))


def fig_kweighting():
    fig, ax = plt.subplots(figsize=(5.4, 3.0))
    f = np.logspace(np.log10(10), np.log10(22000), 800)
    (b1, a1), (b2, a2) = kweighting(48000.0)
    H = response(b1, a1, f, 48000.0) * response(b2, a2, f, 48000.0)
    ax.semilogx(f, db(H), color=ORANGE)
    ax.set_xlabel("fréquence (Hz)")
    ax.set_ylabel("pondération K (dB)")
    ax.set_xlim(10, 22000)
    ax.set_ylim(-25, 6)
    save(fig, "fig_ponderation_k.pdf")


def fig_delta():
    """Clipper preset (mix 0, gain +3 dB, hard at -0.3 dB): output and delta."""
    sr = 44100.0
    n = np.arange(300)
    x = 0.95 * np.sin(2 * np.pi * 220 * n / sr) * (0.6 + 0.4 * np.sin(2 * np.pi * 3 * n / sr))
    g, c = gain(3.0), gain(-0.3)
    y = np.clip(x * g, -c, c)
    fig, ax = plt.subplots(figsize=(5.4, 2.8))
    ax.plot(n, x * g, color=GREY, lw=0.9, label="linéaire : entrée × Gain")
    ax.plot(n, y, color=ORANGE, lw=1.1, label="sortie (écrêtée au plafond)")
    ax.plot(n, y - x * g, color=BLUE, lw=1.1, label="Delta = sortie − linéaire")
    ax.set_xlabel("temps (échantillons)")
    ax.set_ylabel("amplitude")
    ax.legend(loc="lower left", fontsize=7)
    save(fig, "fig_delta.pdf")


if __name__ == "__main__":
    peak = fig_time()
    o2, o3, o30 = fig_overshoot()
    fig_transfer()
    fig_softclip()
    a20, stop = fig_halfband()
    fig_kweighting()
    fig_delta()
    print(f"hit, clip off: output peak {db(peak):+.2f} dBFS")
    print(f"largest overshoot: decay 2 -> {o2:.2f} dB, 3 -> {o3:.2f} dB, 30 -> {o30:.2f} dB")
    print(f"half-band 2x at 44.1 kHz (107 taps): chain {a20:.3f} dB at 20 kHz, one filter above 24.2 kHz <= {stop:.1f} dB")
