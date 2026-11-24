/*
 * Brimstone Maximizer - tests of the preferences file (theme of the interface).
 * Run with HOME / XDG_CONFIG_HOME (Linux) or APPDATA (Windows) pointing to an empty folder.
 *
 * Copyright (C) 2026 Brimstone authors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "BrimstonePrefs.hpp"

#include <cstdio>

using namespace BrimstonePrefs;

static int failures = 0;

static void check(const bool ok, const char* const what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

int main()
{
    check(loadTheme() == kThemeEmber, "no file: Ember");
    check(saveTheme(kThemeAsh), "save Ash");
    check(loadTheme() == kThemeAsh, "read back Ash");
    check(saveTheme(kThemeEmber) && loadTheme() == kThemeEmber, "save and read back Ember");
    check(!saveTheme(kThemeCount), "invalid theme refused");

    // unknown keys and values are ignored, the last valid line wins
    std::FILE* const f = openFile("w");
    check(f != nullptr, "open for writing");
    if (f != nullptr)
    {
        std::fputs("# comment\r\nvolume=11\r\ntheme=ash\r\ntheme=purple\r\n", f);
        std::fclose(f);
    }
    check(loadTheme() == kThemeAsh, "unknown lines ignored (CRLF)");

    std::printf("%s\n", failures == 0 ? "all prefs tests passed" : "PREFS TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
