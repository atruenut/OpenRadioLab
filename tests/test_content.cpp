// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "../src/learn/Content.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static bool ascii(const char* s) { for (; *s; ++s) if ((unsigned char)*s > 126 || ((unsigned char)*s < 32 && *s != '\n')) return false; return true; }
int main() {
    size_t n; const Lesson* L = lessons(n);
    CHECK(n >= 8, "expected at least 8 lessons, have %zu", n);
    for (size_t i = 0; i < n; ++i) {
        CHECK(L[i].title && *L[i].title && L[i].body && std::strlen(L[i].body) > 200, "lesson %zu is empty/short", i);
        CHECK(ascii(L[i].title) && ascii(L[i].body), "lesson %zu has non-ASCII or control characters", i);
        CHECK(L[i].tryHz == 0 || (L[i].tryLabel && *L[i].tryLabel), "lesson %zu has a Try button without a label", i);
        CHECK(L[i].tryMode >= 0 && L[i].tryMode <= 2, "lesson %zu bad mode", i);
    }
    size_t m; const Term* T = dictionary(m);
    std::set<std::string> seen; std::string prev;
    for (size_t i = 0; i < m; ++i) {
        CHECK(ascii(T[i].term) && ascii(T[i].meaning) && *T[i].meaning, "term %zu bad", i);
        CHECK(seen.insert(T[i].term).second, "duplicate term %s", T[i].term);
    }
    std::printf("%zu lessons, %zu dictionary terms\n%s (%d failure%s)\n", n, m, g_fail ? "FAILED" : "ALL CONTENT TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
