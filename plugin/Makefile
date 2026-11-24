#!/usr/bin/make -f
# Brimstone Maximizer (DPF) - see ../build.sh
# Copyright (C) 2026 Brimstone authors - SPDX-License-Identifier: GPL-2.0-or-later

NAME = brimstone_maximizer

FILES_DSP = BrimstonePlugin.cpp
FILES_UI  = BrimstoneUI.cpp

DPF_PATH       = ../dpf
DPF_TARGET_DIR ?= ../bin
DPF_BUILD_DIR  = ../build

include ../dpf/Makefile.plugins.mk

TARGETS += vst3 clap
ifneq ($(MACOS_OR_WINDOWS),true)
TARGETS += lv2_sep
endif

all: $(TARGETS)
