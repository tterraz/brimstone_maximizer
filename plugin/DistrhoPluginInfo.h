/*
 * Brimstone Maximizer - DPF plugin description.
 *
 * Copyright (C) 2026 Brimstone authors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Brimstone"
#define DISTRHO_PLUGIN_NAME    "Brimstone Maximizer"
#define DISTRHO_PLUGIN_URI     "urn:brimstone:maximizer"
#define DISTRHO_PLUGIN_CLAP_ID "brimstone.maximizer"

#define DISTRHO_PLUGIN_BRAND_ID  Brim
#define DISTRHO_PLUGIN_UNIQUE_ID BrMx

#define DISTRHO_PLUGIN_HAS_UI        1
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_LATENCY  1
#define DISTRHO_PLUGIN_WANT_PROGRAMS 1
#define DISTRHO_PLUGIN_WANT_STATE    1   // settings of the interface saved with the project (A/B, history)
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1 // the plugin keeps the state strings (getState)
#define DISTRHO_UI_USE_NANOVG        1
#define DISTRHO_UI_USER_RESIZABLE    1
#define DISTRHO_UI_DEFAULT_WIDTH     900
#define DISTRHO_UI_DEFAULT_HEIGHT    680

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Dynamics"

#endif
