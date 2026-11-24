#!/bin/sh
# Brimstone Maximizer - build everything from Linux.
#   ./build.sh            DSP tests, Windows (VST3, CLAP), Linux (VST3, CLAP, LV2)
#   ./build.sh windows    DSP tests + Windows only
# Copyright (C) 2026 Brimstone authors - SPDX-License-Identifier: GPL-2.0-or-later
set -e
cd "$(dirname "$0")"
JOBS=${JOBS:-7}
[ -d dpf/dgl ] || git clone --recursive https://github.com/DISTRHO/DPF.git dpf
# DPF version used and tested; local fixes in patches/ (applied once)
git -C dpf checkout -q 4238e1c7f0351bbe488d79f0899c540543ac7583 2>/dev/null || true
for p in patches/*.patch; do
    git -C dpf apply --reverse --check "../$p" 2>/dev/null || git -C dpf apply "../$p"
done

echo "== DSP tests"
mkdir -p build
g++ -O2 -std=gnu++17 -Iplugin -Idpf/distrho -Idpf/dgl -DDISTRHO_PLUGIN_TARGET_EXPORT \
    -DDISTRHO_IS_STANDALONE=0 tests/test_dsp.cpp -o build/test_dsp -lpthread
./build/test_dsp

echo "== Windows (MinGW-w64)"
make -j"$JOBS" -C plugin BUILD_DIR_SUFFIX=-win DPF_TARGET_DIR=../bin/windows \
     CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix AR=x86_64-w64-mingw32-ar

if [ "$1" != windows ]; then
    echo "== Linux"
    make -j"$JOBS" -C plugin BUILD_DIR_SUFFIX=-linux DPF_TARGET_DIR=../bin/linux
    make -C dpf utils/lv2_ttl_generator >/dev/null
    bash dpf/utils/generate-ttl.sh bin/linux   # LV2 description files (.ttl)
fi

if command -v makensis >/dev/null 2>&1; then
    echo "== Windows installer"
    makensis -V2 installer/brimstone.nsi
else
    echo "== Windows installer skipped (makensis not found: apt install nsis)"
fi
ls bin/*
